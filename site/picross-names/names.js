/* Naming the Picross puzzles.
 *
 * 137 solved 10x10 pictures, one person, by hand. The whole design is entry
 * speed: the picture, a focused field, Enter, next. Nothing here needs a mouse
 * and nothing here needs a network.
 *
 * THE ONE UNFORGIVABLE FAILURE is losing work, so every commit writes
 * localStorage synchronously before anything moves, and the half-typed word in
 * the field is written too. See save().
 *
 * That draft is also the sharpest edge in here. A cold review found it
 * surviving a save-file load and reappearing on top of the answer the load had
 * just merged in, where one Enter overwrote that answer with a newer timestamp
 * and merging back could not recover it. Clearing it is part of merging now,
 * in logic.js, where a test can hold it to that.
 *
 * The other thing this does that a plain text box could not: it knows what the
 * device can draw. The win screen sets the name in toybox_30 and puts it
 * through toybox::fittedTitle, which steps down to toybox_20 and then to
 * toybox_10 before it ellipsizes -- and toybox_20 and toybox_30 carry
 * U+0020..U+007E and NOTHING else, where a codepoint with no glyph draws as
 * nothing at all rather than as a box. So an accent is a HOLE in the word, and
 * this tool says so while he is typing instead of leaving him to find it on the
 * panel. data.js carries the real advance widths; tools_local/picross/
 * gen_name_tool.py regenerates it from the font headers and the bank.
 */
