// GET /api/latest
//
// The firmware's update check, proxied. A device used to ask GitHub's
// releases/latest directly, so the one request nearly every device makes was
// the one nobody could count: a device was seen only when it used Get Books
// or a bridge. Now it asks here first (src/network/ReleaseSources.h) and
// this function answers with GitHub's JSON, verbatim, after reading the
// three device headers and posting the events the bridges post
// (docs/workflow/events.md, "What each service posts"). GitHub stays the
// firmware's fallback, so a device never loses OTA to this function.
//
// Counting is the point, so every device request runs the function: the
// answer is never CDN-cached. GitHub's rate limit (60/h per IP without a
// token, and Vercel's egress IPs are shared) is spared three ways: this
// instance keeps its last answer for CACHE_MS, asks conditionally so a 304
// does not count, and sends GITHUB_TOKEN when the deployment has one. When
// GitHub does not answer, a stale copy is served if there is one, else 502
// and the device asks GitHub itself.
//
// Never a MAC: the device id is the pseudonymous 64-hex the device sends,
// and a request without one is served and not counted.

const GITHUB =
  "https://api.github.com/repos/ma-r-s/crossplay/releases/latest";
const CACHE_MS = 60 * 1000;
const GITHUB_TIMEOUT_MS = 6000;
const BOARD_TIMEOUT_MS = 2500;
// The report header is at most 600 bytes from the firmware; anything past
// this cap is not a report, whatever it says it is.
const REPORT_MAX = 1000;
const HEALTH_KEYS = ["battery_pct", "heap_min_kb", "uptime_h"];
const HEX64 = /^[0-9a-fA-F]{64}$/;
const BOARD = /^[a-z0-9]{1,16}$/;
const UA = /^CrossPlay-ESP32-(\S{1,32})/;

let cached = null; // { at, etag, body, tag }
// Why the last answer was not GitHub's own: "" when it was, else one line
// naming the failure, so a 502 can be posted as an error the board turns
// into a card. Without this a rate-limited egress IP would only ever show
// as a count that is quietly low.
let failure = "";

function header(req, name) {
  const v = (req.headers || {})[name.toLowerCase()];
  return Array.isArray(v) ? String(v[0] || "") : String(v || "");
}

function isObject(v) {
  return v !== null && typeof v === "object" && !Array.isArray(v);
}

// What one request says about the device that made it, trusting none of it:
// an id that is not 64 hex is no id, a board that is not a short word is no
// board, a report over REPORT_MAX bytes or not a JSON object is ignored, and
// only numbers are copied out of the health fields. The same rules as the
// bridges' events.py client_of().
function clientOf(req) {
  const c = { device: "", board: "", version: "", health: {}, crash: null, ota: null };
  try {
    const dev = header(req, "x-crossplay-device").trim();
    if (HEX64.test(dev)) c.device = dev.toLowerCase();
    const board = header(req, "x-crossplay-board").trim().toLowerCase();
    if (BOARD.test(board)) c.board = board;
    const m = UA.exec(header(req, "user-agent"));
    if (m) c.version = m[1];
    const raw = header(req, "x-crossplay-report");
    if (raw && raw.length <= REPORT_MAX) {
      let report = null;
      try {
        report = JSON.parse(raw);
      } catch (e) {
        report = null;
      }
      if (isObject(report)) {
        for (const k of HEALTH_KEYS) {
          const v = report[k];
          if (typeof v === "number" && Number.isFinite(v)) c.health[k] = v;
        }
        if (isObject(report.crash)) c.crash = report.crash;
        if (isObject(report.ota)) c.ota = report.ota;
      }
    }
  } catch (e) {
    // headers ignored; the request is served all the same
  }
  return c;
}

function record(service, event, level, c, version, props) {
  const r = { service, event, level };
  if (c.device) r.device = c.device;
  if (version) r.version = String(version);
  if (c.board) r.board = c.board;
  r.props = props;
  return r;
}

