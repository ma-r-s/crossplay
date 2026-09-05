/* The Install button.
 *
 * Someone on Reddit could not flash the device and said, reasonably, that they
 * were expecting a tutorial. The answer is not a longer tutorial. Chrome and
 * Edge can talk to a USB serial port from a page, so the browser can do the
 * whole thing: pick the release, download it, write it, and say what to press
 * afterwards. The esptool instructions stay on the page underneath, for people
 * who would rather type them.
 *
 * The shape is CrossPoint's, whose web flasher has been installing firmware on
 * these devices for far longer than this one has, and whose flasher.js is
 * worth reading before changing anything here. Two deliberate differences:
 *
 *   * It writes the merged image at offset 0 -- bootloader, partition table
 *     and app in one write -- rather than dropping the app into the spare OTA
 *     slot. This fork changed its partition table (7.94MB slots, see
 *     partitions.csv) and ONLY a write at 0 lays down the new table. An OTA
 *     write leaves the device on whatever table it was last flashed with,
 *     which is the 6.25MB one, forever. Every install this button makes is a
 *     fresh, correct one; updates afterwards go over Wi-Fi.
 *   * It is one file with no framework, because the site is one file with no
 *     framework.
 *
 * The device is not on the desk of whoever reads this next, so: the download,
 * validation and error paths are exercised by site/serve.py locally; the write
 * path is not testable without hardware and every change to it wants a real
 * flash before it ships.
 */
