// The update-check proxy, run under plain node with GitHub and the board
// stubbed out.
//
// api/latest.js is where a device's update check turns into a count on the
// board. Every rule it keeps is asserted here: the JSON passes through
// verbatim, a device is counted once per request and a browser never, the
// crash and install it carries become the same events the bridges post, a
// bad header is ignored rather than trusted, GitHub's silence is a stale
// answer or a 502 (never a 2xx that would clear what the device carries),
// and the board being down never costs the device its answer.
//
//   node host-tests/site/latest_fn.js <repo-root>

const path = require("node:path");

const root = process.argv[2] || path.join(__dirname, "..", "..");
process.env.SUPABASE_URL = "https://board.test/";
process.env.SUPABASE_ANON_KEY = "anon-key-for-tests";
delete process.env.GITHUB_TOKEN;
const handler = require(path.join(root, "site", "api", "latest.js"));

let pass = 0;
let fail = 0;
const ok = (m) => {
  pass++;
  console.log("  ok   " + m);
};
const bad = (m) => {
  fail++;
  console.log("  FAIL " + m);
};
const is = (m, got, want) =>
  JSON.stringify(got) === JSON.stringify(want)
    ? ok(m)
    : bad(`${m} (want ${JSON.stringify(want)}, got ${JSON.stringify(got)})`);

const RELEASE = JSON.stringify({
  tag_name: "v1.12.50",
  assets: [
    {
      name: "firmware.bin",
      browser_download_url:
        "https://github.com/ma-r-s/crossplay/releases/download/v1.12.50/firmware.bin",
      size: 4200000,
    },
  ],
});
let github = { status: 200, body: RELEASE, etag: '"abc"', throws: false };
let board = { status: 201 };
let calls = [];
global.fetch = async function (url, opts) {
  opts = opts || {};
  const u = String(url);
  calls.push({ url: u, method: opts.method || "GET", headers: opts.headers || {}, body: opts.body });
  if (u.startsWith("https://api.github.com/")) {
    if (github.throws) throw new Error("network down");
    return new Response(github.status === 304 ? null : github.body, {
      status: github.status,
      headers: github.etag ? { etag: github.etag } : {},
    });
  }
  if (u.endsWith("/rest/v1/events") && opts.method === "POST")
    return new Response(null, { status: board.status });
  return new Response("unexpected", { status: 500 });
};

function req(headers, method) {
  const h = {};
  for (const [k, v] of Object.entries(headers || {})) h[k.toLowerCase()] = v;
  return { method: method || "GET", headers: h };
}
function res() {
  return {
    statusCode: 0,
    headers: {},
    body: undefined,
    setHeader(k, v) {
      this.headers[k.toLowerCase()] = v;
    },
    end(b) {
      this.body = b;
    },
  };
}
async function call(headers, method) {
  const r = res();
  await handler(req(headers, method), r);
  return r;
}
const posts = () => calls.filter((c) => c.url.endsWith("/rest/v1/events") && c.method === "POST");
const githubCalls = () => calls.filter((c) => c.url.startsWith("https://api.github.com/"));
const posted = () => posts().map((p) => JSON.parse(p.body));
function fresh() {
  calls = [];
  handler._resetCache();
  github = { status: 200, body: RELEASE, etag: '"abc"', throws: false };
  board = { status: 201 };
}

const DEV = "9f2c".repeat(16);
const UA = "CrossPlay-ESP32-1.12.47";
const REPORT = JSON.stringify({ battery_pct: 84, heap_min_kb: 112, uptime_h: 31 });
const device = (extra) =>
  Object.assign(
    { "X-CrossPlay-Device": DEV, "X-CrossPlay-Board": "x4pro", "User-Agent": UA, "X-CrossPlay-Report": REPORT },
    extra || {},
  );

