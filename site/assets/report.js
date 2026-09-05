/* The report box: one form, drawn wherever a page asks for it.
 *
 * Two pages show it. The front page keeps it in a <dialog> behind the round
 * button in the bottom-right corner, so saying what happened never means
 * leaving the page you were on. /report/ shows the same form as a page of its
 * own, for the deep links and the QR code that already point there. The
 * markup and the behaviour live here once so the two cannot drift; each page
 * supplies the surroundings (a heading, and on the front page the dialog and
 * whatever opens it).
 *
 * A page mounts the form with <div data-report-mount></div>. A <dialog
 * id="report-dialog"> plus any element carrying data-report-open turn on the
 * dialog behaviour; a page without them simply has the form in it. The
 * openers keep href="/report/" so a browser without <dialog>, or without
 * scripts, still gets somewhere.
 *
 * Which device is two checkboxes, not a radio with a "not sure": a person
 * reporting from a device knows which one they hold, and a report that is
 * about both is about both. At least one is required; the function refuses a
 * report naming neither, so the check here is a courtesy rather than the gate.
 */
(function () {
  "use strict";

  // Same repository the Install button reads its release from.
  var REPO = "ma-r-s/crossplay";

  var TEMPLATE = `
<form class="report-form" id="report-form" novalidate>
  <div class="report-row report-choices">
    <div class="report-field">
      <span class="report-label" id="report-kind-label">This is</span>
      <div class="report-chips" role="radiogroup" aria-labelledby="report-kind-label">
        <label><input type="radio" name="kind" value="bug" checked><span>Something broke</span></label>
        <label><input type="radio" name="kind" value="idea"><span>An idea</span></label>
      </div>
    </div>
    <div class="report-field">
      <span class="report-label" id="report-device-label">On which device</span>
      <div class="report-chips" role="group" aria-labelledby="report-device-label">
        <label><input type="checkbox" name="device" value="x4pro"><span>X4 Pro</span></label>
        <label><input type="checkbox" name="device" value="sticky"><span>Sticky</span></label>
      </div>
    </div>
  </div>

  <div class="report-field">
    <label class="report-label" for="report-what">What happened</label>
    <textarea id="report-what" name="what" required maxlength="4000" rows="6"
      placeholder="What were you doing, what happened, and what did you expect instead?"></textarea>
  </div>

  <div class="report-row">
    <div class="report-field">
      <label class="report-label" for="report-version">Firmware version <small>(optional)</small></label>
      <input type="text" id="report-version" name="version" inputmode="decimal" placeholder="" autocomplete="off">
      <p class="report-hint">Settings &gt; About on the device.</p>
    </div>
    <div class="report-field">
      <label class="report-label" for="report-email">Your email <small>(optional)</small></label>
      <input type="email" id="report-email" name="email" autocomplete="email" placeholder="you@example.com">
      <p class="report-hint">Only if you want a reply.</p>
    </div>
  </div>

  <div class="report-field">
    <span class="report-label">A photo <small>(optional)</small></span>
    <div class="report-photo">
      <input type="file" id="report-photo" name="photo" accept="image/*">
      <label class="report-btn" for="report-photo">Add a photo</label>
      <img id="report-thumb" class="report-thumb" alt="" hidden>
      <button type="button" class="report-btn" id="report-photo-clear" hidden>Remove</button>
    </div>
    <p class="report-hint">Of the screen, if the problem is something you can see. It is resized before it is sent.</p>
  </div>

  <div class="report-hp" aria-hidden="true">
    <label for="report-website">Leave this empty</label>
    <input type="text" id="report-website" name="website" tabindex="-1" autocomplete="off">
  </div>

  <input type="hidden" id="report-app" name="app" value="">

  <div class="report-foot">
    <p class="report-status" id="report-status" role="status" aria-live="polite"></p>
    <button type="submit" class="report-btn primary" id="report-send">Send</button>
  </div>
</form>

<section class="report-done" id="report-done" hidden>
  <h2>Got it.</h2>
  <p>Your report is <span class="report-num" id="report-id"></span>.</p>
  <p>Save that number if you ever write in about it. That is all there is to do.</p>
  <p id="report-photo-note" hidden>The photo did not make it, but the report did. You can send it again with the number above in the text.</p>
  <p><button type="button" class="report-btn" id="report-again">Send another</button></p>
</section>
`;

  function $(id) {
    return document.getElementById(id);
  }

  // Resize on the phone, not the server: a buffered request is capped at
  // 4.5MB and a camera photo is bigger than that. 1600px on the long side is
  // plenty to read an e-ink screen.
  function shrink(file) {
    return new Promise(function (resolve) {
      var img = new Image();
      var url = URL.createObjectURL(file);
      img.onload = function () {
        var max = 1600,
          w = img.naturalWidth,
          h = img.naturalHeight;
        var s = Math.min(1, max / Math.max(w, h));
        var c = document.createElement("canvas");
        c.width = Math.round(w * s);
        c.height = Math.round(h * s);
        c.getContext("2d").drawImage(img, 0, 0, c.width, c.height);
        URL.revokeObjectURL(url);
        c.toBlob(
          function (b) {
            resolve(b || file);
          },
          "image/jpeg",
          0.85,
        );
      };
      img.onerror = function () {
        URL.revokeObjectURL(url);
        resolve(null);
      };
      img.src = url;
    });
  }

  // The version field's placeholder is the version the site is shipping, asked
  // for rather than typed here: a number written into this file is right on the
  // day it is written and wrong at the next release, and this one had drifted
  // eleven releases behind before anyone noticed. Same source as the Install
  // button, so the two can never disagree. A failed fetch leaves the field
  // blank -- the hint under it already says where to find the number -- because
  // an empty placeholder is better than a confidently wrong one.
  function fillVersionPlaceholder(version) {
    if (version.value) return; // ?v= already said which
    fetch("https://api.github.com/repos/" + REPO + "/releases/latest")
      .then(function (r) {
        return r.ok ? r.json() : null;
      })
      .then(function (data) {
        if (!data || !data.tag_name || version.value) return;
        version.placeholder = String(data.tag_name).replace(/^v/, "");
      })
      .catch(function () {});
  }

  // Draws the form into `root` and wires it. Returns the handful of things a
  // host page needs: where to put focus, whether a report has been sent, and
  // how to start over.
  function mount(root) {
    root.innerHTML = TEMPLATE;
    var form = $("report-form"),
      what = $("report-what"),
      version = $("report-version"),
      email = $("report-email");
    var photo = $("report-photo"),
      thumb = $("report-thumb"),
      clear = $("report-photo-clear");
    var send = $("report-send"),
      status = $("report-status"),
      done = $("report-done"),
      idOut = $("report-id");
    var photoNote = $("report-photo-note"),
      again = $("report-again"),
      website = $("report-website"),
      app = $("report-app");
    var pickedBlob = null;

    function say(text, err) {
      status.textContent = text;
      status.className = "report-status" + (err ? " err" : "");
    }

    function devices() {
      var out = [];
      var boxes = form.querySelectorAll('input[name="device"]:checked');
      for (var i = 0; i < boxes.length; i++) out.push(boxes[i].value);
      return out;
    }

    // The device's own web UI can open the page with everything it knows
    // filled in: /report/?device=x4pro&v=1.12.9&app=trivia&kind=bug. Two
    // devices are a comma: device=x4pro,sticky. Values go into a selector, so
    // only plain words are looked up at all.
    var q = new URLSearchParams(location.search);
    if (q.get("v")) version.value = q.get("v").replace(/^v/, "");
    (q.get("device") || "").split(",").forEach(function (v) {
      v = v.trim();
      if (!/^[a-z0-9]+$/.test(v)) return;
      var d = form.querySelector('input[name="device"][value="' + v + '"]');
      if (d) d.checked = true;
    });
    if (q.get("kind") === "idea")
      form.querySelector('input[name="kind"][value="idea"]').checked = true;
    if (q.get("app")) app.value = q.get("app");
    fillVersionPlaceholder(version);

    photo.addEventListener("change", function () {
      var f = photo.files && photo.files[0];
      if (!f) return;
      shrink(f).then(function (b) {
        if (!b) {
          say("That file is not a photo we can read.", true);
          photo.value = "";
          return;
        }
        pickedBlob = b;
        thumb.src = URL.createObjectURL(b);
        thumb.hidden = false;
        clear.hidden = false;
        say("");
      });
    });
    function dropPhoto() {
      pickedBlob = null;
      photo.value = "";
      thumb.hidden = true;
      clear.hidden = true;
    }
    clear.addEventListener("click", dropPhoto);

    form.addEventListener("submit", function (ev) {
      ev.preventDefault();
      var text = what.value.trim();
      if (text.length < 8) {
        say("Say a little more about what happened.", true);
        what.focus();
        return;
      }
      var picked = devices();
      if (!picked.length) {
        say("Which device? Pick at least one.", true);
        form.querySelector('input[name="device"]').focus();
        return;
      }
      send.disabled = true;
      say("Sending...");
      var body = {
        kind: form.querySelector('input[name="kind"]:checked').value,
        what: text,
        device: picked.join(","),
        version: version.value.trim(),
        email: email.value.trim(),
        app: app.value,
        website: website.value,
      };
      fetch("/api/report", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      })
        .then(function (r) {
          return r.json().then(function (j) {
            return { ok: r.ok, j: j };
          });
        })
        .then(function (res) {
          if (!res.ok)
            throw new Error(
              (res.j && res.j.error) || "Something went wrong sending it.",
            );
          var id = res.j.id;
          if (!pickedBlob || !id) return { id: id, photoOk: true };
          say("Sending the photo...");
          return fetch("/api/report?photo=" + id, {
            method: "PUT",
            headers: { "Content-Type": "image/jpeg" },
            body: pickedBlob,
          }).then(
            function (r) {
              return { id: id, photoOk: r.ok };
            },
            function () {
              return { id: id, photoOk: false };
            },
          );
        })
        .then(function (out) {
          form.hidden = true;
          done.hidden = false;
          idOut.textContent = "#" + out.id;
          photoNote.hidden = out.photoOk;
          say("");
          done.scrollIntoView({ block: "nearest" });
          again.focus();
        })
        .catch(function (err) {
          say(err.message || "Something went wrong sending it.", true);
          send.disabled = false;
        });
    });

    function reset() {
      form.reset();
      dropPhoto();
      send.disabled = false;
      say("");
      done.hidden = true;
      form.hidden = false;
    }
    again.addEventListener("click", function () {
      reset();
      what.focus();
    });

    return {
      focus: function () {
        (done.hidden ? what : again).focus();
      },
      isDone: function () {
        return !done.hidden;
      },
      reset: reset,
    };
  }

  var root = document.querySelector("[data-report-mount]");
  if (!root) return;
  var box = mount(root);

  // -- the dialog, on pages that have one ------------------------------------
  var dialog = $("report-dialog");
  if (!dialog || typeof dialog.showModal !== "function") return;
  var closeBtn = $("report-close");
  var openers = document.querySelectorAll("[data-report-open]");
  var opener = null;

  function open(from) {
    opener = from || document.activeElement;
    dialog.showModal();
    // The page behind is inert while the dialog is up; this keeps it from
    // scrolling under the sheet as well.
    document.documentElement.classList.add("report-open");
    box.focus();
  }

  // Escape is the browser's own cancel; the backdrop is the dialog element
  // itself, since its whole box is covered by the sheet inside it.
  dialog.addEventListener("click", function (ev) {
    if (ev.target === dialog) dialog.close();
  });
  if (closeBtn)
    closeBtn.addEventListener("click", function () {
      dialog.close();
    });
  dialog.addEventListener("close", function () {
    document.documentElement.classList.remove("report-open");
    // A report that went through is finished; the next open is a fresh box.
    // One that was only half written stays as it was.
    if (box.isDone()) box.reset();
    if (opener && typeof opener.focus === "function") opener.focus();
    opener = null;
  });
  for (var i = 0; i < openers.length; i++) {
    openers[i].addEventListener("click", function (ev) {
      ev.preventDefault();
      open(ev.currentTarget);
    });
  }
})();
