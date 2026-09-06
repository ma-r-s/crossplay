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
  var MAX_NAME = 60;
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
        state.pos = typeof parsed.pos === "number" ? parsed.pos : 0;
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

  // The rungs toybox::fittedTitle walks, largest first. Names match data.js.
  var RUNGS = [
    { key: "title", label: "full size" },
    { key: "body", label: "one size down" },
    { key: "small", label: "the smallest cut" }
  ];

  // Two different folds, because the firmware makes the same distinction and
  // for the same reason (lib/Utf8's utf8FoldTypography, and the typography-fold
  // note): PUNCTUATION has an exact ASCII spelling and folding it changes
  // nothing, while a LETTER does not -- turning an n-tilde into an n changes
  // the word, and that is his call rather than the tool's.
  //
  // The punctuation fold is not a nicety, it is the commonest case. An iPhone
  // turns a typed apostrophe into U+2019 by itself, so without this every
  // possessive typed on the sofa would become a name the panel draws with a
  // hole in it.
  var PUNCT_FOLD = {
    "\u2018": "'", "\u2019": "'", "\u201A": "'", "\u201B": "'",
    "\u201C": '"', "\u201D": '"', "\u201E": '"', "\u00AB": '"', "\u00BB": '"',
    "\u2013": "-", "\u2014": "-", "\u2012": "-", "\u2015": "-", "\u2212": "-",
    "\u2026": "...", "\u00A0": " ", "\u2007": " ", "\u2009": " ", "\u202F": " ",
    "\u2022": "*", "\u00D7": "x", "\u00B7": ".", "\u2032": "'", "\u2033": '"'
  };

  var LETTER_FOLD = {
    "\u00C0": "A", "\u00C1": "A", "\u00C2": "A", "\u00C3": "A", "\u00C4": "A",
    "\u00C5": "A", "\u00C6": "AE", "\u00C7": "C", "\u00C8": "E", "\u00C9": "E",
    "\u00CA": "E", "\u00CB": "E", "\u00CC": "I", "\u00CD": "I", "\u00CE": "I",
    "\u00CF": "I", "\u00D1": "N", "\u00D2": "O", "\u00D3": "O", "\u00D4": "O",
    "\u00D5": "O", "\u00D6": "O", "\u00D8": "O", "\u00D9": "U", "\u00DA": "U",
    "\u00DB": "U", "\u00DC": "U", "\u00DD": "Y", "\u00DF": "SS",
    "\u0152": "OE", "\u0160": "S", "\u017D": "Z", "\u0178": "Y"
  };

  function foldWith(table, text) {
    var out = "";
    for (var i = 0; i < text.length; i++) {
      var ch = text[i];
      out += Object.prototype.hasOwnProperty.call(table, ch) ? table[ch] : ch;
    }
    return out;
  }

  // Pixels the given cut would set this string in. A codepoint the cut has no
  // glyph for costs ZERO and draws NOTHING, so it is reported rather than
  // measured -- otherwise a broken string measures as a comfortable fit.
  function measure(text, rung) {
    var adv = DATA.fonts[rung].advance;
    var width = 0;
    var holes = [];
    for (var i = 0; i < text.length; i++) {
      var cp = text.codePointAt(i);
      if (cp > 0xffff) i++; // surrogate pair, one character
      if (cp >= 0x20 && cp <= 0x7e) {
        width += adv[cp - 0x20];
      } else {
        holes.push(text[i]);
      }
    }
    return { width: width, holes: holes };
  }

  // -> {level, text}. level is "ok" | "warn" | "stop".
  function judge(name) {
    if (!name) return { level: "ok", text: "Type a name and press Enter." };

    var holes = measure(name, "title").holes;
    if (holes.length) {
      var uniq = holes.filter(function (c, i) {
        return holes.indexOf(c) === i;
      });
      // Offered, not applied. And offered as a BUTTON: Alt+F is not a key a
      // phone has, and the phone is where he said he would be doing this.
      var folded = foldWith(LETTER_FOLD, name);
      var canFold = measure(folded, "title").holes.length === 0 && folded !== name;
      return {
        level: "stop",
        text:
          "The device cannot draw " +
          uniq
            .map(function (c) {
              return '"' + c + '"';
            })
            .join(", ") +
          ". It would draw a HOLE in the word, not a box. " +
          (canFold ? "" : "Use plain letters, digits and punctuation."),
        fold: canFold ? folded : null
      };
    }

    var band = DATA.bandWidth;
    for (var i = 0; i < RUNGS.length; i++) {
      var w = measure(name, RUNGS[i].key).width;
      if (w <= band) {
        if (i === 0) {
          return { level: "ok", rung: i, text: "Fits at " + RUNGS[i].label + " (" + Math.round(w) + " of " + band + "px)." };
        }
        return {
          level: "warn",
          rung: i,
          text: "Too wide for full size; the device will set it at " + RUNGS[i].label + "."
        };
      }
    }
    return {
      level: "stop",
      text: "Too long even at the smallest cut. The device would cut it and end it in an ellipsis."
    };
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
    return Object.prototype.hasOwnProperty.call(state.entries, id) ? state.entries[id] : null;
  }

  function counts() {
    var named = 0;
    var nameless = 0;
    puzzles.forEach(function (p) {
      var e = entryOf(p.id);
      if (!e) return;
      if (e.nameless) nameless++;
      else if (e.name) named++;
    });
    return { named: named, nameless: nameless, done: named + nameless, left: total - named - nameless };
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

  // One place that writes the field, so the draft, the verdict and the preview
  // can never disagree with what is in the box.
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

  function markNameless() {
    var p = current();
    state.entries[p.id] = { nameless: true, at: new Date().toISOString() };
    state.draft = "";
    save();
    var next = nextUnfinished(state.pos);
    show(next === -1 ? state.pos : next);
    el.field.focus();
    buildGrid();
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

  // names.txt: the answer, in the same shape as the other picross assets. One
  // line per puzzle that has an ANSWER -- an id, a tab, the name -- and an id
  // with an empty name for a picture deliberately left unnamed. A puzzle not
  // reached yet is absent, so "not done" and "no good name" never read alike.
  function namesTxt() {
    var c = counts();
    var lines = [
      "# Picross puzzle names, annotated by hand.",
      "# Generated by /picross-names/ on " + new Date().toISOString(),
      "# One line per answered puzzle: id, a TAB, the name.",
      "# An id with an empty name is a picture deliberately left unnamed.",
      "# A puzzle absent from this file has not been answered yet.",
      "# " + c.named + " named, " + c.nameless + " deliberately unnamed, " + c.left + " not yet answered.",
      ""
    ];
    puzzles.forEach(function (p) {
      var e = entryOf(p.id);
      if (!e) return;
      lines.push(p.id + "\t" + (e.name || ""));
    });
    return lines.join("\n") + "\n";
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

  // Merging, not replacing: the point of the save file is carrying work between
  // a phone and a laptop, and a load that threw away whatever was already in
  // this browser would be the same lost-work failure by another route. Newer
  // timestamp wins; an entry only one side has is kept.
  function merge(incoming) {
    var added = 0;
    var updated = 0;
    Object.keys(incoming.entries || {}).forEach(function (id) {
      if (!Object.prototype.hasOwnProperty.call(indexById, id)) return; // not a puzzle we ship
      var theirs = incoming.entries[id];
      var mine = entryOf(id);
      if (!mine) {
        state.entries[id] = theirs;
        added++;
        return;
      }
      if ((theirs.at || "") > (mine.at || "")) {
        state.entries[id] = theirs;
        updated++;
      }
    });
    save();
    show(state.pos);
    buildGrid();
    note("Merged: " + added + " new, " + updated + " replaced by a newer answer.");
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
    var up = foldWith(PUNCT_FOLD, el.field.value).toUpperCase().slice(0, MAX_NAME);
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

  document.getElementById("prevBtn").addEventListener("click", prev);
  document.getElementById("skipBtn").addEventListener("click", skip);
  document.getElementById("namelessBtn").addEventListener("click", markNameless);

  document.addEventListener("keydown", function (ev) {
    if (ev.altKey && ev.key === "ArrowRight") {
      ev.preventDefault();
      skip();
    } else if (ev.altKey && ev.key === "ArrowLeft") {
      ev.preventDefault();
      prev();
    } else if (ev.altKey && (ev.key === "n" || ev.key === "N")) {
      ev.preventDefault();
      markNameless();
    } else if (ev.altKey && (ev.key === "f" || ev.key === "F")) {
      ev.preventDefault();
      setField(foldWith(LETTER_FOLD, el.field.value).toUpperCase());
    } else if (ev.key === "Escape") {
      setField("");
    }
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
    download("names.txt", namesTxt(), "text/plain;charset=utf-8");
    note("names.txt downloaded.");
  });

  document.getElementById("exportJson").addEventListener("click", function () {
    download("picross-names-save.json", JSON.stringify(state, null, 2), "application/json");
    note("Save file downloaded. Load it on the other device to merge.");
  });

  document.getElementById("copyTxt").addEventListener("click", function () {
    var text = namesTxt();
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(
        function () {
          note("names.txt copied to the clipboard.");
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
  var startAt = state.pos;
  if (!state.draft && entryOf(puzzles[startAt].id)) {
    var open = nextUnfinished(startAt - 1);
    if (open !== -1) startAt = open;
  }
  show(startAt, true);
  el.field.focus();
})();
