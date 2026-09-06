// The naming tool's logic, driven with no browser.
//
// It exists because a cold review found two confirmed data-loss bugs that no
// amount of looking at the page could have found -- the page rendered perfectly
// in both cases -- and there was no test to catch either. Both are pinned here.
"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");

const root = path.resolve(__dirname, "../..");
const sandbox = { window: {}, console };
sandbox.globalThis = sandbox;
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync(path.join(root, "site/picross-names/logic.js"), "utf8"), sandbox, {
  filename: "logic.js"
});
const L = sandbox.window.PicrossNamesLogic;

// The real generated data, so the widths and the coverage are the shipped ones
// rather than a fixture that can drift away from them.
const dataSrc = fs.readFileSync(path.join(root, "site/picross-names/data.js"), "utf8");
const DATA = JSON.parse(dataSrc.slice(dataSrc.indexOf("= ") + 2).trim().replace(/;$/, ""));

let checks = 0;
let failed = 0;
function check(cond, what) {
  checks++;
  if (!cond) {
    failed++;
    console.log("FAIL picrossnames  " + what);
  }
}
function eq(got, want, what) {
  check(got === want, what + "  (got " + JSON.stringify(got) + ", want " + JSON.stringify(want) + ")");
}

// --- the data itself -------------------------------------------------------

check(DATA.puzzles.length > 0, "data.js carries puzzles");
eq(DATA.size, 10, "the bank the tool names is 10x10");
check(
  DATA.puzzles.every((p) => /^[0-9]+$/.test(p.janko)),
  "every puzzle has a decimal janko key, which is what the names file is keyed by"
);
eq(
  new Set(DATA.puzzles.map((p) => p.janko)).size,
  DATA.puzzles.length,
  "the janko keys are unique, so no two puzzles collide onto one name"
);
check(
  DATA.puzzles.every((p) => p.prov >= 0 && p.prov < DATA.provenances.length),
  "every puzzle indexes a provenance row that exists"
);
["title", "body", "small"].forEach((slot) => {
  eq(DATA.fonts[slot].advance.length, 0x7f - 0x20, slot + " carries an advance for every printable ASCII character");
});

// --- judge -----------------------------------------------------------------

eq(L.judge("", DATA).level, "ok", "an empty field is not an error");
eq(L.judge("CAT", DATA).level, "ok", "a short plain name fits");
eq(L.judge("PIÑATA", DATA).level, "stop", "a glyph the cut lacks is refused");
check(L.judge("PIÑATA", DATA).text.includes('"Ñ"'), "and the message names the character");
eq(L.judge("PIÑATA", DATA).fold, "PINATA", "and offers the folded spelling");

// A fold is only offered when the folded spelling passes the WHOLE judgement.
// It used to be offered whenever the fold removed the HOLES, so the iPhone
// possessive got two stops in a row, the second caused by the tool's own
// suggestion.
eq(L.judge("PIÑATA'S", DATA).level, "stop", "an accent plus an apostrophe is refused");
eq(L.judge("PIÑATA'S", DATA).fold, null, "and no fold is offered, because the fold would be refused too");

// The character quoted must be the one that was typed. Reading text[i] after
// stepping over a surrogate pair quoted the LOW SURROGATE, so the message named
// a character nobody had typed.
check(L.judge("\u{1F600} CAT", DATA).text.includes("\u{1F600}"), "an emoji is quoted whole, not as half a surrogate pair");
// A decomposed accent is stripped rather than reported as a mark with no letter.
eq(L.judge("CAFÉ", DATA).fold, "CAFE", "a combining accent folds away");

eq(L.judge("CAFE'S BAR", DATA).level, "stop", "the file's charset refuses an apostrophe");
eq(L.judge("SEVENTEEN LETTERS", DATA).level, "stop", "over the file's 16-character cap");
eq(L.judge("A-FRAME HOUSE", DATA).level, "ok", "hyphens and spaces are allowed");
eq(L.judge("SIXTEEN CHARS OK", DATA).level, "warn", "16 legal characters can still be too wide for the display cut");
eq(L.judge("SIXTEEN CHARS OK", DATA).rung, 1, "and the device sets that one a cut down");

// Advances are rounded PER GLYPH, as EpdFont does, not summed and rounded once.
// Summed-then-rounded measured sixteen capital As at 488px against the device's
// 493, and the error pointed the unsafe way.
eq(L.measure("AAAAAAAAAAAAAAAA", "title", DATA.fonts).width, 493, "sixteen capital As measure as the device measures them");

// --- the three states, end to end -----------------------------------------

const puzzles = DATA.puzzles.slice(0, 4);
const entries = {
  [puzzles[0].id]: { name: "COCKTAIL GLASS", at: "2026-09-06T00:00:00.000Z" },
  [puzzles[1].id]: { nameless: true, at: "2026-09-06T00:00:00.000Z" }
  // puzzles[2] skipped: no entry at all
};
const out = L.namesJson(puzzles, entries);
const parsed = JSON.parse(out);
eq(parsed[puzzles[0].janko], "COCKTAIL GLASS", "a named puzzle exports its name");
eq(parsed[puzzles[1].janko], "", "a deliberately unnamed puzzle exports an empty string");
check(!(puzzles[2].janko in parsed), "an unanswered puzzle is absent, not empty");
check(out.indexOf('"_comment"') < out.indexOf('"' + puzzles[0].janko + '"'), "_comment is the first line, as in janko-authors.json");
eq(L.namesJson(puzzles, entries), out, "two exports of the same answers are the same bytes");

