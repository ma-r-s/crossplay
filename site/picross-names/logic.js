/* The parts of the naming tool that are not the page.
 *
 * Split out of names.js so a host suite can drive them with no browser:
 * host-tests/picrossnames/. That is not tidiness. A cold review found two
 * confirmed data-loss bugs in here -- a stale draft surviving a save-file merge
 * and overwriting the answer it had just merged in, and an out-of-range saved
 * position that bricked the tool while it still looked alive -- and neither was
 * findable by looking at the page, because the page rendered perfectly in both
 * cases. A test would have caught both, and there was no test to catch them.
 *
 * Everything here is a pure function of its arguments: no DOM, no
 * localStorage, no `window` beyond the one export at the bottom. If a function
 * in here starts needing the page, it belongs in names.js instead.
 */
(function (root) {
  "use strict";

  // The rungs toybox::fittedTitle walks for a name set in kDisplayFont, largest
  // first, under toybox::toyboxFaces().
  var RUNGS = [
    { key: "title", label: "full size" },
    { key: "body", label: "one size down" },
    { key: "small", label: "the smallest cut" }
  ];

  // What the file card #390 consumes will accept. Its generator makes each of
  // these a HARD ERROR on the whole file, so a name that breaks one is not a
  // warning to read later: it is 137 names rejected at the end.
  var ALLOWED = /^[A-Z0-9 -]*$/;
  var MAX_CHARS = 16;

  // Two different folds, because the firmware makes the same distinction and
  // for the same reason (lib/Utf8's utf8FoldTypography, and the typography-fold
  // note): PUNCTUATION has an exact ASCII spelling and folding it changes
  // nothing, while a LETTER does not -- turning an n-tilde into an n changes
  // the word, and that is his call rather than the tool's.
  //
  // Note what the punctuation fold is NOT for any more. It was written to
  // rescue the apostrophe an iPhone makes out of a possessive, and the names
  // file refuses an apostrophe however it is spelled, so that case is now
  // refused rather than folded. What is left is real but narrower: a
  // non-breaking or thin space becomes a space and a dash of any width becomes
  // a hyphen, both of which the file accepts, and both of which a phone
  // keyboard produces.
  var PUNCT_FOLD = {
    "‘": "'", "’": "'", "‚": "'", "‛": "'",
    "“": '"', "”": '"', "„": '"', "«": '"', "»": '"',
    "–": "-", "—": "-", "‒": "-", "―": "-", "−": "-",
    "…": "...", " ": " ", " ": " ", " ": " ", " ": " ",
    "•": "*", "×": "X", "·": ".", "′": "'", "″": '"'
  };

  var LETTER_FOLD = {
    "À": "A", "Á": "A", "Â": "A", "Ã": "A", "Ä": "A",
    "Å": "A", "Æ": "AE", "Ç": "C", "È": "E", "É": "E",
    "Ê": "E", "Ë": "E", "Ì": "I", "Í": "I", "Î": "I",
    "Ï": "I", "Ñ": "N", "Ò": "O", "Ó": "O", "Ô": "O",
    "Õ": "O", "Ö": "O", "Ø": "O", "Ù": "U", "Ú": "U",
    "Û": "U", "Ü": "U", "Ý": "Y", "ß": "SS",
    "Œ": "OE", "Š": "S", "Ž": "Z", "Ÿ": "Y",
    // Combining marks, stripped rather than spelled. A macOS dead key and some
    // mobile keyboards produce a decomposed accent, and without these the tool
    // reported "the device cannot draw" followed by a mark with no letter under
    // it -- a message nobody can act on -- and offered no way out.
    "̀": "", "́": "", "̂": "", "̃": "", "̄": "",
    "̆": "", "̇": "", "̈": "", "̊": "", "̋": "",
    "̌": "", "̧": "", "̨": ""
  };

  function foldWith(table, text) {
    var out = "";
    for (var i = 0; i < text.length; i++) {
      var ch = text[i];
      out += Object.prototype.hasOwnProperty.call(table, ch) ? table[ch] : ch;
    }
    return out;
  }

  // Pixels the given cut would set this string in, and every character it has
  // no glyph for. A character with no glyph draws NOTHING and advances the pen
  // by nothing, so it is reported rather than measured -- otherwise a broken
  // string measures as a comfortable fit.
  //
  // This is EpdFont::getTextBounds restated, not an approximation of it, and
  // the difference is not academic: what the device reports is the width of
  // the INK BOX (maxX - minX in getTextDimensions), which is NOT the sum of
  // the advances. Two things in it that a sum gets wrong, both in the unsafe
  // direction for the last one --
  //
  //   * each advance is rounded to a whole pixel AS IT IS ACCUMULATED
  //     (`lastBaseX += fp4::toPixel(prevAdvanceFP)`), so the fractions never
  //     get to cancel each other out;
  //   * the box runs from the first glyph's left side bearing to the last
  //     glyph's right edge, so the last glyph contributes its BITMAP rather
  //     than its advance.
  //
  // Summing float advances measured sixteen capital As at 488px against the
  // device's 493 -- and 488 is under the 448px... no: under the band it is
  // compared to at the next rung, which is exactly the direction that makes
  // the tool say "fits at full size" for a name the device sets a cut down.
  //
  // Kerning is not replicated and does not need to be: all three toybox cuts
  // ship with no kerning data, and gen_name_tool.py refuses to generate from a
  // cut that has any.
  function measure(text, rung, fonts) {
    var font = fonts[rung];
    var pen = 0;
    // minX and maxX start at ZERO and are only ever pushed outward, exactly as
    // getTextDimensions initialises them before calling getTextBounds. They are
    // not seeded from the first glyph.
    var minX = 0;
    var maxX = 0;
    var prevAdvance = null; // 12.4 fixed point, or null before the first glyph
    var holes = [];
    for (var i = 0; i < text.length; i++) {
      var cp = text.codePointAt(i);
      var char = String.fromCodePoint(cp);
      if (cp > 0xffff) i++; // a surrogate pair is one character
      if (cp < 0x20 || cp > 0x7e) {
        // The CHARACTER, built from the codepoint. Reading text[i] here quoted
        // the low surrogate for an emoji, so the message named a character
        // nobody typed and could not be acted on.
        holes.push(char);
        // The device flushes the pending advance when a glyph is missing and
        // then advances by nothing for it, which is why a hole costs no width.
        if (prevAdvance !== null) pen += Math.round(prevAdvance / 16);
        prevAdvance = 0;
        continue;
      }
      var g = cp - 0x20;
      if (prevAdvance !== null) pen += Math.round(prevAdvance / 16);
      var left = pen + font.left[g];
      var right = left + font.width[g];
      if (left < minX) minX = left;
      if (right > maxX) maxX = right;
      prevAdvance = font.advance[g];
    }
    return { width: maxX - minX, holes: holes };
  }

  // -> {level, text, rung, fold}. level is "ok" | "warn" | "stop".
  //
  // The order is deliberate: what the FONT cannot draw, then what the FILE will
  // not accept, then how wide it ends up. A name that fails more than one is
  // reported by the first, and `fold` is only ever offered when the folded
  // spelling passes the WHOLE judgement -- offering a fold that the next rule
  // then refuses is two stops in a row, the second caused by the tool's own
  // suggestion.
  function judge(name, data) {
    if (!name) return { level: "ok", text: "Type a name and press Enter." };

    var holes = measure(name, "title", data.fonts).holes;
    if (holes.length) {
      var uniq = holes.filter(function (c, i) {
        return holes.indexOf(c) === i;
      });
      var folded = foldWith(LETTER_FOLD, name);
      var canFold = folded !== name && judge(folded, data).level !== "stop";
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
          (canFold ? "" : "Use letters, digits, spaces and hyphens."),
        fold: canFold ? folded : null
      };
    }

    if (!ALLOWED.test(name)) {
      var bad = [];
      for (var b = 0; b < name.length; b++) {
        if (!ALLOWED.test(name[b]) && bad.indexOf(name[b]) === -1) bad.push(name[b]);
      }
      return {
        level: "stop",
        text:
          "The names file takes letters, digits, spaces and hyphens only, so " +
          bad
            .map(function (c) {
              return '"' + c + '"';
            })
            .join(", ") +
          " would be refused for the whole file. Spell it without."
      };
    }

    if (name.length > MAX_CHARS) {
      return {
        level: "stop",
        text: "The names file takes " + MAX_CHARS + " characters at most, and this is " + name.length + "."
      };
    }

    // Only two width answers can actually happen, and saying "three" here was
    // wrong: sixteen characters of the widest glyph the file's charset allows
    // is 252px in toybox_10 against a 448px band, so neither the smallest cut
    // nor the ellipsis is reachable while MAX_CHARS is 16. The loop still walks
    // every rung rather than assuming that, because MAX_CHARS is the thing
    // most likely to move.
    for (var i = 0; i < RUNGS.length; i++) {
      var w = measure(name, RUNGS[i].key, data.fonts).width;
      if (w <= data.bandWidth) {
        if (i === 0) {
          return {
            level: "ok",
            rung: i,
            text:
              "Fits at " + RUNGS[i].label + " (" + w + " of " + data.bandWidth + "px, " +
              name.length + " of " + MAX_CHARS + " characters)."
          };
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

  // An entry is one of: absent (not answered), {name: "..."} , {nameless: true}.
  //
  // A THIRD shape can arrive through a loaded save file -- {name: ""} -- and it
  // used to be a hole nothing could reach: counted as neither named nor
  // nameless so it stayed in "left", truthy so nextUnfinished skipped it
  // forever, and hidden from the "Left to do" filter because that asks whether
  // an entry exists. The counter would stick at 136 of 137 with no way to find
  // the one missing. An empty name IS the no-good-name answer, so it is read as
  // one.
  function normaliseEntry(e) {
    if (!e || typeof e !== "object") return null;
    if (e.nameless) return { nameless: true, at: e.at };
    if (typeof e.name === "string" && e.name !== "") return { name: e.name, at: e.at };
    if (typeof e.name === "string") return { nameless: true, at: e.at };
    return null;
  }

  function countsOf(puzzles, entries) {
    var named = 0;
    var nameless = 0;
    puzzles.forEach(function (p) {
      var e = normaliseEntry(entries[p.id]);
      if (!e) return;
      if (e.nameless) nameless++;
      else named++;
    });
    return {
      named: named,
      nameless: nameless,
      done: named + nameless,
      left: puzzles.length - named - nameless
    };
  }

  // A saved position that is not a real puzzle index. Not paranoia: the bank
  // is read from the header and card #390 shrinks it, so a position saved
  // before that lands is past the end afterwards -- and an unclamped one threw
  // AFTER the page had already revealed itself, leaving a tool that showed
  // "0 of 137", accepted typing, and silently discarded every name because
  // there was no current puzzle to write it to.
  function clampPos(pos, total) {
    if (typeof pos !== "number" || !isFinite(pos)) return 0;
    var i = Math.floor(pos);
    if (i < 0 || i >= total) return 0;
    return i;
  }

  // Merge a loaded save file into what this browser already has.
  //
  // Merging and not replacing, because the file exists to carry work between a
  // phone and a laptop and a load that threw away what was already here would
  // be the same lost-work failure by another route. Newer timestamp wins; an
  // entry only one side has is kept.
  //
  // Everything arriving this way is JUDGED, which is the door the entry checks
  // did not cover: a save file hand-edited or written by an older build could
  // carry a lowercase name, an accent or a forty-character string, and it
  // would have been stored and exported unexamined -- the exact "137 names
  // rejected at the end" failure those checks exist to prevent.
  function mergeEntries(mine, incoming, knownIds, data) {
    var out = {};
    Object.keys(mine).forEach(function (id) {
      out[id] = mine[id];
    });
    var added = 0;
    var updated = 0;
    var refused = [];
    Object.keys(incoming || {}).forEach(function (id) {
      if (!Object.prototype.hasOwnProperty.call(knownIds, id)) return; // not a puzzle we ship
      var theirs = normaliseEntry(incoming[id]);
      if (!theirs) return;
      if (theirs.name && judge(theirs.name, data).level === "stop") {
        refused.push(id);
        return;
      }
      var ours = normaliseEntry(out[id]);
      if (!ours) {
        out[id] = theirs;
        added++;
        return;
      }
      if ((theirs.at || "") > (ours.at || "")) {
        out[id] = theirs;
        updated++;
      }
    });
    return { entries: out, added: added, updated: updated, refused: refused };
  }

  // janko-names.json: the answer, in the shape card #390's generator reads and
  // in the shape janko-authors.json already uses -- a flat object keyed by the
  // janko.at puzzle number as a decimal string.
  //
  // THREE states, and the file keeps all three apart:
  //
  //   absent      not answered yet
  //   ""          answered: this picture has no good name
  //   "CAT"       answered: this is what it is
  //
  // Assembled line by line rather than handed to JSON.stringify, for two
  // reasons that both show up in a diff: an object's integer-like keys are
  // enumerated BEFORE its string keys, so "222" would come out above "_comment"
  // and the file would not read like janko-authors.json; and the keys are kept
  // in bank order rather than in whatever order an engine decides, so two
  // exports of the same answers are the same bytes.
  function namesJson(puzzles, entries) {
    var c = countsOf(puzzles, entries);
    var comment =
      "Picross puzzle names, written by hand at /picross-names/. Keyed by the janko.at " +
      "puzzle number, as janko-authors.json is. An empty string means the picture was " +
      "looked at and has no good name; a puzzle absent from this file has not been " +
      "answered yet. " +
      c.named +
      " named, " +
      c.nameless +
      " deliberately unnamed, " +
      c.left +
      " not answered.";
    var lines = ['  "_comment": ' + JSON.stringify(comment)];
    puzzles.forEach(function (p) {
      var e = normaliseEntry(entries[p.id]);
      if (!e) return;
      lines.push("  " + JSON.stringify(p.janko) + ": " + JSON.stringify(e.name || ""));
    });
    return "{\n" + lines.join(",\n") + "\n}\n";
  }

  // The whole saved state after a save file is loaded into it, rather than just
  // the entries.
  //
  // The draft belongs in here and that is the point. Clearing it was a
  // one-line fix for a confirmed data-loss bug -- the half-typed word from
  // before the load survived, went back to storage, and reappeared in the field
  // on top of the answer the merge had just brought in, where one Enter
  // overwrote that answer with a newer timestamp so merging back from the other
  // device could not recover it either. As a line in the page's merge handler
  // it was untestable and it was missing; here it has a test.
  function mergeState(state, incoming, knownIds, data) {
    var merged = mergeEntries(state.entries, incoming, knownIds, data);
    return {
      state: {
        v: state.v,
        entries: merged.entries,
        pos: clampPos(state.pos, Object.keys(knownIds).length),
        draft: ""
      },
      added: merged.added,
      updated: merged.updated,
      refused: merged.refused
    };
  }

  root.PicrossNamesLogic = {
    RUNGS: RUNGS,
    ALLOWED: ALLOWED,
    MAX_CHARS: MAX_CHARS,
    PUNCT_FOLD: PUNCT_FOLD,
    LETTER_FOLD: LETTER_FOLD,
    foldWith: foldWith,
    measure: measure,
    judge: judge,
    normaliseEntry: normaliseEntry,
    countsOf: countsOf,
    clampPos: clampPos,
    mergeEntries: mergeEntries,
    mergeState: mergeState,
    namesJson: namesJson
  };
})(typeof window !== "undefined" ? window : globalThis);
