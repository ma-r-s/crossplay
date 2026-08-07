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

  // Scancodes, matching HalGPIO's own table. Kept here rather than as button
  // indices so the firmware's remapping stays the only thing that decides what
  // a button does.
  var SCANCODE = {
    back: 41,
    ok: 40,
    right: 79,
    left: 80,
    down: 81,
    up: 82,
  };
  var KEYS = {
    Escape: SCANCODE.back,
    Backspace: SCANCODE.back,
    Enter: SCANCODE.ok,
    NumpadEnter: SCANCODE.ok,
    ArrowRight: SCANCODE.right,
    ArrowLeft: SCANCODE.left,
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
    canvas.setAttribute("aria-label", "The Crossplay firmware running live");
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
    new ResizeObserver(sizeCanvas).observe(canvas);

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
      if (consumeDirty && consumeDirty()) paint();
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

    var pointerId = null;
    canvas.addEventListener("pointerdown", function (e) {
      if (!injectTouch || pointerId !== null || !e.isPrimary) return;
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

    function keyHandler(down) {
      return function (e) {
        var sc = KEYS[e.code] || KEYS[e.key];
        if (!sc || !injectKey) return;
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
            sizeCanvas();
            requestAnimationFrame(frame);
            resolve({
              canvas: canvas,
              bindButton: bindButton,
              scancodes: SCANCODE,
            });
          })
          .catch(reject);
      };
      document.head.appendChild(script);
    });
  }

  window.Crossplay = { boot: boot, scancodes: SCANCODE };
})();
