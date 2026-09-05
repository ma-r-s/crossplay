// POST /api/trivia   the device's queued question reports, handed over on sync
//
// Board card #257. The one job this function has is to take reports off a
// device and put them where a person can read them. It is a function rather
// than a static file for the same reason api/report.js is: the board's write
// key must never reach a client.
//
// WHAT THIS DELIBERATELY DOES NOT DO
//
// It does not serve the manifest. The device fetches `pack.json` straight from
// the GitHub release, beside the `pack.dat` it already fetches from there. It
// is not a browser, so the CORS problem that forced api/firmware.js into
// existence does not apply to it, and a freshness check needing no service is
// better than one that does. That keeps this endpoint down to a single verb.
//
// It does not validate the pack id against the CURRENT manifest, and that is
// not an oversight. Old reports are the ones most worth keeping: a report filed
// against a pack from two builds ago is exactly what the frozen index map
// exists for. Rejecting anything but the newest pack would discard the reports
// the whole design was built to collect. Resolution happens later and offline,
// against each build's published index map; `outcome` stays 'open' until then.
//
// IDENTITY
//
// The device id in X-CrossPlay-Device (docs/workflow/events.md) is used to
// build ONE value and is then dropped:
//
//     report_key = sha256(secret, device, pack, index)
//
// which is the table's primary key. A repeat report of the same question by the
// same device collides, so de-duplication works; two reports of different
// questions share nothing, so the rows cannot be assembled into a reading
// history. The id is never stored in any column. A device sending no header
// (the toggle is off, or older firmware) still reports; it gets a random key,
// so it merely cannot be de-duplicated, which is the right trade over refusing.

const crypto = require("node:crypto");

const SUPABASE_URL = process.env.SUPABASE_URL || "";
const SERVICE_KEY = process.env.SUPABASE_SERVICE_ROLE_KEY || "";
// NO FALLBACK, deliberately. This was
//     SECRET = process.env.TRIVIA_REPORT_SECRET || SERVICE_KEY.slice(0, 32)
// which is not a secret at all: a Supabase service-role JWT begins with the
// base64url of the standard HS256 header, so its first 32 characters are
// IDENTICAL on every Supabase project and are public knowledge. Unset, the
// report key became sha256(<public constant>, device, pack, index), offline
// computable by anyone holding a device id, and the address hash became
// brute-forceable across all of IPv4 in seconds. The migration's promise that
// "the secret never leaves the server" was false in the default configuration.
//
// A missing secret is therefore a configuration error, not a degraded mode.
const SECRET = process.env.TRIVIA_REPORT_SECRET || "";

// Sized against what a device can actually hold queued. The card carries one
// report per question a player hid; a long offline stretch is tens, not
// thousands, and a batch larger than this did not come from a reader.
const MAX_REPORTS = 64;
// Comfortably above any pack (the live one is 49,958) and far below the
// column's range, so an out-of-range index is a 400 with a sentence rather than
// an overflow surfacing as a misleading 502.
const MAX_INDEX = 10 * 1000 * 1000;
const MAX_BODY = 8 * 1024;
const WINDOW_MS = 60 * 60 * 1000;
const BATCHES_PER_WINDOW = 20;

// The codes, and they must match tools_local/trivia/reports.py exactly. A code
// meaning "wrong answer" on one side and "too easy" on the other is a silent
// corpus edit. host-tests/site/trivia_fn.js asserts the two lists agree.
const REASONS = [
  "none",
  "wrong",
  "nonsense",
  "giveaway",
  "ambiguous",
  "outdated",
  "broken",
  "regional",
  "us",
  "hard",
  "easy",
];

function json(res, status, body) {
  res.statusCode = status;
  res.setHeader("Content-Type", "application/json");
  res.setHeader("Cache-Control", "no-store");
  res.end(JSON.stringify(body));
}

