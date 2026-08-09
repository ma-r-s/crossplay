// The device, running in the page.
//
// site/emulator/crossplay.js is the firmware compiled to WebAssembly. It hands
// the page a framebuffer pointer and four input functions and does nothing
// else: no canvas of its own, no page of its own. This file is the whole front
// end, so the device boots inside whatever box it is given and the screenshot
// it replaces does not move.
//
//   Crossplay.boot(element).then(...)
//
// Everything below is written against one number the firmware reports: the
// rotation it wants its frame drawn at. Solitaire and the comic reader flip the
// panel to landscape mid-session, and letting the box change shape with them
// would shove the page around under the reader -- so the box is fixed and a
// landscape frame is letterboxed inside it, with taps mapped back through the
// same transform.

(function () {
  var PANEL_W = 800;
  var PANEL_H = 480;

  // The radio, standing in for a room.
  //
  // On the device PLAY NEARBY is ESP-NOW; in Mario's simulator it is UDP on
  // loopback. Two instances in one page are two WebAssembly memories, so the
  // only thing that can carry a packet between them is this. Slot numbers match
  // the loopback transport's port range, so "lower address wins" still means
  // "whoever started first" and host election behaves the same in all three.
  var SLOTS = 8;
  // On globalThis rather than on the module: link_browser.cpp reaches this from
  // a MAIN_THREAD_EM_ASM snippet, which runs proxied on the main thread where
  // `Module` does not resolve to the calling instance.
  var radio = (globalThis.crossplayRadio = {
    taken: [],
    seats: {},
    register: function (id, seat) {
      radio.seats[id] = seat;
    },
    claim: function (id) {
      var seat = radio.seats[id];
      if (!seat) return -1;
      for (var i = 0; i < SLOTS; i++) {
        if (!radio.taken[i]) {
          radio.taken[i] = seat;
          return i;
        }
      }
      return -1;
    },
    release: function (slot) {
      radio.taken[slot] = null;
    },
    // to === -1 is a broadcast: everyone but the sender, which is what the
    // loopback transport does by looping over the port range.
    send: function (from, to, bytes) {
      for (var i = 0; i < SLOTS; i++) {
        var peer = radio.taken[i];
        if (!peer || i === from) continue;
        if (to >= 0 && to !== i) continue;
        // Copied into the peer's own heap. `bytes` is a view of the sender's,
        // and the two modules share nothing but this function.
        peer.module.HEAPU8.set(bytes, peer.inbox);
        peer.deliver(from, bytes.length);
      }
    },
  });
  var nextInstanceId = 0;

  // Scancodes, matching HalGPIO's own table. Kept here rather than as button
  // indices so the firmware's remapping stays the only thing that decides what
  // a button does.
  // The X4 Pro has TWO buttons: the side page keys, wired to logical up and
  // down. There is no back, ok, left or right key -- those pins are unassigned
  // in the board profile and are never configured as inputs.
  //
  // The simulator this is compiled from does have all six, so pressing them
  // here worked and taught a control scheme the hardware cannot honour. This
  // demo is the first thing anyone touches, so it should teach the device.
  var SCANCODE = {
    down: 81,
    up: 82,
  };
  var KEYS = {
    ArrowDown: SCANCODE.down,
    ArrowUp: SCANCODE.up,
  };

  function boot(mount, options) {
    options = options || {};
    if (!self.crossOriginIsolated) {
      return Promise.reject(
        new Error(
          "This page is not cross-origin isolated, so the threaded build cannot " +
            "start. It needs COOP and COEP headers; see site/vercel.json.",
        ),
      );
    }

    var canvas = document.createElement("canvas");
    canvas.className = "device-canvas";
    canvas.setAttribute("role", "img");
    canvas.setAttribute("aria-label", "The CrossPlay firmware running live");
    // Focusable, because the arrow keys, Enter and Backspace are the device's
    // buttons and the page needs them back the moment focus leaves. See the
    // guard in keyHandler.
    canvas.tabIndex = 0;
    mount.appendChild(canvas);
    var ctx = canvas.getContext("2d");

    // Native-resolution staging frame. The firmware's dither is a real 1-bit
    // pattern, so it wants drawing at 1:1 and scaling with smoothing on -- that
    // is what averages a 2x2 dither cell into flat grey, the same thing the
    // 220ppi panel does with your eye.
    var off = document.createElement("canvas");
    off.width = PANEL_W;
    off.height = PANEL_H;
    var offCtx = off.getContext("2d");
    var image = offCtx.createImageData(PANEL_W, PANEL_H);
    var rgba = image.data;

    var logicalW = PANEL_H;
    var logicalH = PANEL_W;
    var rotation = 90;
    var Module = null;
    var framePtr = 0;
    var consumeDirty, frameRotation, injectTouch, injectKey;
    var painted = false;

    function sizeCanvas() {
      var rect = canvas.getBoundingClientRect();
      var dpr = window.devicePixelRatio || 1;
      var w = Math.max(1, Math.round(rect.width * dpr));
      var h = Math.max(1, Math.round(rect.height * dpr));
      if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
      }
      ctx.imageSmoothingEnabled = true;
      ctx.imageSmoothingQuality = "high";
    }
    // Resizing a canvas clears it, and this panel only redraws when the
    // firmware says something changed -- which on e-ink can be minutes away. So
    // every resize repaints from the framebuffer immediately. Without this,
    // going from two devices back to one left a blank white panel: the box grew
    // back, the backing store was cleared, and nothing was due to be drawn.
    new ResizeObserver(function () {
      sizeCanvas();
      if (Module && visible && painted) paint();
    }).observe(canvas);

    // How the logical screen sits inside the canvas: uniform scale, centred.
    // Portrait fills it; landscape letterboxes. Taps invert this.
    function fit() {
      var scale = Math.min(canvas.width / logicalW, canvas.height / logicalH);
      return {
        scale: scale,
        x: (canvas.width - logicalW * scale) / 2,
        y: (canvas.height - logicalH * scale) / 2,
      };
    }

    function paint() {
      // HEAPU32 is replaced whenever wasm memory grows, so re-read it a frame.
      var heap = Module.HEAPU32;
      var base = framePtr >> 2;
      for (var i = 0, n = PANEL_W * PANEL_H; i < n; i++) {
        // ARGB in the firmware's buffer, RGBA in ImageData, and the frame is
        // opaque, so only the channel order has to change.
        var argb = heap[base + i];
        var o = i << 2;
        rgba[o] = (argb >> 16) & 0xff;
        rgba[o + 1] = (argb >> 8) & 0xff;
        rgba[o + 2] = argb & 0xff;
        rgba[o + 3] = 0xff;
      }
      offCtx.putImageData(image, 0, 0);

      var deg = frameRotation();
      if (deg !== rotation) {
        rotation = deg;
        var portrait = deg === 90 || deg === 270;
        logicalW = portrait ? PANEL_H : PANEL_W;
        logicalH = portrait ? PANEL_W : PANEL_H;
      }
      sizeCanvas();
      var box = fit();

      ctx.setTransform(1, 0, 0, 1, 0, 0);
      ctx.fillStyle = "#faf9f6";
      ctx.fillRect(0, 0, canvas.width, canvas.height);
      ctx.save();
      // Rotate about the centre of the drawn area and lay the landscape frame
      // across it, which is what HalDisplay asks SDL to do on the desktop.
      ctx.translate(
        box.x + (logicalW * box.scale) / 2,
        box.y + (logicalH * box.scale) / 2,
      );
      ctx.rotate((rotation * Math.PI) / 180);
      ctx.scale(box.scale, box.scale);
      ctx.drawImage(off, -PANEL_W / 2, -PANEL_H / 2);
      ctx.restore();

      if (!painted) {
        painted = true;
        if (options.onFirstFrame) options.onFirstFrame();
      }
    }

    function frame() {
      if (visible && consumeDirty && consumeDirty()) paint();
      requestAnimationFrame(frame);
    }

    // ---- input -------------------------------------------------------------
    // Coordinates go in as logical screen pixels: the space GfxRenderer draws
    // in, so the firmware never learns there was a canvas.
    function toPanel(event) {
      var rect = canvas.getBoundingClientRect();
      var box = fit();
      var cx = ((event.clientX - rect.left) * canvas.width) / rect.width;
      var cy = ((event.clientY - rect.top) * canvas.height) / rect.height;
      return [
        Math.round((cx - box.x) / box.scale),
        Math.round((cy - box.y) / box.scale),
      ];
    }

    // Paused instances keep running the firmware but stop being drawn, which is
    // the whole cost of a second device the page is not showing.
    var visible = true;

    var pointerId = null;
    canvas.addEventListener("pointerdown", function (e) {
      if (!injectTouch || pointerId !== null || !e.isPrimary) return;
      // preventDefault below suppresses the focus a click would normally give a
      // tabindex element, and without focus the key guard would swallow every
      // press. Touching the device is how you say "the keys are mine now".
      canvas.focus({ preventScroll: true });
      e.preventDefault();
      pointerId = e.pointerId;
      canvas.setPointerCapture(e.pointerId);
      var p = toPanel(e);
      injectTouch(0, p[0], p[1]);
    });
    canvas.addEventListener("pointermove", function (e) {
      if (pointerId !== e.pointerId) return;
      e.preventDefault();
      var p = toPanel(e);
      injectTouch(1, p[0], p[1]);
    });
    function lift(e) {
      // Guarded on the id that went down. pointerup and pointercancel can both
      // arrive for one gesture, and a second release is not harmless: the
      // firmware latches input edges per frame, so a stray one lands on the
      // activity the first one just opened. That is how a single tap on BACK
      // used to walk back two screens.
      if (pointerId !== e.pointerId) return;
      pointerId = null;
      e.preventDefault();
      var p = toPanel(e);
      injectTouch(2, p[0], p[1]);
    }
    canvas.addEventListener("pointerup", lift);
    canvas.addEventListener("pointercancel", lift);

    // Only this device's keys, and only while this device has focus. Bound to
    // window rather than the canvas so a key held across a focus change still
    // gets its release, but gated on focus so the page keeps its own keyboard:
    // these handlers used to preventDefault() Enter, Backspace and all four
    // arrows for every element on the page, which killed link activation and
    // arrow-key scrolling for the rest of the session once the device booted.
    function hasFocus() {
      var a = document.activeElement;
      // `a !== mount` matters: mount is the .device-screen button and
      // Node.contains() returns true for the node itself, so focusing the
      // button counted as focusing the device and swallowed the arrows again.
      // The canvas and the on-screen keys are the only things that drive it.
      return a === canvas || (a !== mount && mount.contains(a));
    }

    function keyHandler(down) {
      return function (e) {
        var sc = KEYS[e.code] || KEYS[e.key];
        if (!sc || !injectKey) return;
        if (!hasFocus()) return;
        e.preventDefault();
        if (down && e.repeat) return; // HalGPIO is edge-triggered
        injectKey(sc, down ? 1 : 0);
      };
    }
    var onKeyDown = keyHandler(true);
    var onKeyUp = keyHandler(false);
    window.addEventListener("keydown", onKeyDown);
    window.addEventListener("keyup", onKeyUp);

    // The on-screen buttons carry the same guard as touch: a press is released
    // exactly once, whatever mixture of up, leave and cancel the browser sends.
    function bindButton(el, scancode) {
      var held = false;
      var press = function (e) {
        if (held || !injectKey) return;
        e.preventDefault();
        held = true;
        el.setPointerCapture && el.setPointerCapture(e.pointerId);
        injectKey(scancode, 1);
      };
      var release = function (e) {
        if (!held || !injectKey) return;
        e.preventDefault();
        held = false;
        injectKey(scancode, 0);
      };
      el.addEventListener("pointerdown", press);
      el.addEventListener("pointerup", release);
      el.addEventListener("pointercancel", release);
    }

    // Back is a SWIPE on this device, not a key: MappedInputManager::wasReleased
    // folds a left-edge left-to-right drag into Button::Back, which is the only
    // reason every app is exitable with the back pin unassigned. So the demo's
    // Back control performs that swipe rather than pressing a button that does
    // not exist -- what it does here is exactly what a thumb does there.
    function swipeBack() {
      if (!injectTouch) return;
      // LOGICAL portrait coordinates, which is what toPanel hands the firmware:
      // logicalW is the panel's short side. Using PANEL_W here would swipe off
      // the side of a 480-wide screen.
      var y = Math.round(logicalH / 2);
      var steps = 8;
      // The gesture wants a start inside the left quarter
      // (LEFT_EDGE_BACK_GESTURE_FRAC_X = 0.25) and more horizontal travel than
      // vertical. 5% to 55% is comfortably both.
      var from = Math.round(logicalW * 0.05);
      var to = Math.round(logicalW * 0.55);
      injectTouch(0, from, y);
      for (var i = 1; i <= steps; i++) {
        injectTouch(1, Math.round(from + ((to - from) * i) / steps), y);
      }
      injectTouch(2, to, y);
    }

    return new Promise(function (resolve, reject) {
      var script = document.createElement("script");
      script.src = "emulator/crossplay.js";
      script.onerror = function () {
        reject(new Error("Could not load emulator/crossplay.js"));
      };
      script.onload = function () {
        createCrossplay({
          // Emscripten resolves crossplay.wasm and crossplay.data against the
          // page, not against the script that loaded it, so booting the device
          // from index.html would look for them at the site root.
          locateFile: function (path) {
            return "emulator/" + path;
          },
          // Both instances mount the same preloaded card, so both would name
          // themselves the same thing and PLAY NEARBY would show you two
          // identical faces. Overwriting one file is enough, and it keeps the
          // card itself single-copy.
          //
          // postRun, not preRun: --preload-file mounts the card in a preRun of
          // its own, and creating /fs_ before it gets there makes that mount
          // fail with an ErrnoError nothing catches. postRun lands after the
          // card is up and after main() has started the firmware worker, which
          // is still long before anything asks who this device is -- the name
          // is read lazily, first by the shelf footer.
          postRun: options.name
            ? [
                function (mod) {
                  try {
                    mod.FS.writeFile(
                      "/fs_/.crosspoint/player.cfg",
                      new TextEncoder().encode(options.name + "\n"),
                    );
                  } catch (e) {
                    console.warn(
                      "[device] could not name it:",
                      (e && (e.message || e.errno)) || e,
                    );
                  }
                },
              ]
            : [],
          print: function (t) {
            console.log("[device]", t);
          },
          printErr: function (t) {
            console.warn("[device]", t);
          },
        })
          .then(function (mod) {
            Module = mod;
            framePtr = mod.cwrap("crossplay_frame_ptr", "number", [])();
            consumeDirty = mod.cwrap("crossplay_consume_dirty", "number", []);
            frameRotation = mod.cwrap("crossplay_frame_rotation", "number", []);
            injectTouch = mod.cwrap("crossplay_touch", null, [
              "number",
              "number",
              "number",
            ]);
            injectKey = mod.cwrap("crossplay_key", null, ["number", "number"]);

            // Hand this instance its end of the radio. link_browser.cpp finds
            // the router on globalThis and identifies itself by this id.
            var id = nextInstanceId++;
            radio.register(id, {
              module: mod,
              inbox: mod.cwrap("crossplay_link_inbox", "number", [])(),
              deliver: mod.cwrap("crossplay_link_deliver", null, [
                "number",
                "number",
              ]),
            });
            mod.cwrap("crossplay_link_bind", null, ["number"])(id);

            sizeCanvas();
            requestAnimationFrame(frame);
            resolve({
              canvas: canvas,
              bindButton: bindButton,
              swipeBack: swipeBack,
              scancodes: SCANCODE,
              // Hiding a device stops it being drawn; the firmware behind it
              // keeps running, because there is no clean way to tear a wasm
              // module down. Showing it again repaints from the framebuffer
              // rather than waiting for the next dirty frame -- an e-ink screen
              // can sit unchanged for minutes, and a blank panel until then
              // would look like a crash.
              setVisible: function (on) {
                visible = on;
                if (on) paint();
              },
            });
          })
          .catch(reject);
      };
      document.head.appendChild(script);
    });
  }

  window.Crossplay = { boot: boot, scancodes: SCANCODE };
})();