(function () {
  "use strict";

  var DATA = window.PICROSS_NAME_DATA;
  var boot = document.getElementById("boot");
  if (!DATA || !DATA.puzzles || !DATA.puzzles.length) {
    boot.textContent = "The puzzle data did not load. Run tools_local/picross/gen_name_tool.py.";
    return;
  }

  var STORE_KEY = "crossplay.picross.names.v1";

  // What the file card #390 consumes will accept. Its generator makes each of
  // these a HARD ERROR on the whole file, so a name that breaks one is not a
  // warning to read later: it is 137 names rejected at the end. Checked here,
  // on the picture it belongs to, while there is still something to do about it.
  //
  // The charset is tighter than the font is -- toybox_30 draws every printable
  // ASCII character -- and the cap is a proxy for a width this tool measures
  // exactly. Both are recorded on the card as things that could be relaxed;
  // until they are, this enforces what the consumer enforces.
  var ALLOWED = /^[A-Z0-9 -]*$/;
  var MAX_CHARS = 16;
  var puzzles = DATA.puzzles;
  var total = puzzles.length;
  var indexById = {};
  puzzles.forEach(function (p, i) {
    indexById[p.id] = i;
  });

  // --- state ---------------------------------------------------------------
  //
  // entries[id] is one of:
  //   absent            not looked at yet, or skipped
  //   {name: "..."}     named
  //   {nameless: true}  looked at, deliberately has no good name
  //
  // Skipping deliberately writes nothing: "I came back to this later" and "I
  // decided it has no name" are different answers and the export must be able
  // to tell them apart.
  var state = { v: 1, entries: {}, pos: 0, draft: "" };
  var storageWorks = true;

  function load() {
    var raw = null;
    try {
      raw = window.localStorage.getItem(STORE_KEY);
    } catch (e) {
      storageWorks = false;
      return;
    }
    if (!raw) return;
    try {
      var parsed = JSON.parse(raw);
      if (parsed && parsed.entries) {
        state.entries = parsed.entries;
        // Clamped HERE, not trusted. An out-of-range position threw inside
        // show() AFTER the page had revealed itself, leaving a tool that read
        // "0 of 137", accepted typing and discarded every name in silence. The
        // bank shrinks when card #390 lands, so a position saved today can be
        // past the end tomorrow.
        state.pos = L.clampPos(parsed.pos, total);
        state.draft = typeof parsed.draft === "string" ? parsed.draft : "";
      }
    } catch (e) {
      /* A corrupt blob is not a reason to start over silently. Keep it where a
         human can still get at it and carry on with an empty board. */
      try {
        window.localStorage.setItem(STORE_KEY + ".broken." + Date.now(), raw);
      } catch (e2) {
        /* nothing else to try */
      }
    }
  }

  var savedNote = document.getElementById("savedNote");
  var flashTimer = null;

  function save(quiet) {
    if (!storageWorks) return;
    try {
      state.updated = new Date().toISOString();
      window.localStorage.setItem(STORE_KEY, JSON.stringify(state));
    } catch (e) {
      storageWorks = false;
      savedNote.textContent = "THIS BROWSER IS NOT SAVING. Download the save file now.";
      savedNote.classList.add("is-flash");
      return;
    }
    if (quiet) return;
    savedNote.textContent = "Saved";
    savedNote.classList.add("is-flash");
    window.clearTimeout(flashTimer);
    flashTimer = window.setTimeout(function () {
      savedNote.classList.remove("is-flash");
      savedNote.textContent = storageNoteText();
    }, 900);
  }

  function storageNoteText() {
    if (!storageWorks) return "This browser is not saving. Download the save file often.";
    return "Saved in this browser (" + window.location.host + ")";
  }

  // --- what the panel can draw --------------------------------------------
  //
  // The judgement itself lives in logic.js, with no DOM around it, so
  // host-tests/picrossnames can drive it. See that file for what each rule is
  // and why it is in the order it is.
  var L = window.PicrossNamesLogic;
  if (!L) {
    boot.textContent = "logic.js did not load; the tool cannot judge a name without it.";
    return;
  }

  function judge(name) {
    return L.judge(name, DATA);
  }


  // --- drawing -------------------------------------------------------------

  // The device fills solid cells and draws nothing else: no grid, no border.
  // Drawing a grid here would have him name a picture the panel does not show.
  function paint(canvas, puzzle) {
    var ctx = canvas.getContext("2d");
    var n = DATA.size;
    canvas.width = n;
    canvas.height = n;
    ctx.clearRect(0, 0, n, n);
    var ink = getComputedStyle(document.body).getPropertyValue("color") || "#111110";
    ctx.fillStyle = ink.trim();
    for (var r = 0; r < n; r++) {
      var bits = puzzle.rows[r] || 0;
      for (var c = 0; c < n; c++) {
        if (bits & (1 << c)) ctx.fillRect(c, r, 1, 1);
      }
    }
  }

  // The browse list is 137 pictures at once, and 137 live canvases is a lot to
  // ask of a phone. One scratch canvas, rendered to a 10x10 PNG per puzzle and
  // cached: the list is then plain <img>, which the browser composites without
  // help. The cache is cleared when the theme flips, since the ink changes.
  var thumbCache = {};
  var scratch = document.createElement("canvas");

  function thumb(puzzle) {
    if (thumbCache[puzzle.id]) return thumbCache[puzzle.id];
    paint(scratch, puzzle);
    thumbCache[puzzle.id] = scratch.toDataURL("image/png");
    return thumbCache[puzzle.id];
  }

  // --- the lane ------------------------------------------------------------

  var el = {
    main: document.querySelector(".pn"),
    picture: document.getElementById("picture"),
    field: document.getElementById("nameField"),
    form: document.getElementById("entryForm"),
    verdict: document.getElementById("verdict"),
    preview: document.getElementById("previewName"),
    posNow: document.getElementById("posNow"),
    posTotal: document.getElementById("posTotal"),
    id: document.getElementById("puzzleId"),
    author: document.getElementById("puzzleAuthor"),
    source: document.getElementById("puzzleSource"),
    fill: document.getElementById("progressFill"),
    countDone: document.getElementById("countDone"),
    countTotal: document.getElementById("countTotal"),
    countNamed: document.getElementById("countNamed"),
    countNameless: document.getElementById("countNameless"),
    countLeft: document.getElementById("countLeft"),
    grid: document.getElementById("grid"),
    gridCells: document.getElementById("gridCells"),
    browseBtn: document.getElementById("browseBtn"),
    ioNote: document.getElementById("ioNote"),
    storageNote: document.getElementById("storageNote"),
    doneNote: document.getElementById("doneNote"),
    io: document.querySelector(".pn-io")
  };

  function current() {
    return puzzles[state.pos];
  }

  function entryOf(id) {
    return L.normaliseEntry(state.entries[id]);
  }

  function counts() {
    return L.countsOf(puzzles, state.entries);
  }

  function refreshCounts() {
    var c = counts();
    el.countDone.textContent = c.done;
    el.countTotal.textContent = total;
    el.countNamed.textContent = c.named;
    el.countNameless.textContent = c.nameless;
    el.countLeft.textContent = c.left;
    el.fill.style.width = (total ? (c.done / total) * 100 : 0) + "%";

    // Finished is not the same as delivered. The answers are safe in this
    // browser either way, but the FILE is the thing the device side needs and
    // nothing else in the page ever mentions it: a counter reading 0 left is
    // not an instruction. So the last answer opens the export and says so.
    var done = c.left === 0;
    el.doneNote.hidden = !done;
    if (done && !el.io.open) el.io.open = true;
  }

  function showVerdict(name) {
    var v = judge(name);
    el.verdict.textContent = v.text;
    el.verdict.className = "pn-verdict" + (v.level === "ok" ? "" : v.level === "warn" ? " is-warn" : " is-stop");
    if (v.fold) {
      var btn = document.createElement("button");
      btn.type = "button";
      btn.className = "pn-fold";
      btn.textContent = "Use " + v.fold;
      btn.addEventListener("click", function () {
        setField(v.fold);
        el.field.focus();
      });
      el.verdict.appendChild(btn);
    }
    // What the panel would really put there. Two things the browser would
    // otherwise get wrong and both of them are the reason this box exists: a
    // codepoint the cut has no glyph for draws as NOTHING on the device (the
    // browser has the glyph and would happily show it, hiding the hole), and a
    // name too wide for the display cut is set in a smaller one.
    var drawable = "";
    for (var k = 0; k < name.length; k++) {
      var cp = name.charCodeAt(k);
      if (cp >= 0x20 && cp <= 0x7e) drawable += name[k];
    }
    el.preview.textContent = drawable;
    // The size is the CUT the ladder picked; the CSS turns it into pixels
    // against the box, which stands in for the 448px band.
    var rung = typeof v.rung === "number" ? v.rung : 0;
    el.preview.className = "pn-preview-name" + (rung === 1 ? " is-body" : rung === 2 ? " is-small" : "");
    return v;
  }

  // The place that writes the field FROM A CONTROL -- the fold button, Escape.
  // Not the only writer: show() sets it when the puzzle changes and the input
  // handler rewrites it as he types, and saying "one place" here was wrong in a
  // way that mattered, because show() was exactly the writer that left the
  // draft disagreeing with the box after a save-file load. That is fixed in
  // logic.js's mergeState; this comment no longer claims a rule the file does
  // not keep.
  function setField(value) {
    el.field.value = value;
    showVerdict(value);
    state.draft = value;
    save(true);
  }

  function show(i, keepDraft) {
    state.pos = Math.max(0, Math.min(total - 1, i));
    var p = current();
    paint(el.picture, p);
    el.picture.setAttribute("aria-label", "Solved picture for puzzle " + p.id);
    el.posNow.textContent = state.pos + 1;
    el.posTotal.textContent = total;
    el.id.textContent = p.id;
    var prov = DATA.provenances[p.prov] || {};
    el.author.textContent = prov.author || "";
    if (prov.source) {
      el.source.href = prov.source;
      el.source.hidden = false;
    } else {
      el.source.hidden = true;
    }
    var e = entryOf(p.id);
    var value = keepDraft && state.draft ? state.draft : e && e.name ? e.name : "";
    el.field.value = value;
    el.field.placeholder = e && e.nameless ? "Marked: no good name" : "What is it?";
    showVerdict(value);
    refreshCounts();
    markCurrentCell();
    save(true);
  }

  function nextUnfinished(from) {
    for (var step = 1; step <= total; step++) {
      var i = (from + step) % total;
      if (!entryOf(puzzles[i].id)) return i;
    }
    return -1;
  }

  // Enter: keep the answer, then move. The move goes to the next puzzle with no
  // answer yet -- not simply the next one -- so a second pass over a list with
  // gaps does not walk him back through everything he has already done.
  function commit(name) {
    var p = current();
    var clean = name.trim().replace(/\s+/g, " ");
    if (!clean) return false;
    var v = judge(clean);
    if (v.level === "stop") {
      el.field.focus();
      return false;
    }
    state.entries[p.id] = { name: clean, at: new Date().toISOString() };
    state.draft = "";
    save();
    var next = nextUnfinished(state.pos);
    show(next === -1 ? state.pos : next);
    el.field.focus();
    buildGrid();
    return true;
  }

  // One tap, irreversible, and sitting next to Skip. Recovering from a mis-tap
  // without this means opening Browse, switching the filter to "no name",
  // finding the thumbnail and tapping it -- so the tap offers to undo itself.
  function markNameless() {
    var p = current();
    var was = state.entries[p.id];
    state.entries[p.id] = { nameless: true, at: new Date().toISOString() };
    state.draft = "";
    save();
    var next = nextUnfinished(state.pos);
    show(next === -1 ? state.pos : next);
    el.field.focus();
    buildGrid();
    offerUndo(p, was);
  }

  function offerUndo(puzzle, previous) {
    el.verdict.textContent = "Marked " + puzzle.id + " as having no good name. ";
    el.verdict.className = "pn-verdict is-warn";
    var btn = document.createElement("button");
    btn.type = "button";
    btn.className = "pn-undo";
    btn.textContent = "Undo";
    btn.addEventListener("pointerdown", function (ev) {
      ev.preventDefault();
    });
    btn.addEventListener("click", function () {
      if (previous === undefined) delete state.entries[puzzle.id];
      else state.entries[puzzle.id] = previous;
      save();
      show(indexById[puzzle.id]);
      buildGrid();
      el.field.focus();
    });
    el.verdict.appendChild(btn);
  }

  // Skip writes nothing at all: it is "not now", and the export must not
  // confuse it with "this picture has no name".
  function skip() {
    state.draft = "";
    var next = nextUnfinished(state.pos);
    show(next === -1 ? (state.pos + 1) % total : next);
    el.field.focus();
  }

  function prev() {
    state.draft = "";
    show((state.pos - 1 + total) % total);
    el.field.focus();
  }

  // --- browse --------------------------------------------------------------

  var filter = "all";

  function passes(p) {
    var e = entryOf(p.id);
    if (filter === "all") return true;
    if (filter === "left") return !e;
    if (filter === "named") return !!(e && e.name);
    if (filter === "nameless") return !!(e && e.nameless);
    return true;
  }

  function buildGrid() {
    if (el.grid.hidden) return;
    var frag = document.createDocumentFragment();
    puzzles.forEach(function (p, i) {
      if (!passes(p)) return;
      var e = entryOf(p.id);
      var cell = document.createElement("button");
      cell.type = "button";
      cell.className =
        "pn-cell" + (e && e.name ? " is-named" : "") + (e && e.nameless ? " is-nameless" : "");
      cell.dataset.index = String(i);
      var img = document.createElement("img");
      img.src = thumb(p);
      img.alt = "";
      img.width = DATA.size;
      img.height = DATA.size;
      cell.appendChild(img);
      var label = document.createElement("span");
      label.className = "pn-cell-label";
      label.textContent = e && e.name ? e.name : e && e.nameless ? "no name" : p.id;
      cell.appendChild(label);
      frag.appendChild(cell);
    });
    el.gridCells.replaceChildren(frag);
    markCurrentCell();
  }

  function markCurrentCell() {
    var cells = el.gridCells.children;
    for (var i = 0; i < cells.length; i++) {
      cells[i].classList.toggle("is-current", cells[i].dataset.index === String(state.pos));
    }
  }

  // --- export --------------------------------------------------------------

  function namesJson() {
    return L.namesJson(puzzles, state.entries);
  }

  function download(filename, text, type) {
    var blob = new Blob([text], { type: type });
    var url = URL.createObjectURL(blob);
    var a = document.createElement("a");
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    window.setTimeout(function () {
      URL.revokeObjectURL(url);
    }, 1000);
  }

  function note(text) {
    el.ioNote.textContent = text;
  }

  function merge(incoming) {
    // The whole state comes back, not just the entries: clearing the draft is
    // part of merging (see logic.js), and as a line in this function it was
    // untestable and it was missing.
    var result = L.mergeState(state, incoming.entries, indexById, DATA);
    state = result.state;
    save();
    show(state.pos);
    buildGrid();
    var said = "Merged: " + result.added + " new, " + result.updated + " replaced by a newer answer.";
    // Loudly, because a refused name is an answer of his that did not arrive.
    if (result.refused.length) {
      said += " " + result.refused.length + " refused as unusable: " + result.refused.join(", ") + ".";
    }
    note(said);
  }

  // --- wiring --------------------------------------------------------------

  el.form.addEventListener("submit", function (ev) {
    ev.preventDefault();
    commit(el.field.value);
  });

  // Enter, handled by name as well. A form with one text field submits on Enter
  // by itself in a browser -- and it is the single most-repeated keystroke in
  // this tool, 137 times, so it does not get to depend on a default: a keydown
  // that arrives without going through implicit submission (an automated key,
  // some IMEs, some mobile keyboards' Go) would silently do nothing at all, and
  // "Enter does nothing" is the one failure that would make the tool useless.
  el.field.addEventListener("keydown", function (ev) {
    if (ev.key !== "Enter" || ev.isComposing) return;
    ev.preventDefault();
    commit(el.field.value);
  });

  el.field.addEventListener("input", function () {
    // Uppercase because every Toybox label is, and because the measurement has
    // to be of the string that will really be stored: caps are wider, so
    // measuring what he typed and storing what the device gets would report a
    // fit that is not there.
    var up = L.foldWith(L.PUNCT_FOLD, el.field.value).toUpperCase().slice(0, L.MAX_CHARS);
    if (up !== el.field.value) {
      var at = el.field.selectionStart;
      el.field.value = up;
      try {
        el.field.setSelectionRange(at, at);
      } catch (e) {
        /* some mobile keyboards refuse this mid-composition */
      }
    }
    showVerdict(el.field.value);
    // The half-typed word survives a closed tab too.
    state.draft = el.field.value;
    save(true);
  });

  // Tapping a <button> blurs the field, which on a phone dismisses the keyboard
  // and then raises it again when the handler re-focuses -- a full animation
  // and a visualViewport reflow of the picture underneath, on every skip, 137
  // times. Refusing the default on pointerdown keeps focus where it is and the
  // keyboard never moves.
  function actionButton(id, fn) {
    var btn = document.getElementById(id);
    btn.addEventListener("pointerdown", function (ev) {
      ev.preventDefault();
    });
    btn.addEventListener("click", fn);
  }

  actionButton("prevBtn", prev);
  actionButton("skipBtn", skip);
  actionButton("namelessBtn", markNameless);

  // Escape clears the field, and ONLY while the field has focus: it used to be
  // a document-level handler, so Escape anywhere on the page wiped what he had
  // typed, with no undo.
  //
  // The Alt shortcuts that were here are gone. Option is the dead-key composer
  // on macOS -- Option+N reports key "Dead" and Option+F reports a florin --
  // so neither could ever have fired on the laptop, a phone has no Alt at all,
  // and Option+Arrow also took word-jump away from the text field. Each of
  // them had a button beside it that works everywhere.
  el.field.addEventListener("keydown", function (ev) {
    if (ev.key !== "Escape") return;
    ev.preventDefault();
    setField("");
  });

  el.browseBtn.addEventListener("click", function () {
    var open = el.grid.hidden;
    el.grid.hidden = !open;
    el.browseBtn.setAttribute("aria-expanded", String(open));
    el.browseBtn.textContent = open ? "Hide the list" : "Browse all " + total;
    if (open) buildGrid();
  });

  el.gridCells.addEventListener("click", function (ev) {
    var cell = ev.target.closest(".pn-cell");
    if (!cell) return;
    state.draft = "";
    show(Number(cell.dataset.index));
    el.field.focus();
    el.field.select();
  });

  document.querySelector(".pn-filters").addEventListener("click", function (ev) {
    var btn = ev.target.closest("button[data-filter]");
    if (!btn) return;
    filter = btn.dataset.filter;
    Array.prototype.forEach.call(this.querySelectorAll("button"), function (b) {
      b.setAttribute("aria-pressed", String(b === btn));
    });
    buildGrid();
  });

  document.getElementById("exportTxt").addEventListener("click", function () {
    download("janko-names.json", namesJson(), "application/json");
    note("janko-names.json downloaded.");
  });

  document.getElementById("exportJson").addEventListener("click", function () {
    download("picross-names-save.json", JSON.stringify(state, null, 2), "application/json");
    note("Save file downloaded. Load it on the other device to merge.");
  });

  document.getElementById("copyTxt").addEventListener("click", function () {
    var text = namesJson();
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(
        function () {
          note("janko-names.json copied to the clipboard.");
        },
        function () {
          note("The browser refused the clipboard. Use the download instead.");
        }
      );
    } else {
      note("This browser has no clipboard API. Use the download instead.");
    }
  });

  document.getElementById("importFile").addEventListener("change", function (ev) {
    var file = ev.target.files && ev.target.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function () {
      try {
        var parsed = JSON.parse(String(reader.result));
        if (!parsed || !parsed.entries) throw new Error("no entries");
        merge(parsed);
      } catch (e) {
        note("That file is not a save file from this tool.");
      }
    };
    reader.readAsText(file);
    ev.target.value = "";
  });

  document.getElementById("resetBtn").addEventListener("click", function () {
    var c = counts();
    if (!window.confirm("Erase all " + c.done + " answers in this browser? Download the save file first if you want them.")) return;
    state = { v: 1, entries: {}, pos: 0, draft: "" };
    save();
    show(0);
    buildGrid();
    note("Erased.");
  });

  // The height the phone is actually showing, which is not innerHeight once the
  // keyboard is up. Published as --vvh so the picture can shrink and leave the
  // field where a thumb can see it; see .pn-picture in names.css.
  function trackViewport() {
    var vv = window.visualViewport;
    var h = vv ? vv.height : window.innerHeight;
    document.documentElement.style.setProperty("--vvh", h / 100 + "px");
  }

  if (window.matchMedia) {
    var dark = window.matchMedia("(prefers-color-scheme: dark)");
    var onTheme = function () {
      thumbCache = {};
      paint(el.picture, current());
      buildGrid();
    };
    if (dark.addEventListener) dark.addEventListener("change", onTheme);
  }

  if (window.visualViewport) {
    window.visualViewport.addEventListener("resize", trackViewport);
    window.visualViewport.addEventListener("scroll", trackViewport);
  }
  window.addEventListener("resize", trackViewport);
  window.addEventListener("orientationchange", trackViewport);
  trackViewport();

  // --- boot ----------------------------------------------------------------

  load();
  el.storageNote.textContent =
    "Answers live in this browser's local storage under “" + STORE_KEY + "”, on this device only. " +
    "They survive closing the tab and rebooting; they do NOT follow you to another device or another browser, " +
    "and clearing site data erases them. To move between a phone and a laptop, download the save file and load it on the other one.";
  savedNote.textContent = storageNoteText();
  // Every count on the page comes from the bank, never from a number typed into
  // the markup: the bank is 137 today and the whole point of reading it is that
  // nobody has to remember that when it is not.
  el.countTotal.textContent = total;
  document.getElementById("doneTotal").textContent = total;
  document.getElementById("browseTotal").textContent = total;
  boot.hidden = true;
  el.main.hidden = false;

  // Resume where he was, on the puzzle he was looking at, with the half-typed
  // word still in the field.
  var startAt = L.clampPos(state.pos, total);
  if (!state.draft && entryOf(puzzles[startAt].id)) {
    var open = nextUnfinished(startAt - 1);
    if (open !== -1) startAt = open;
  }
  show(startAt, true);
  el.field.focus();
})();