(function () {
  "use strict";

  var REPO = "ma-r-s/crossplay";

  // Both boards are ESP32-S3. The check is not X4-Pro-vs-Sticky (nothing on
  // the wire tells those apart); it is "this is not one of the ESP32-C3
  // Xteinks", which is a real mistake someone will make, and one that used to
  // cost them a bricked device. CrossPoint upstream is the right answer there
  // and the error says so.
  var EXPECTED_CHIP = "ESP32-S3";

  var DEVICES = {
    x4pro: {
      name: "Xteink X4 Pro",
      // Written where the ROM looks for the second-stage bootloader on an S3.
      restart:
        "Unplug the USB cable and plug it back in, then hold the power button " +
        "until the screen changes.",
    },
    sticky: {
      name: "Seeed reTerminal Sticky",
      restart: "Unplug the USB cable and plug it back in.",
    },
  };

  // The three magic numbers the release workflow checks on the merged image
  // before publishing it. Checking them again here is not paranoia about
  // GitHub: it is the difference between refusing a truncated download and
  // writing one over a working bootloader.
  var OFF_PARTITIONS = 0x8000;
  var OFF_APP = 0x10000;

  var els = {};
  var state = { tag: null, device: "x4pro", busy: false };
  var log = [];

  function $(id) {
    return document.getElementById(id);
  }

  function say(text, kind) {
    els.status.textContent = text || "";
    els.status.dataset.kind = kind || "";
  }

  function note(line) {
    log.push(line);
    els.log.textContent = log.join("\n");
    els.logBox.hidden = false;
  }

  function progress(fraction) {
    if (fraction === null) {
      els.bar.hidden = true;
      return;
    }
    els.bar.hidden = false;
    els.barFill.style.width =
      Math.round(Math.max(0, Math.min(1, fraction)) * 100) + "%";
  }

  function setBusy(busy) {
    state.busy = busy;
    els.button.setAttribute("aria-disabled", busy ? "true" : "false");
    els.button.disabled = busy;
    var radios = els.panel.querySelectorAll('input[name="install-device"]');
    for (var i = 0; i < radios.length; i++) radios[i].disabled = busy;
  }

  function labelButton() {
    var device = DEVICES[state.device];
    if (!state.tag) {
      els.button.textContent = "Install on " + device.name;
      return;
    }
    els.button.textContent = "Install " + state.tag + " on " + device.name;
  }

  /* --- the release ------------------------------------------------------- */

  // Asked from the visitor's browser rather than from /api/firmware, which is
  // the whole reason that endpoint takes a tag instead of looking one up: the
  // GitHub API's unauthenticated rate limit is per IP, and one shared server
  // address would run out of them long before any one visitor did.
  function loadRelease() {
    return fetch("https://api.github.com/repos/" + REPO + "/releases/latest")
      .then(function (r) {
        return r.ok ? r.json() : null;
      })
      .then(function (data) {
        if (!data || !data.tag_name) return;
        state.tag = data.tag_name;
        els.stamp.textContent = "Latest release: " + data.tag_name;
        els.stamp.hidden = false;
        labelButton();
      })
      .catch(function () {
        // A failed fetch leaves no broken control behind: the button still
        // works, it just cannot name the version until it downloads it.
      });
  }

  /* --- the download ------------------------------------------------------ */

  function download(device, tag) {
    var url =
      "/api/firmware?device=" +
      encodeURIComponent(device) +
      "&tag=" +
      encodeURIComponent(tag);
    return fetch(url).then(function (res) {
      if (!res.ok) {
        return res
          .json()
          .catch(function () {
            return {};
          })
          .then(function (body) {
            throw new Error(
              body.error || "The download failed with HTTP " + res.status + ".",
            );
          });
      }
      // X-Firmware-Size is the DECODED size, straight off GitHub's
      // content-length, and that is deliberate. Reading content-length off our
      // own response instead would be measuring the compressed body against
      // decoded bytes -- the same arithmetic that once had the Study page
      // announcing it was 154% downloaded.
      var expected = Number(res.headers.get("X-Firmware-Size") || 0);
      var name = res.headers.get("X-Firmware-Name") || "firmware";
      var reader = res.body.getReader();
      var chunks = [];
      var received = 0;

      function pump() {
        return reader.read().then(function (step) {
          if (step.done) {
            var out = new Uint8Array(received);
            var at = 0;
            for (var i = 0; i < chunks.length; i++) {
              out.set(chunks[i], at);
              at += chunks[i].length;
            }
            if (expected && out.length !== expected) {
              // A stream that ends early arrives as a perfectly ordinary
              // "done", with no error anywhere. Without this the next step
              // writes a half image over the bootloader.
              throw new Error(
                "The download stopped early (" +
                  out.length +
                  " of " +
                  expected +
                  " bytes). Check your connection and try again.",
              );
            }
            return { data: out, name: name };
          }
          chunks.push(step.value);
          received += step.value.length;
          if (expected) progress(received / expected);
          say(
            "Downloading " +
              name +
              " -- " +
              Math.round(received / 1024) +
              " KB",
            "busy",
          );
          return pump();
        });
      }
      return pump();
    });
  }

  function checkImage(bytes) {
    if (bytes.length < OFF_APP + 1024) {
      throw new Error("That file is too small to be a CrossPlay image.");
    }
    if (bytes[0] !== 0xe9) {
      throw new Error(
        "The downloaded image has no bootloader at offset 0. Nothing was written.",
      );
    }
    if (bytes[OFF_PARTITIONS] !== 0xaa || bytes[OFF_PARTITIONS + 1] !== 0x50) {
      throw new Error(
        "The downloaded image has no partition table at 0x8000. Nothing was written.",
      );
    }
    if (bytes[OFF_APP] !== 0xe9) {
      throw new Error(
        "The downloaded image has no app at 0x10000. Nothing was written.",
      );
    }
  }

  /* --- the flash --------------------------------------------------------- */

  function friendly(err) {
    var message = (err && err.message) || String(err);
    // Chrome throws NotFoundError when the port picker is dismissed. It reads
    // like a failure and is not one.
    if (err && err.name === "NotFoundError") {
      return "No device chosen. Press Install again, then pick your device from the list the browser shows.";
    }
    if (/Failed to open serial port|NetworkError|already open/i.test(message)) {
      return (
        "That port would not open. Close anything else talking to the device " +
        "(a serial monitor, the Arduino IDE), unplug the cable, plug it back " +
        "in, and try again."
      );
    }
    if (/Timed out waiting for packet|Failed to connect/i.test(message)) {
      return (
        "The device did not answer. Wake it with a button press, make sure the " +
        "cable carries data and not only power, and try again. If the browser " +
        "never lists your device at all, it may be one of the units that ship " +
        "with USB flashing locked; crosspointreader.com/#unlock-tool opens those."
      );
    }
    return message;
  }

  function flash(port, bytes) {
    var loader = null;
    var terminal = {
      clean: function () {},
      writeLine: function (line) {
        note(line);
      },
      write: function () {},
    };

    // Root-absolute, not "./". A dynamic import() in a classic script resolves
    // against the active script in Chrome today, but the same relative path in
    // assets/emulator.js broke the /study/ page once, and the fix there was
    // this one.
    return import("/assets/esptool.bundle.js")
      .then(function (mod) {
        say("Connecting to the device", "busy");
        progress(null);
        var transport = new mod.Transport(port, false);
        loader = new mod.ESPLoader({
          transport: transport,
          baudrate: 921600,
          romBaudrate: 115200,
          enableTracing: false,
          terminal: terminal,
        });
        return loader.main();
      })
      .then(function () {
        var chip = loader.chip && loader.chip.CHIP_NAME;
        if (chip && chip !== EXPECTED_CHIP) {
          // Released without a reset pulse: nothing has been written, and the
          // device should be left exactly as it was found.
          return loader
            .after("no_reset_stub")
            .catch(function () {})
            .then(function () {
              return loader.transport.disconnect().catch(function () {});
            })
            .then(function () {
              throw new Error(
                "This device is an " +
                  chip +
                  ", and CrossPlay is built for the " +
                  EXPECTED_CHIP +
                  ". The X3 and the original X4 are ESP32-C3 -- " +
                  "CrossPoint upstream is the right firmware for those. Nothing was written.",
              );
            });
        }
        say("Writing firmware -- do not unplug", "busy");
        progress(0);
        return loader.writeFlash({
          fileArray: [{ data: loader.ui8ToBstr(bytes), address: 0 }],
          // The image was merged with -fm/-fs/-ff keep, so the header already
          // carries the mode, size and frequency this build was configured
          // with. Passing anything else here would rewrite them with a second
          // opinion.
          flashSize: "keep",
          flashMode: "keep",
          flashFreq: "keep",
          eraseAll: false,
          compress: true,
          reportProgress: function (_index, written, total) {
            if (total) progress(written / total);
          },
        });
      })
      .then(function () {
        // No serial reset. The X4 Pro cannot be rebooted over the wire, so the
        // honest end of this flow is to leave the chip in the flasher stub and
        // tell the human what to press -- rather than send a reset that does
        // nothing and then claim the device restarted.
        return loader
          .after("no_reset_stub")
          .catch(function () {})
          .then(function () {
            return loader.transport.disconnect().catch(function () {});
          });
      })
      .catch(function (err) {
        if (loader && loader.transport) {
          try {
            loader.transport.disconnect().catch(function () {});
          } catch (e) {
            /* the port is already gone; nothing to release */
          }
        }
        throw err;
      });
  }

  /* --- the button -------------------------------------------------------- */

  function install() {
    if (state.busy) return;
    var device = state.device;
    var meta = DEVICES[device];

    // The port picker has to be opened from inside the click, before the first
    // await, or Chrome refuses it as a gesture-less request.
    var portPromise;
    try {
      // No filters. A device's USB ids depend on which firmware is currently
      // on it -- stock Xteink, stock Seeed and CrossPlay do not agree -- and a
      // filter that hides the one device someone owns is worse than a list
      // with three entries in it.
      portPromise = navigator.serial.requestPort();
    } catch (err) {
      say(friendly(err), "bad");
      return;
    }

    setBusy(true);
    log = [];
    els.logBox.hidden = true;
    els.done.hidden = true;
    say("Waiting for you to pick the device", "busy");

    var port = null;
    portPromise
      .then(function (picked) {
        port = picked;
        if (state.tag) return state.tag;
        // The version fetch failed earlier. Try once more rather than give up:
        // without a tag there is no URL to ask for.
        return loadRelease().then(function () {
          if (!state.tag) {
            throw new Error(
              "Could not reach GitHub to find the latest release. Check your " +
                "connection, or flash by hand with the steps below.",
            );
          }
          return state.tag;
        });
      })
      .then(function (tag) {
        say("Downloading CrossPlay " + tag, "busy");
        return download(device, tag);
      })
      .then(function (file) {
        checkImage(file.data);
        note("downloaded " + file.name + " (" + file.data.length + " bytes)");
        return flash(port, file.data);
      })
      .then(function () {
        progress(1);
        say(
          "Done. CrossPlay " + (state.tag || "") + " is on the device.",
          "good",
        );
        els.doneText.textContent = meta.restart;
        els.done.hidden = false;
        setBusy(false);
        tellBoard("info", null);
      })
      .catch(function (err) {
        progress(null);
        say(friendly(err), "bad");
        note("error: " + ((err && err.message) || String(err)));
        setBusy(false);
        tellBoard(userSide(err) ? "info" : "error", (err && err.message) || String(err));
      });
  }

  /* --- the board --------------------------------------------------------- */

  // One site/install event per attempt, so the Numbers page can say how many
  // installs the button did and how many failed on what. A failed install
  // posts an error, which opens a card by the board's own rule, unless the
  // failure is the person's own: a port picker they closed, a device that
  // would not answer on the cable. Those are info with the message, counted
  // and not carded (cards 153 and 157 were two of them). The message is the
  // flasher's, with nothing about the person. Best effort throughout: no
  // board, no key, no network, and the install itself is unaffected.
  function userSide(err) {
    var name = (err && err.name) || "";
    var msg = String((err && err.message) || err || "");
    return (
      name === "NotFoundError" || // "No port selected by the user"
      name === "NotAllowedError" || // the browser's permission prompt, declined
      name === "SecurityError" || // not a secure context, or a policy
      name === "AbortError" ||
      /No port selected/i.test(msg) ||
      /Failed to connect with the device/i.test(msg) || // esptool-js: no sync on the cable
      /The port is already open|Failed to open serial port/i.test(msg)
    );
  }
  function tellBoard(level, message) {
    if (typeof fetch !== "function") return;
    fetch("/api/board-config")
      .then(function (r) {
        return r.ok ? r.json() : null;
      })
      .then(function (cfg) {
        if (!cfg || !cfg.url || !cfg.anonKey) return;
        var body = {
          service: "site",
          event: "install",
          level: level,
          version: state.tag ? String(state.tag).replace(/^v/, "") : null,
          board: state.device || null,
          props: message ? { message: String(message).slice(0, 300) } : {},
        };
        return fetch(cfg.url + "/rest/v1/events", {
          method: "POST",
          headers: {
            apikey: cfg.anonKey,
            Authorization: "Bearer " + cfg.anonKey,
            "Content-Type": "application/json",
            Prefer: "return=minimal",
          },
          body: JSON.stringify(body),
        });
      })
      .catch(function () {});
  }

  /* --- wiring ------------------------------------------------------------ */

  function init() {
    els.panel = $("installer");
    if (!els.panel) return;
    els.button = $("installButton");
    els.status = $("installStatus");
    els.bar = $("installBar");
    els.barFill = $("installBarFill");
    els.stamp = $("releaseStamp");
    els.unsupported = $("installUnsupported");
    els.log = $("installLog");
    els.logBox = $("installLogBox");
    els.done = $("installDone");
    els.doneText = $("installDoneText");

    var radios = els.panel.querySelectorAll('input[name="install-device"]');
    for (var i = 0; i < radios.length; i++) {
      radios[i].addEventListener("change", function (e) {
        state.device = e.target.value;
        labelButton();
      });
    }

    loadRelease();

    // Web Serial is Chromium-only, and on a phone it is nowhere at all. Say
    // which of those it is: "use Chrome" is unhelpful advice to someone
    // already in Chrome on an iPad.
    if (!("serial" in navigator)) {
      els.panel.dataset.unsupported = "true";
      els.button.setAttribute("aria-disabled", "true");
      els.button.disabled = true;
      var touch = /Android|iPhone|iPad|iPod/i.test(navigator.userAgent);
      els.unsupported.textContent = touch
        ? "Flashing needs a computer. No phone or tablet can open a USB serial port, whichever browser it runs."
        : "This browser cannot talk to USB devices. Open the page in Chrome or Edge, on a computer.";
      els.unsupported.hidden = false;
      return;
    }

    els.button.addEventListener("click", install);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", init);
  } else {
    init();
  }
})();