// api/report.js's reader, kept for a reason of this endpoint's own:
// bridge::request sends its POST body with NO Content-Type on the device
// (SecureHttpClient writes Host, User-Agent, Connection, auth, caller headers
// and Content-Length, and nothing in BridgeHttp.cpp adds one), while the
// SIMULATOR path passes -H 'Content-Type: application/json'. A handler that
// trusts a pre-parsed req.body therefore works in the simulator and fails on
// hardware. Read the stream instead.
async function readBody(req, limit) {
  if (req.body !== undefined && req.body !== null) {
    // The cap applies here too. It guarded only the streaming path, so a
    // platform that pre-parsed the body skipped it entirely.
    const pre = Buffer.isBuffer(req.body)
      ? req.body
      : Buffer.from(
          typeof req.body === "string" ? req.body : JSON.stringify(req.body),
        );
    return pre.length > limit ? null : pre;
  }
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > limit) return null;
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

function clientIp(req) {
  // The LAST entry, not the first. x-forwarded-for is append-only as a request
  // crosses proxies, so the first element is whatever the CALLER put there --
  // letting anyone choose their own rate-limit bucket by sending a header. The
  // last is the one the nearest trusted proxy wrote.
  const fwd = req.headers["x-forwarded-for"];
  if (typeof fwd === "string" && fwd.length) {
    const hops = fwd.split(",").map((h) => h.trim()).filter(Boolean);
    if (hops.length) return hops[hops.length - 1];
  }
  return (req.socket && req.socket.remoteAddress) || "0.0.0.0";
}

function sha(...parts) {
  const h = crypto.createHash("sha256");
  for (const p of parts) h.update(String(p)).update("\u0000");
  return h.digest("hex");
}

async function rest(path, init) {
  return fetch(
    `${SUPABASE_URL}/rest/v1/${path}`,
    Object.assign({}, init, {
      headers: Object.assign(
        {
          apikey: SERVICE_KEY,
          Authorization: `Bearer ${SERVICE_KEY}`,
          "Content-Type": "application/json",
          Accept: "application/json",
        },
        (init && init.headers) || {},
      ),
    }),
  );
}

// One counter per address hash, taken atomically.
//
// This was SELECT-then-PATCH, which is not a limiter at all: N concurrent
// requests read the same count and all write count+1, so a burst of any size
// advanced it by one -- and a burst is precisely what a rate limit is for. The
// increment now happens inside Postgres (trivia_rate_take), which returns the
// count THIS caller took.
//
// It still fails open, deliberately: losing a reader's reports because the
// counter table hiccupped is a worse outcome than one extra batch. But it no
// longer fails open on a REST error being mistaken for "no row yet", which is
// how the old version reset the window on any transient failure.
async function allow(ipHash) {
  try {
    const r = await fetch(`${SUPABASE_URL}/rest/v1/rpc/trivia_rate_take`, {
      method: "POST",
      headers: {
        apikey: SERVICE_KEY,
        Authorization: `Bearer ${SERVICE_KEY}`,
        "Content-Type": "application/json",
        Accept: "application/json",
      },
      body: JSON.stringify({
        p_ip_hash: ipHash,
        p_window: `${Math.round(WINDOW_MS / 1000)} seconds`,
      }),
    });
    if (!r.ok) return true;
    const taken = await r.json();
    if (!Number.isFinite(taken)) return true;
    return taken <= BATCHES_PER_WINDOW;
  } catch (err) {
    return true;
  }
}

function parseVersion(ua) {
  const m = /^CrossPlay-ESP32-(\d{1,3}\.\d{1,3}\.\d{1,3})$/.exec(
    String(ua || "").trim(),
  );
  return m ? m[1] : null;
}

