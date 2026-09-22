// The arithmetic behind the inbox's fleet tables, kept out of the page so it
// can be run by a test instead of looked at (host-tests/site/fleet_js.js).
//
// Three jobs, all of them things the page got wrong before 2026-09-21:
//
//   1. ORDER. Versions were shown in whatever order the board returned. As
//      text "1.13.9" sorts after "1.13.15", so the newest release landed in
//      the middle of the table. compareVersions compares the dotted numbers.
//   2. ONE ROW PER VERSION. The old versions_now view was keyed
//      (version, board), so 1.13.15 appeared once for x4pro and again for
//      sticky and the reader had to add them up. foldVersions merges by
//      version and keeps the boards as a map.
//   3. BOTH SHAPES. The site deploys separately from the board's migrations,
//      so the page can be new while the view is still the old (version, board,
//      devices) one, or the reverse. foldVersions accepts either and produces
//      the same answer, which is why a migration that has not run yet cannot
//      empty this table.
//
// Everything here is pure: rows in, rows out, no DOM and no fetch.

(function (root, factory) {
  var api = factory();
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.FLEET = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  var UNKNOWN = "unknown";

  // "1.13.15" -> [1,13,15]; null for anything that is not a dotted number, so
  // a branch build or an empty string sorts last instead of throwing.
  function versionKey(v) {
    var m = /^\d+(?:\.\d+)*/.exec(String(v == null ? "" : v).trim());
    if (!m) return null;
    return m[0].split(".").map(function (n) {
      return parseInt(n, 10);
    });
  }

  // Newest first. Shorter keys lose to longer ones only where the longer one
  // has something past the shared prefix: 1.13 is older than 1.13.1 and the
  // same as 1.13.0.
  function compareVersions(a, b) {
    var ka = versionKey(a);
    var kb = versionKey(b);
    if (!ka && !kb)
      return String(b == null ? "" : b).localeCompare(
        String(a == null ? "" : a),
      );
    if (!ka) return 1;
    if (!kb) return -1;
    var n = Math.max(ka.length, kb.length);
    for (var i = 0; i < n; i++) {
      var x = i < ka.length ? ka[i] : 0;
      var y = i < kb.length ? kb[i] : 0;
      if (x !== y) return y - x;
    }
    return 0;
  }

  // Rows -> one row per version, newest first, each carrying a board -> count
  // map and the total. Accepts {version, devices, boards} and the older
  // {version, board, devices}, and merges duplicates of either.
  function foldVersions(rows) {
    var by = {};
    (rows || []).forEach(function (r) {
      if (!r) return;
      var version =
        r.version == null || r.version === "" ? UNKNOWN : String(r.version);
      var slot =
        by[version] ||
        (by[version] = { version: version, devices: 0, boards: {} });
      var boards = r.boards;
      if (boards && typeof boards === "object" && !Array.isArray(boards)) {
        Object.keys(boards).forEach(function (b) {
          var name = b || UNKNOWN;
          var n = Number(boards[b]) || 0;
          slot.boards[name] = (slot.boards[name] || 0) + n;
          slot.devices += n;
        });
        return;
      }
      // The older per-board shape, and the fallback when a row carries a count
      // and no board at all.
      var board = r.board == null || r.board === "" ? UNKNOWN : String(r.board);
      var count = Number(r.devices);
      if (!isFinite(count)) count = 0;
      slot.boards[board] = (slot.boards[board] || 0) + count;
      slot.devices += count;
    });
    return Object.keys(by)
      .map(function (v) {
        return by[v];
      })
      .sort(function (a, b) {
        return compareVersions(a.version, b.version);
      });
  }

  // Which board columns the table needs, busiest first so the fork's main
  // board leads without this file naming it. Derived from the data, never a
  // literal list: a board added to the firmware appears here by itself.
  function boardColumns(folded) {
    var total = {};
    (folded || []).forEach(function (row) {
      Object.keys(row.boards || {}).forEach(function (b) {
        total[b] = (total[b] || 0) + (Number(row.boards[b]) || 0);
      });
    });
    return Object.keys(total).sort(function (a, b) {
      return total[b] - total[a] || a.localeCompare(b);
    });
  }

  // device -> {versions: [...], services: [...]}, each list newest first.
  // Both inputs are flat (device, ...) rows straight off the board.
  function byDevice(versionRows, serviceRows) {
    var out = {};
    function slot(id) {
      return out[id] || (out[id] = { versions: [], services: [] });
    }
    (versionRows || []).forEach(function (r) {
      if (r && r.device) slot(r.device).versions.push(r);
    });
    (serviceRows || []).forEach(function (r) {
      if (r && r.device) slot(r.device).services.push(r);
    });
    Object.keys(out).forEach(function (id) {
      out[id].versions.sort(function (a, b) {
        return compareVersions(a.version, b.version);
      });
      out[id].services.sort(function (a, b) {
        return (
          String(b.last_at || "").localeCompare(String(a.last_at || "")) ||
          String(a.service || "").localeCompare(String(b.service || ""))
        );
      });
    });
    return out;
  }

  // The services that are supposed to post, so one that has never posted can
  // be shown as silent rather than be absent. A table can only ever draw what
  // it collected; this list is how the page knows what it did not.
  //
  // Kept in step with the `service` column in docs/workflow/events.md.
  //
  // `trivia` is in this list because the real board has been posting it, and
  // neither that table nor a first draft of this one mentioned it: the list
  // was taken from the docs and the docs were incomplete. A service missing
  // from here is not dropped -- a row the board returns is always shown -- but
  // it would never be reported as SILENT, which is the case that matters.
  var KNOWN_SERVICES = [
    "firmware",
    "site",
    "getbooks",
    "anki",
    "instapaper",
    "live",
    "trivia",
    "release",
    "pulse",
    "upstream-sync",
    "workflow",
  ];

  // Rows the board had, plus a zero row for every known service that posted
  // nothing, flagged so the page can say which is which.
  function withSilentServices(rows, known) {
    var list = (known || KNOWN_SERVICES).slice();
    var seen = {};
    var out = (rows || []).map(function (r) {
      seen[r.service] = true;
      return Object.assign({ silent: false }, r);
    });
    list.forEach(function (s) {
      if (!seen[s]) out.push({ service: s, silent: true });
    });
    return out.sort(function (a, b) {
      if (a.silent !== b.silent) return a.silent ? 1 : -1;
      return (
        (Number(b.devices_7d) || 0) - (Number(a.devices_7d) || 0) ||
        String(a.service || "").localeCompare(String(b.service || ""))
      );
    });
  }

  return {
    versionKey: versionKey,
    compareVersions: compareVersions,
    foldVersions: foldVersions,
    boardColumns: boardColumns,
    byDevice: byDevice,
    withSilentServices: withSilentServices,
    KNOWN_SERVICES: KNOWN_SERVICES,
  };
});
