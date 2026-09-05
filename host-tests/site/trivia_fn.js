// The trivia report endpoint, run under plain node with the board stubbed out.
//
// api/trivia.js is where a device's queued reports meet the board's write key,
// so every refusal it makes is asserted here, and so are the two things it must
// never do: store a device id, and let one device's repeat report of the same
// question count twice. The stub records every call, which is how "the id was
// not stored" is checked rather than assumed.
//
//   node host-tests/site/trivia_fn.js <repo-root>

const path = require("node:path");
const fs = require("node:fs");
const { Readable } = require("node:stream");

const root = process.argv[2] || path.join(__dirname, "..", "..");
process.env.SUPABASE_URL = "https://board.test";
process.env.SUPABASE_SERVICE_ROLE_KEY = "service-key-for-tests";
process.env.TRIVIA_REPORT_SECRET = "test-secret";
const handler = require(path.join(root, "site", "api", "trivia.js"));

let calls = [];
let rateRow = null;
global.fetch = async function (url, opts) {
  opts = opts || {};
  const u = String(url);
  calls.push({ url: u, method: opts.method || "GET", body: opts.body });
  if (
    u.includes("trivia_rate?ip_hash=eq.") &&
    (opts.method || "GET") === "GET"
  ) {
    return new Response(JSON.stringify(rateRow ? [rateRow] : []), {
      status: 200,
    });
  }
  return new Response("", { status: 201 });
};

let failed = 0;
let checks = 0;
function check(name, ok, detail) {
  checks += 1;
  if (ok) {
    console.log(`  ok   ${name}`);
  } else {
    failed += 1;
    console.log(`  FAIL ${name}${detail ? "  -- " + detail : ""}`);
  }
}

// A request whose body arrives as a STREAM with no content-type, which is what
// the device actually sends: bridge::request sets no Content-Type, so nothing
// upstream parses the body for the handler. A test that passed req.body as an
// object would be testing the simulator's request, not the device's.
function deviceRequest(obj, headers) {
  const raw = Buffer.from(typeof obj === "string" ? obj : JSON.stringify(obj));
  const req = Readable.from([raw]);
  req.method = "POST";
  req.url = "/api/trivia";
  req.headers = Object.assign(
    { "user-agent": "CrossPlay-ESP32-1.12.32", "x-forwarded-for": "10.0.0.9" },
    headers || {},
  );
  req.socket = { remoteAddress: "10.0.0.9" };
  return req;
}

function sink() {
  const res = {
    statusCode: 0,
    headers: {},
    payload: null,
    setHeader(k, v) {
      this.headers[k.toLowerCase()] = v;
    },
    end(s) {
      this.payload = s ? JSON.parse(s) : null;
    },
  };
  return res;
}

async function post(obj, headers) {
  calls = [];
  const res = sink();
  await handler(deviceRequest(obj, headers), res);
  return res;
}

function inserted() {
  const c = calls.find(
    (x) => x.url.includes("trivia_reports") && x.method === "POST",
  );
  return c ? JSON.parse(c.body) : null;
}

const DEVICE = "9f2c" + "a".repeat(60);

