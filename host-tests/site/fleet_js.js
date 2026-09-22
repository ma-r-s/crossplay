// site/assets/fleet.js: the arithmetic behind the inbox's fleet tables.
//
// This suite exists because the three bugs it pins are all invisible in a
// screenshot of real data. A versions table sorted as text looks fine until a
// two-digit patch appears; a version listed once per board looks like two
// releases; and a page that only understands the new view shape looks empty
// for as long as the board is a migration behind. Each of those shipped or
// nearly shipped, so each is a case here.
//
//   node host-tests/site/fleet_js.js <repo-root>

const path = require("node:path");

const root = process.argv[2] || path.join(__dirname, "..", "..");
const FLEET = require(path.join(root, "site", "assets", "fleet.js"));

let checks = 0;
let failures = 0;

function expect(label, got, want) {
  checks++;
  const g = JSON.stringify(got);
  const w = JSON.stringify(want);
  if (g === w) return;
  failures++;
  console.log(`  FAIL ${label}\n       got  ${g}\n       want ${w}`);
}

// -- ordering ---------------------------------------------------------------
// The case that named this card: as text "1.13.9" sorts after "1.13.15", so a
// descending string sort puts the newest release below an older one.
expect(
  "1.13.15 is newer than 1.13.9",
  FLEET.compareVersions("1.13.15", "1.13.9") < 0,
  true,
);
expect(
  "and the string sort it replaces disagrees",
  "1.13.15" > "1.13.9",
  false,
);
expect(
  "a sort puts the numeric order in",
  ["1.13.9", "1.2.0", "1.13.15", "1.9.4", "2.0.0"].sort(FLEET.compareVersions),
  ["2.0.0", "1.13.15", "1.13.9", "1.9.4", "1.2.0"],
);
expect(
  "1.13 is older than 1.13.1",
  FLEET.compareVersions("1.13", "1.13.1") > 0,
  true,
);
expect("1.13 ties with 1.13.0", FLEET.compareVersions("1.13", "1.13.0"), 0);
expect(
  "a branch build sorts last",
  FLEET.compareVersions("dev-build", "1.0.0") > 0,
  true,
);
expect(
  "two branch builds do not throw",
  typeof FLEET.compareVersions("a", "b"),
  "number",
);
expect(
  "an empty version sorts last",
  FLEET.compareVersions("", "1.0.0") > 0,
  true,
);
expect(
  "versionKey reads the numbers",
  FLEET.versionKey("1.13.15"),
  [1, 13, 15],
);
expect("versionKey refuses a word", FLEET.versionKey("nightly"), null);
expect(
  "versionKey takes the numeric head",
  FLEET.versionKey("1.12.9-rc1"),
  [1, 12, 9],
);

// -- one row per version ----------------------------------------------------
// The old view returned one row per (version, board). Three rows, two
// versions: the page must show two.
const oldShape = [
  { version: "1.13.15", board: "x4pro", devices: 12 },
  { version: "1.13.15", board: "sticky", devices: 3 },
  { version: "1.13.9", board: "x4pro", devices: 4 },
];
const foldedOld = FLEET.foldVersions(oldShape);
expect("three per-board rows fold to two versions", foldedOld.length, 2);
expect("newest first", foldedOld[0].version, "1.13.15");
expect("with both boards on one row", foldedOld[0].boards, {
  x4pro: 12,
  sticky: 3,
});
expect("and the total beside them", foldedOld[0].devices, 15);
expect("the older version keeps its own count", foldedOld[1], {
  version: "1.13.9",
  devices: 4,
  boards: { x4pro: 4 },
});

// The new view already folds, and must come out identical.
const newShape = [
  { version: "1.13.15", devices: 15, boards: { x4pro: 12, sticky: 3 } },
  { version: "1.13.9", devices: 4, boards: { x4pro: 4 } },
];
expect(
  "the new view shape gives the same answer as the old one",
  FLEET.foldVersions(newShape),
  foldedOld,
);

expect("no rows is no rows, not a crash", FLEET.foldVersions([]), []);
expect("a null row is skipped", FLEET.foldVersions([null, undefined]), []);
expect(
  "a missing board is called unknown",
  FLEET.foldVersions([{ version: "1.0.0", devices: 2 }])[0].boards,
  { unknown: 2 },
);
expect(
  "a missing version is called unknown",
  FLEET.foldVersions([{ board: "x4pro", devices: 2 }])[0].version,
  "unknown",
);
expect(
  "the sum over folded rows is still the device count",
  FLEET.foldVersions(oldShape).reduce((a, r) => a + r.devices, 0),
  19,
);

// -- board columns ----------------------------------------------------------
expect(
  "board columns come from the data, busiest first",
  FLEET.boardColumns(foldedOld),
  ["x4pro", "sticky"],
);
expect(
  "a board nobody has seen before needs no code change",
  FLEET.boardColumns(
    FLEET.foldVersions([{ version: "2.0.0", board: "newboard", devices: 1 }]),
  ),
  ["newboard"],
);
expect("no versions, no columns", FLEET.boardColumns([]), []);

// -- per device -------------------------------------------------------------
const versions = [
  {
    device: "aa",
    version: "1.13.9",
    first_at: "2026-09-01",
    last_at: "2026-09-04",
  },
  {
    device: "aa",
    version: "1.13.15",
    first_at: "2026-09-05",
    last_at: "2026-09-20",
  },
  {
    device: "bb",
    version: "1.13.15",
    first_at: "2026-09-06",
    last_at: "2026-09-19",
  },
];
const services = [
  { device: "aa", service: "getbooks", events: 9, last_at: "2026-09-18" },
  { device: "aa", service: "site", events: 40, last_at: "2026-09-20" },
];
const per = FLEET.byDevice(versions, services);
expect(
  "a device's versions are newest first",
  per.aa.versions.map((r) => r.version),
  ["1.13.15", "1.13.9"],
);
expect(
  "its services are most recent first",
  per.aa.services.map((r) => r.service),
  ["site", "getbooks"],
);
expect("a device with no service rows still appears", per.bb.services, []);
expect(
  "a row with no device is dropped",
  FLEET.byDevice([{ version: "1.0.0" }], []),
  {},
);

// -- services that posted nothing -------------------------------------------
// The gap that started the card: Live had no instrumentation, so it was
// ABSENT from the services table rather than shown as silent. Absent reads as
// "not a service"; silent reads as "nothing has arrived yet".
const withLive = FLEET.withSilentServices([
  { service: "site", devices_7d: 40, events_7d: 900 },
  { service: "getbooks", devices_7d: 6, events_7d: 30 },
]);
const live = withLive.find((r) => r.service === "live");
expect("a service that never posted is still listed", Boolean(live), true);
// `live ? ... : null` rather than live.silent: when this check is the one
// failing, the row is missing, and a TypeError here would hide every
// assertion after it behind a stack trace.
expect("and is marked silent", live ? live.silent : null, true);
const siteRow = withLive.find((r) => r.service === "site");
expect("a service that posted is not", siteRow ? siteRow.silent : null, false);
expect(
  "the ones with numbers come first, busiest first",
  withLive.slice(0, 2).map((r) => r.service),
  ["site", "getbooks"],
);
expect(
  "every known service appears exactly once",
  withLive.length,
  FLEET.KNOWN_SERVICES.length,
);
expect(
  "live is one of the known services",
  FLEET.KNOWN_SERVICES.includes("live"),
  true,
);

console.log(`fleet_js: ${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