(async () => {
  console.log("api/latest.js");

  fresh();
  let r = await call({ "User-Agent": "Mozilla/5.0" });
  is("a browser gets GitHub's JSON, verbatim", [r.statusCode, r.body], [200, RELEASE]);
  is("and the answer is never CDN-cached", r.headers["cache-control"], "no-store");
  is("and a browser is not counted", posts().length, 0);
  is("GitHub was asked as GitHub wants", githubCalls()[0].headers.Accept, "application/vnd.github+json");

  fresh();
  r = await call(device());
  is("a device gets the same JSON, verbatim", [r.statusCode, r.body], [200, RELEASE]);
  is("and is counted once", posts().length, 1);
  is(
    "as a site/update-check event with its id, board, version, health and the version offered",
    posted()[0],
    [
      {
        service: "site",
        event: "update-check",
        level: "info",
        device: DEV,
        version: "1.12.47",
        board: "x4pro",
        props: { battery_pct: 84, heap_min_kb: 112, uptime_h: 31, latest: "v1.12.50" },
      },
    ],
  );
  is("posted with the public key, as the bridges post", posts()[0].headers.apikey, "anon-key-for-tests");
  is("and asking for nothing back", posts()[0].headers.Prefer, "return=minimal");

  fresh();
  r = await call(
    device({
      "X-CrossPlay-Report": JSON.stringify({
        battery_pct: 40,
        crash: { message: "assert failed: x (reset: panic)", version: "1.12.46", backtrace: "" },
        ota: { attempted: true, ok: false, error: "too_large", path: "ota" },
      }),
    }),
  );
  let ev = posted()[0];
  is("a carried crash and install become three events in one post", [posts().length, ev.length], [1, 3]);
  is(
    "the crash is a firmware/crash error on the version that crashed, via the site",
    ev[1],
    {
      service: "firmware",
      event: "crash",
      level: "error",
      device: DEV,
      version: "1.12.46",
      board: "x4pro",
      props: { message: "assert failed: x (reset: panic)", backtrace: "", app: "firmware", via: "site" },
    },
  );
  is(
    "the failed install is a firmware/update error naming the reason and the path",
    ev[2],
    {
      service: "firmware",
      event: "update",
      level: "error",
      device: DEV,
      version: "1.12.47",
      board: "x4pro",
      props: {
        attempted: true,
        ok: false,
        error: "too_large",
        path: "ota",
        app: "firmware",
        message: "update failed: too_large (ota)",
      },
    },
  );

  fresh();
  await call(device({ "X-CrossPlay-Report": JSON.stringify({ ota: { attempted: true, ok: true } }) }));
  ev = posted()[0];
  is("an install that went well is a firmware/update at level info, no message", [ev[1].level, ev[1].props], [
    "info",
    { attempted: true, ok: true, app: "firmware" },
  ]);

  fresh();
  r = await call(device({ "X-CrossPlay-Device": "abc" }));
  is("an id that is not 64 hex is no id: served, not counted", [r.statusCode, posts().length], [200, 0]);

  fresh();
  await call(device({ "User-Agent": "curl/8.0", "X-CrossPlay-Board": "not a board!" }));
  ev = posted()[0][0];
  is("no CrossPlay User-Agent: no version; a board that is not a word: no board", [ev.version, ev.board], [
    undefined,
    undefined,
  ]);

  fresh();
  await call(device({ "X-CrossPlay-Report": "{" + " ".repeat(1200) + "}" }));
  ev = posted()[0];
  is("a report over 1000 bytes is ignored, the device still counted", [ev.length, ev[0].props], [
    1,
    { latest: "v1.12.50" },
  ]);

  fresh();
  await call(device({ "X-CrossPlay-Report": "not json" }));
  is("a report that is not JSON is ignored", posted()[0][0].props, { latest: "v1.12.50" });

  fresh();
  await call(
    device({
      "X-CrossPlay-Report": JSON.stringify({ battery_pct: "84", heap_min_kb: true, uptime_h: 3, crash: "boom", ota: [1] }),
    }),
  );
  ev = posted()[0];
  is("only numbers are health, and a crash or ota that is not an object is nothing", [ev.length, ev[0].props], [
    1,
    { uptime_h: 3, latest: "v1.12.50" },
  ]);

  fresh();
  github.status = 500;
  r = await call(device());
  is("GitHub down with nothing cached: 502, so the device asks GitHub itself", r.statusCode, 502);
  is(
    "and the failure is posted as an error naming the status, never the device's report",
    posted()[0],
    [
      {
        service: "site",
        event: "update-check",
        level: "error",
        device: DEV,
        version: "1.12.47",
        board: "x4pro",
        props: { message: "GitHub answered the update check with HTTP 500", fallback: "the device asks GitHub itself" },
      },
    ],
  );

  fresh();
  github.throws = true;
  r = await call(device());
  is("GitHub unreachable with nothing cached: 502 as well", r.statusCode, 502);
  is("posted with the cause", posted()[0][0].props.message, "GitHub did not answer the update check: Error");

  fresh();
  await call(device());
  await call(device());
  is("two devices within a minute: one GitHub request, two counts", [githubCalls().length, posts().length], [1, 2]);

  fresh();
  await call(device());
  handler._ageCache(61 * 1000);
  github.status = 304;
  r = await call(device());
  is("after a minute GitHub is asked again, conditionally", githubCalls()[1].headers["If-None-Match"], '"abc"');
  is("and a 304 serves the copy", [r.statusCode, r.body], [200, RELEASE]);

  fresh();
  await call(device());
  handler._ageCache(61 * 1000);
  github.status = 500;
  r = await call(device());
  is("GitHub down with a stale copy: the copy is served", [r.statusCode, r.body], [200, RELEASE]);

  fresh();
  github.body = "<html>rate limited</html>";
  r = await call(device());
  is("an answer that is not release JSON is not served", r.statusCode, 502);
  is("and says so", posted()[0][0].props.message, "GitHub's answer to the update check had no tag_name");

  fresh();
  board.status = 500;
  r = await call(device());
  is("the board refusing the post never costs the device its answer", [r.statusCode, r.body], [200, RELEASE]);

  fresh();
  r = await call(device(), "POST");
  is("POST is refused", r.statusCode, 405);

  console.log(`${pass + fail} checks, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})();