async function postReports(req, res) {
  const raw = await readBody(req, MAX_BODY);
  if (raw === null)
    return json(res, 413, { error: "That batch is too large." });
  let body;
  try {
    body = JSON.parse(raw.toString("utf8") || "{}");
  } catch (err) {
    return json(res, 400, { error: "The batch was not readable JSON." });
  }

  const pack = String(body.pack == null ? "" : body.pack).trim();
  // A batch with no pack id names nothing. The device sends none when its own
  // pack.meta failed its binding check, which is the honest "I do not know
  // which build I hold" -- and in that state there is nothing worth storing.
  // "." and ".." excluded explicitly: the resolver joins this into a path to
  // that build's index map, and an id that can climb out of its own directory
  // is harmless right up until it is not.
  if (!/^[a-z0-9._-]{1,32}$/i.test(pack) || pack === "." || pack === "..") {
    return json(res, 400, { error: "That batch does not name a pack." });
  }
  const list = Array.isArray(body.reports) ? body.reports : null;
  if (!list || !list.length) {
    return json(res, 400, { error: "That batch has no reports in it." });
  }
  if (list.length > MAX_REPORTS) {
    return json(res, 413, {
      error: `A batch carries at most ${MAX_REPORTS} reports.`,
    });
  }
  const packCount =
    Number.isInteger(body.count) && body.count > 0 ? body.count : null;

  const seen = new Set();
  const rows = [];
  for (const item of list) {
    if (!item || typeof item !== "object") {
      return json(res, 400, {
        error: "A report in that batch is not an object.",
      });
    }
    const idx = item.i;
    // MAX_INDEX applies even when the batch declares no count. Without it any
    // integer up to 2^53 was accepted, which overflows the migration's integer
    // column and makes every row of a flood distinct.
    if (
      !Number.isInteger(idx) ||
      idx < 0 ||
      idx > MAX_INDEX ||
      (packCount !== null && idx >= packCount)
    ) {
      return json(res, 400, {
        error: `Report index ${idx} is not inside that pack.`,
      });
    }
    const reason = item.r == null ? "none" : String(item.r);
    if (!REASONS.includes(reason)) {
      return json(res, 400, { error: `Unknown reason ${reason}.` });
    }
    // Within one batch the first report of a question wins, which is the same
    // rule collect_flags.py applies on the card.
    if (seen.has(idx)) continue;
    seen.add(idx);
    rows.push({ idx, reason });
  }

  const ipHash = sha(clientIp(req), SECRET);
  if (!(await allow(ipHash))) {
    return json(res, 429, {
      error: "That is a lot of reports from one place. Try again later.",
    });
  }

  // The device id is read HERE and used for nothing but the key below.
  const device = String(req.headers["x-crossplay-device"] || "");
  const board =
    String(req.headers["x-crossplay-board"] || "").slice(0, 16) || null;
  const version = parseVersion(req.headers["user-agent"]);

  const payload = rows.map(({ idx, reason }) => ({
    // No device header means no stable key, so this report cannot be
    // de-duplicated. Random rather than a constant: a constant would collide
    // every headerless device into ONE row and silently discard real reports
    // from different readers.
    report_key: device ? sha(SECRET, device, pack, idx) : crypto.randomUUID(),
    pack,
    idx,
    pack_count: packCount,
    reason,
    version,
    board,
  }));

  const r = await rest("trivia_reports?on_conflict=report_key", {
    method: "POST",
    headers: {
      Prefer: "resolution=ignore-duplicates,return=minimal",
    },
    body: JSON.stringify(payload),
  });
  if (!r.ok) {
    return json(res, 502, {
      error: "The board did not take the reports. Try again later.",
    });
  }

  // Counted beside every other service's numbers. A failed count must never
  // fail the reports themselves, which are already stored by this point.
  await rest("events", {
    method: "POST",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({
      service: "trivia",
      event: "report",
      version,
      board,
      props: {
        pack,
        reports: payload.length,
        identified: Boolean(device),
      },
    }),
  }).catch(() => null);

  return json(res, 202, { accepted: payload.length });
}

module.exports = async function handler(req, res) {
  if (!SUPABASE_URL || !SERVICE_KEY || !SECRET) {
    // SECRET is checked beside the others because without it every anonymity
    // property this endpoint claims is false, and taking reports it cannot
    // anonymise is worse than taking none.
    return json(res, 503, {
      error: "Reporting is not set up on this deployment.",
    });
  }
  if (req.method === "POST") return postReports(req, res);
  res.setHeader("Allow", "POST");
  return json(res, 405, {
    error:
      "Send reports with POST. The pack manifest comes from the release, not from here.",
  });
};
module.exports.REASONS = REASONS;