const counts = L.countsOf(puzzles, entries);
eq(counts.named, 1, "one named");
eq(counts.nameless, 1, "one deliberately unnamed");
eq(counts.left, 2, "two still to do");

// A {name: ""} arriving through a loaded save file used to be reachable by
// nothing: counted in neither total so it stayed in "left", truthy so the
// next-unanswered walk skipped it forever, and hidden from the "left to do"
// filter. The counter stuck one short with no way to find the puzzle.
eq(L.normaliseEntry({ name: "" }).nameless, true, "an empty name IS the no-good-name answer");
eq(L.countsOf(puzzles, { [puzzles[0].id]: { name: "" } }).nameless, 1, "and it is counted as one");

// --- clampPos --------------------------------------------------------------

eq(L.clampPos(5, 137), 5, "a real position is kept");
eq(L.clampPos(200, 137), 0, "a position past the end falls back to the first puzzle");
eq(L.clampPos(-1, 137), 0, "a negative position falls back");
eq(L.clampPos("nope", 137), 0, "a non-number falls back");
eq(L.clampPos(undefined, 137), 0, "a missing position falls back");

// --- merge -----------------------------------------------------------------

const known = {};
DATA.puzzles.forEach((p, i) => {
  known[p.id] = i;
});
const mine = {
  [puzzles[0].id]: { name: "OLD", at: "2026-09-01T00:00:00.000Z" },
  [puzzles[1].id]: { name: "KEEP ME", at: "2026-09-09T00:00:00.000Z" }
};
const theirs = {
  [puzzles[0].id]: { name: "NEWER", at: "2026-09-05T00:00:00.000Z" },
  [puzzles[1].id]: { name: "STALE", at: "2026-09-02T00:00:00.000Z" },
  [puzzles[2].id]: { name: "BRAND NEW", at: "2026-09-05T00:00:00.000Z" },
  NOT_A_PUZZLE: { name: "GHOST", at: "2099-01-01T00:00:00.000Z" }
};
const merged = L.mergeEntries(mine, theirs, known, DATA);
eq(merged.entries[puzzles[0].id].name, "NEWER", "a newer answer replaces an older one");
eq(merged.entries[puzzles[1].id].name, "KEEP ME", "an older answer never overwrites a newer one");
eq(merged.entries[puzzles[2].id].name, "BRAND NEW", "an answer only the other device had is kept");
check(!("NOT_A_PUZZLE" in merged.entries), "an id we do not ship is ignored");
eq(merged.added, 1, "one added");
eq(merged.updated, 1, "one replaced");
check(mine[puzzles[0].id].name === "OLD", "merge does not mutate what it was given");

// The import was the only door into the answers that never ran judge(), so a
// hand-edited or older save file could put a name in that the consuming
// generator rejects -- the very "137 names rejected at the end" failure the
// entry checks exist to prevent, arriving through the door they did not cover.
const dirty = L.mergeEntries({}, { [puzzles[0].id]: { name: "CAFE'S Ñ", at: "2026-09-05T00:00:00.000Z" } }, known, DATA);
check(!(puzzles[0].id in dirty.entries), "a name the file would reject does not get in through a save file");
eq(dirty.refused.length, 1, "and it is reported rather than dropped in silence");

// The confirmed data-loss bug, pinned. Before the fix, the half-typed word from
// before the load survived the merge, went back to storage and reappeared in
// the field on top of the answer the merge had just brought in -- where one
// Enter overwrote that answer with a NEWER timestamp, so merging back from the
// other device could not recover it either.
const beforeLoad = {
  v: 1,
  pos: 3,
  draft: "HALF TYPED",
  entries: {}
};
const afterLoad = L.mergeState(
  beforeLoad,
  { [puzzles[0].id]: { name: "GOOD ANSWER", at: "2026-09-05T00:00:00.000Z" } },
  known,
  DATA
);
eq(afterLoad.state.draft, "", "a save file load drops the draft it would otherwise overwrite the merged answer with");
eq(afterLoad.state.entries[puzzles[0].id].name, "GOOD ANSWER", "and the merged answer is there");
eq(afterLoad.state.pos, 3, "the position survives a load");
eq(beforeLoad.draft, "HALF TYPED", "mergeState does not mutate the state it was given");

// A save file whose position is past the end of a shrunken bank.
eq(L.mergeState({ v: 1, pos: 9999, draft: "", entries: {} }, {}, known, DATA).state.pos, 0,
   "a position past the end of the bank is clamped by a load too");

console.log(checks + " checks, " + failed + " failed");
process.exit(failed === 0 ? 0 : 1);