(async () => {
  // --- the happy path -------------------------------------------------------
  let res = await post(
    { pack: "abc123", count: 100, reports: [{ i: 4, r: "wrong" }, { i: 9 }] },
    { "x-crossplay-device": DEVICE, "x-crossplay-board": "x4pro" },
  );
  check(
    "a well-formed batch is accepted",
    res.statusCode === 202,
    `got ${res.statusCode}`,
  );
  let rows = inserted();
  check(
    "both reports are stored",
    rows && rows.length === 2,
    JSON.stringify(rows),
  );
  check(
    "a report with no reason stores 'none'",
    rows && rows[1].reason === "none",
  );
  check(
    "the pack id rides with every row",
    rows && rows.every((r) => r.pack === "abc123"),
  );
  check(
    "the count the device held is kept for the binding check",
    rows && rows[0].pack_count === 100,
  );
  check(
    "the firmware version is read from the User-Agent",
    rows && rows[0].version === "1.12.32",
    rows && rows[0].version,
  );

  // --- THE PRIVACY PROPERTY, asserted rather than described -----------------
  const blob = JSON.stringify(rows);
  check(
    "the device id appears in NO stored column",
    !blob.includes(DEVICE),
    blob.slice(0, 120),
  );
  check(
    "no column is named like a device id",
    rows && !Object.keys(rows[0]).some((k) => /device|mac|serial|ip/i.test(k)),
    rows && Object.keys(rows[0]).join(","),
  );

  // Same device, same question, twice -> the SAME key, so the table's primary
  // key collapses them. Different questions -> different keys that share
  // nothing, so the rows cannot be joined into a reading history.
  const again = await post(
    { pack: "abc123", count: 100, reports: [{ i: 4, r: "wrong" }] },
    { "x-crossplay-device": DEVICE },
  );
  const rowsAgain = inserted();
  check(
    "the same device reporting the same question makes the same key",
    rowsAgain[0].report_key === rows[0].report_key,
  );
  check(
    "different questions get unrelated keys",
    rows[0].report_key !== rows[1].report_key,
  );
  check(
    "a repeat is an upsert that ignores duplicates",
    calls.some((c) => String(c.url).includes("on_conflict=report_key")),
  );
  check("the repeat still answers 202", again.statusCode === 202);

  // A different device reporting the SAME question must not collide with the
  // first, or one reader would silently erase another's report.
  await post(
    { pack: "abc123", count: 100, reports: [{ i: 4, r: "wrong" }] },
    { "x-crossplay-device": "0000" + "b".repeat(60) },
  );
  check(
    "a different device reporting the same question does not collide",
    inserted()[0].report_key !== rows[0].report_key,
  );

  // The same question in a DIFFERENT pack is a different report: an index is
  // meaningless without its pack.
  await post(
    { pack: "zzz999", count: 100, reports: [{ i: 4, r: "wrong" }] },
    { "x-crossplay-device": DEVICE },
  );
  check(
    "the same index in another pack is a different report",
    inserted()[0].report_key !== rows[0].report_key,
  );

  // --- headerless devices ---------------------------------------------------
  await post({ pack: "abc123", count: 100, reports: [{ i: 1 }] });
  const anonA = inserted()[0].report_key;
  await post({ pack: "abc123", count: 100, reports: [{ i: 1 }] });
  const anonB = inserted()[0].report_key;
  check("a device with the headers off still reports", anonA && anonB);
  check(
    "headerless reports do NOT all collide into one row",
    anonA !== anonB,
    "a constant key would silently discard every reader but the first",
  );

  // --- refusals -------------------------------------------------------------
  res = await post({ count: 10, reports: [{ i: 1 }] });
  check("a batch naming no pack is refused", res.statusCode === 400);
  check("nothing is stored when the pack is missing", inserted() === null);

  res = await post({ pack: "a b/c", count: 10, reports: [{ i: 1 }] });
  check("a pack id with punctuation is refused", res.statusCode === 400);

  res = await post({ pack: "abc123", count: 10, reports: [] });
  check("an empty batch is refused", res.statusCode === 400);

  res = await post({ pack: "abc123", count: 10, reports: [{ i: 10 }] });
  check("an index past the pack's count is refused", res.statusCode === 400);
  res = await post({ pack: "abc123", count: 10, reports: [{ i: -1 }] });
  check("a negative index is refused", res.statusCode === 400);
  res = await post({ pack: "abc123", count: 10, reports: [{ i: 1.5 }] });
  check("a non-integer index is refused", res.statusCode === 400);

  res = await post({
    pack: "abc123",
    count: 10,
    reports: [{ i: 1, r: "because" }],
  });
  check("an unknown reason is refused", res.statusCode === 400);
  check("nothing is stored when a reason is unknown", inserted() === null);

  res = await post({
    pack: "abc123",
    count: 1000,
    reports: Array.from({ length: 65 }, (_, i) => ({ i })),
  });
  check("a batch over the cap is refused", res.statusCode === 413);

  res = await post("this is not json");
  check("an unreadable body is refused", res.statusCode === 400);

  // Duplicated inside ONE batch: the first wins, and only one row is written.
  await post({
    pack: "abc123",
    count: 10,
    reports: [
      { i: 3, r: "wrong" },
      { i: 3, r: "easy" },
    ],
  });
  rows = inserted();
  check(
    "a question repeated inside one batch stores once",
    rows.length === 1,
    JSON.stringify(rows),
  );
  check("the FIRST reason in a batch wins", rows[0].reason === "wrong");

  // --- rate limiting --------------------------------------------------------
  rateRow = { window_start: new Date().toISOString(), count: 20 };
  res = await post({ pack: "abc123", count: 10, reports: [{ i: 1 }] });
  check("a caller over the window limit gets 429", res.statusCode === 429);
  check("nothing is stored for a rate-limited caller", inserted() === null);

  rateRow = {
    window_start: new Date(Date.now() - 2 * 3600 * 1000).toISOString(),
    count: 999,
  };
  res = await post({ pack: "abc123", count: 10, reports: [{ i: 1 }] });
  check(
    "a stale window resets rather than blocking forever",
    res.statusCode === 202,
  );
  rateRow = null;

  // --- method and configuration --------------------------------------------
  const g = sink();
  await handler({ method: "GET", url: "/api/trivia", headers: {} }, g);
  check(
    "GET is refused and says where the manifest lives",
    g.statusCode === 405,
  );
  check("the refusal names the release", /release/i.test(g.payload.error));

  // --- bounds that do not depend on the batch declaring a count -------------
  res = await post({ pack: "abc123", reports: [{ i: 9007199254740991 }] });
  check("a huge index is refused even with no declared count", res.statusCode === 400,
    `got ${res.statusCode}`);
  res = await post({ pack: "..", count: 10, reports: [{ i: 1 }] });
  check("a pack id of .. is refused", res.statusCode === 400);
  res = await post({ pack: ".", count: 10, reports: [{ i: 1 }] });
  check("a pack id of . is refused", res.statusCode === 400);

  // A body the platform already parsed must still meet the size cap; the limit
  // used to guard only the streaming path.
  {
    calls = [];
    const big = sink();
    const req = {
      method: "POST",
      url: "/api/trivia",
      headers: { "x-forwarded-for": "10.0.0.9" },
      socket: { remoteAddress: "10.0.0.9" },
      body: { pack: "abc123", count: 10, note: "x".repeat(20000), reports: [{ i: 1 }] },
    };
    await handler(req, big);
    check("an oversized pre-parsed body is refused", big.statusCode === 413, `got ${big.statusCode}`);
  }

  // x-forwarded-for is append-only, so the FIRST entry is whatever the caller
  // chose. Two requests that differ only in that prefix must land in the same
  // rate bucket, or anyone can pick their own.
  {
    const bucket = async (xff) => {
      calls = [];
      const r = sink();
      await handler(deviceRequest(
        { pack: "abc123", count: 10, reports: [{ i: 1 }] },
        { "x-forwarded-for": xff },
      ), r);
      const c = calls.find((x) => x.url.includes("trivia_rate?ip_hash=eq."));
      return c ? c.url.split("ip_hash=eq.")[1].split("&")[0] : null;
    };
    const honest = await bucket("203.0.113.7");
    const spoofed = await bucket("1.2.3.4, 203.0.113.7");
    check("the rate bucket is the last hop, not the caller's claim",
      honest && honest === spoofed, `${honest} vs ${spoofed}`);
  }

  // --- the anonymity secret has no usable default --------------------------
  //
  // This test is why the module is re-required rather than reused: the harness
  // sets TRIVIA_REPORT_SECRET at the top, so every assertion above runs in the
  // configured case and NONE of them can see a bad default. The bad default
  // was sha256 over the first 32 characters of a Supabase service key, which
  // are the base64url of the standard HS256 header and identical on every
  // Supabase project -- a public constant standing in for a secret.
  {
    const modPath = require.resolve(path.join(root, "site", "api", "trivia.js"));
    const saved = process.env.TRIVIA_REPORT_SECRET;
    delete process.env.TRIVIA_REPORT_SECRET;
    delete require.cache[modPath];
    const unconfigured = require(modPath);
    calls = [];
    const r = sink();
    await unconfigured(deviceRequest({ pack: "abc123", count: 10, reports: [{ i: 1 }] }), r);
    check("with no secret configured the endpoint refuses", r.statusCode === 503, `got ${r.statusCode}`);
    check("and stores nothing", inserted() === null);
    process.env.TRIVIA_REPORT_SECRET = saved;
    delete require.cache[modPath];
  }

  // --- the parity that keeps a code from meaning two things -----------------
  const py = fs.readFileSync(
    path.join(root, "tools_local", "trivia", "reports.py"),
    "utf8",
  );
  const pyCodes = [...py.matchAll(/^\s+(\d+): "([a-z]+)",/gm)].map((m) => [
    Number(m[1]),
    m[2],
  ]);
  const pyNone = /^NONE = (\d+)$/m.exec(py);
  const table = [[Number(pyNone[1]), "none"], ...pyCodes].sort(
    (a, b) => a[0] - b[0],
  );
  check(
    "reports.py defines every code the endpoint knows",
    table.length === handler.REASONS.length,
    `py=${table.length} js=${handler.REASONS.length}`,
  );
  check(
    "the code table is identical on both sides, in order",
    table.every(([code, name], i) => code === i && handler.REASONS[i] === name),
    JSON.stringify(table),
  );

  console.log(`\n${checks} checks, ${failed} failed`);
  process.exit(failed ? 1 : 0);
})();