// The events one counted request posts: the usage event (site/update-check,
// with the health numbers and the version offered), plus the crash and the
// install attempt the device is carrying, as firmware events of their own.
function eventsOf(c, latestTag) {
  const out = [];
  const usage = Object.assign({}, c.health);
  if (latestTag) usage.latest = latestTag;
  out.push(record("site", "update-check", "info", c, c.version, usage));
  if (c.crash) {
    const k = c.crash;
    out.push(
      record("firmware", "crash", "error", c, k.version || c.version, {
        message: String(k.message || ""),
        backtrace: String(k.backtrace || ""),
        app: "firmware",
        via: "site",
      }),
    );
  }
  if (c.ota) {
    const o = c.ota;
    const props = Object.assign({}, o, { app: "firmware" });
    let level = "info";
    if (o.ok === false && o.error) {
      level = "error";
      props.message = `update failed: ${o.error} (${o.path || "unknown"})`;
    }
    out.push(record("firmware", "update", level, c, c.version, props));
  }
  return out;
}

// One POST for all of them, with the public key, as the bridges do. Never
// throws: the board is where numbers go, never something a device waits on.
async function post(records) {
  const url = (process.env.SUPABASE_URL || "").trim().replace(/\/+$/, "");
  const key = (process.env.SUPABASE_ANON_KEY || "").trim();
  if (!url || !key || !records.length) return false;
  try {
    const r = await fetch(`${url}/rest/v1/events`, {
      method: "POST",
      headers: {
        apikey: key,
        Authorization: `Bearer ${key}`,
        "Content-Type": "application/json",
        Prefer: "return=minimal",
      },
      body: JSON.stringify(records),
      signal: AbortSignal.timeout(BOARD_TIMEOUT_MS),
    });
    return r.ok;
  } catch (e) {
    return false;
  }
}

async function latest() {
  const now = Date.now();
  if (cached && now - cached.at < CACHE_MS) return cached;
  const headers = {
    "User-Agent": "crossplay-site (update check proxy)",
    Accept: "application/vnd.github+json",
  };
  const token = (process.env.GITHUB_TOKEN || "").trim();
  if (token) headers.Authorization = `Bearer ${token}`;
  if (cached && cached.etag) headers["If-None-Match"] = cached.etag;
  let r;
  try {
    r = await fetch(GITHUB, { headers, signal: AbortSignal.timeout(GITHUB_TIMEOUT_MS) });
  } catch (e) {
    failure = "GitHub did not answer the update check: " + String((e && e.name) || e);
    return cached;
  }
  if (r.status === 304 && cached) {
    failure = "";
    cached = Object.assign({}, cached, { at: now });
    return cached;
  }
  if (!r.ok) {
    failure = `GitHub answered the update check with HTTP ${r.status}`;
    return cached;
  }
  const body = await r.text();
  let tag = "";
  try {
    tag = String(JSON.parse(body).tag_name || "");
  } catch (e) {
    tag = "";
  }
  if (!tag) {
    failure = "GitHub's answer to the update check had no tag_name";
    return cached;
  }
  failure = "";
  cached = { at: now, etag: r.headers.get("etag") || "", body, tag };
  return cached;
}

module.exports = async function handler(req, res) {
  res.setHeader("Content-Type", "application/json");
  res.setHeader("Cache-Control", "no-store");
  if (req.method !== "GET" && req.method !== "HEAD") {
    res.statusCode = 405;
    res.end(JSON.stringify({ error: "GET only." }));
    return;
  }
  const answer = await latest();
  const client = clientOf(req);
  if (!answer) {
    // Not a 2xx, so the device keeps what it is carrying for the next
    // request that is one, and asks GitHub itself. The failure is posted as
    // an error (one fingerprint per cause, so one card with a count): a
    // device that fell back was not counted, and that must show somewhere.
    await post([
      record("site", "update-check", "error", client, client.version, {
        message: failure || "GitHub did not answer the update check",
        fallback: "the device asks GitHub itself",
      }),
    ]);
    res.statusCode = 502;
    res.end(JSON.stringify({ error: "GitHub did not answer; ask it directly." }));
    return;
  }
  if (client.device) await post(eventsOf(client, answer.tag));
  res.statusCode = 200;
  res.end(req.method === "HEAD" ? undefined : answer.body);
};

module.exports.clientOf = clientOf;
module.exports.eventsOf = eventsOf;
module.exports._resetCache = function () {
  cached = null;
};
module.exports._ageCache = function (ms) {
  if (cached) cached.at -= ms;
};
