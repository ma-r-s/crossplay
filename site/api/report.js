// POST /api/report              JSON: what happened, from the site's report form
// PUT  /api/report?photo=<id>   the optional photo for that report, raw bytes
//
// Turns a stranger's report into a card on the board (server/board/supabase).
// Same shape as api/firmware.js: the only reason this is a function rather
// than a static page is that the board's write key must never reach a browser.
// It writes with the service key, which bypasses row security, so every check
// on what a caller may do lives here and nowhere else.
//
// No account, no CAPTCHA. A honeypot field catches the dumb bots (they fill
// every input; a person never sees it), sizes are capped, and one address gets
// ten reports an hour, counted against a salted hash of the address so the
// address itself is never stored.
//
// `device` names one or both of the two boards the fork runs on, comma-joined
// ("x4pro", "sticky", "x4pro,sticky"). There is no "not sure": a person
// reporting from a device knows which one they hold, and a report naming
// neither is refused rather than filed under a value nobody can act on. The
// card keeps the joined string, in a fixed order, so two reports about both
// boards read the same.
//
// The photo comes in a second request because a buffered Vercel request body
// is capped at 4.5MB and a phone photo is bigger than that until the page
// resizes it. The page sends the report first, gets its id, then the photo.
// A photo may only be attached to a card that the site created within the
// last hour and that has no photo yet, so the endpoint cannot be used to hang
// files on somebody else's card.

const crypto = require("node:crypto");

const SUPABASE_URL = process.env.SUPABASE_URL || "";
const SERVICE_KEY = process.env.SUPABASE_SERVICE_ROLE_KEY || "";
const SALT = process.env.REPORT_SALT || SERVICE_KEY.slice(0, 24);

const REPORTS_PER_HOUR = 10;
const MAX_WHAT = 4000;
const MIN_WHAT = 8;
const MAX_PHOTO = 4 * 1000 * 1000;
const PHOTO_TYPES = {
  "image/jpeg": "jpg",
  "image/png": "png",
  "image/webp": "webp",
};
const DEVICES = ["x4pro", "sticky"];
const VERSION = /^v?\d{1,3}\.\d{1,3}\.\d{1,3}$/;
const EMAIL = /^[^\s@]{1,64}@[^\s@]{1,190}$/;

// Who the report belongs to, which the board records as `reporter` so
// `board list --from-mario` can answer "what have I reported?".
//
// This form is PUBLIC -- no account, linked from the front page, and used by
// strangers -- so a submission is `user` unless the address given is the
// owner's. Stamping every site report as Mario's would put other people's bugs
// on the one list whose whole value is that he can trust it, and a report with
// no address at all is not evidence that he wrote it. `unknown` is therefore
// only what an unstamped path lands on; this one always says something.
const OWNER_EMAIL = (
  process.env.BOARD_OWNER_EMAIL || "marioalejandroruizsarmiento@gmail.com"
)
  .trim()
  .toLowerCase();

function reporterFor(email) {
  return email && email.trim().toLowerCase() === OWNER_EMAIL ? "mario" : "user";
}

function json(res, status, body, extra) {
  res.statusCode = status;
  res.setHeader("Content-Type", "application/json");
  res.setHeader("Cache-Control", "no-store");
  for (const [k, v] of Object.entries(extra || {})) res.setHeader(k, v);
  res.end(JSON.stringify(body));
}

async function readBody(req, limit) {
  if (req.body !== undefined && req.body !== null) {
    if (Buffer.isBuffer(req.body)) return req.body;
    if (typeof req.body === "string") return Buffer.from(req.body);
    return Buffer.from(JSON.stringify(req.body));
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
  const fwd = req.headers["x-forwarded-for"];
  if (typeof fwd === "string" && fwd.length) return fwd.split(",")[0].trim();
  return (req.socket && req.socket.remoteAddress) || "0.0.0.0";
}

function reporterHash(req) {
  return crypto
    .createHash("sha256")
    .update(`${clientIp(req)}|${SALT}`)
    .digest("hex");
}

async function rest(path, init) {
  const headers = Object.assign(
    {
      apikey: SERVICE_KEY,
      Authorization: `Bearer ${SERVICE_KEY}`,
      "Content-Type": "application/json",
      Accept: "application/json",
    },
    (init && init.headers) || {},
  );
  return fetch(
    `${SUPABASE_URL}/rest/v1/${path}`,
    Object.assign({}, init, { headers }),
  );
}

function clean(s, max) {
  return String(s == null ? "" : s)
    .replace(/\r\n?/g, "\n")
    .trim()
    .slice(0, max);
}

// "x4pro", "sticky" or both, in any order and any case, with stray spaces.
// Returns the devices in DEVICES order, or null when the value names anything
// else or nothing at all.
function parseDevices(raw) {
  const picked = new Set(
    clean(raw, 40)
      .toLowerCase()
      .split(",")
      .map((d) => d.trim())
      .filter(Boolean),
  );
  if (!picked.size) return null;
  for (const d of picked) if (!DEVICES.includes(d)) return null;
  return DEVICES.filter((d) => picked.has(d));
}

async function recentCount(hash) {
  const since = new Date(Date.now() - 3600 * 1000).toISOString();
  const r = await rest(
    `cards?select=id&reporter_hash=eq.${hash}&created_at=gte.${encodeURIComponent(since)}`,
    {
      method: "GET",
      headers: { Prefer: "count=exact", Range: "0-0" },
    },
  );
  const range = r.headers.get("content-range") || "";
  const total = parseInt(range.split("/")[1], 10);
  return Number.isFinite(total) ? total : 0;
}

async function createReport(req, res) {
  const raw = await readBody(req, 64 * 1024);
  if (raw === null)
    return json(res, 413, {
      error: "That report is too long. Keep it under 4000 characters.",
    });
  let body;
  try {
    body = JSON.parse(raw.toString("utf8") || "{}");
  } catch (err) {
    return json(res, 400, {
      error: "The report was not readable. Please try again.",
    });
  }

  // The honeypot: a field a person never sees. A bot that fills it gets a
  // polite success and nothing is stored.
  if (clean(body.website, 10).length) return json(res, 200, { id: 0 });

  const what = clean(body.what, MAX_WHAT);
  if (what.length < MIN_WHAT)
    return json(res, 400, { error: "Say a little more about what happened." });
  const kind = body.kind === "idea" ? "feature" : "bug";
  const devices = parseDevices(body.device);
  if (!devices)
    return json(res, 400, {
      error: "Which device? Pick X4 Pro, Sticky or both.",
    });
  const device = devices.join(",");
  const version = clean(body.version, 16);
  if (version && !VERSION.test(version))
    return json(res, 400, {
      error: "The version looks like 1.12.9, three numbers with dots.",
    });
  const email = clean(body.email, 200);
  if (email && !EMAIL.test(email))
    return json(res, 400, { error: "That email does not look right." });
  const app =
    clean(body.app, 40)
      .toLowerCase()
      .replace(/[^a-z0-9 ]/g, "") || "unknown";

  const hash = reporterHash(req);
  if ((await recentCount(hash)) >= REPORTS_PER_HOUR) {
    return json(res, 429, {
      error:
        "That is a lot of reports from one place in an hour. Try again later.",
    });
  }

  const title =
    what.split("\n")[0].replace(/\s+/g, " ").trim().slice(0, 80) ||
    (kind === "bug" ? "A bug report" : "An idea");
  const card = {
    title,
    app,
    kind,
    body: what,
    state: "reported",
    source: "site",
    device,
    version: version || null,
    reporter: reporterFor(email),
    reporter_email: email || null,
    reporter_hash: hash,
  };
  const r = await rest("cards?select=id", {
    method: "POST",
    headers: { Prefer: "return=representation" },
    body: JSON.stringify(card),
  });
  if (!r.ok)
    return json(res, 502, {
      error: "The board did not take the report. Please try again in a minute.",
    });
  const rows = await r.json();
  const id = rows && rows[0] && rows[0].id;
  if (!id)
    return json(res, 502, {
      error: "The board did not say which report this became.",
    });
  await rest("history", {
    method: "POST",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({
      card_id: id,
      what: `reported from the site (${kind}, ${devices.join(" and ")})`,
    }),
  });
  // The report is also an event, so the Numbers page counts reports next to
  // installs and the services' events. A failed count must not fail the report. The
  // board column holds one board; a report about both leaves it empty and
  // says so in props, rather than inventing a third value for that column.
  await rest("events", {
    method: "POST",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({
      service: "site",
      event: "report",
      version: version || null,
      board: devices.length === 1 ? devices[0] : null,
      props: { card: id, kind, devices },
    }),
  }).catch(() => null);
  return json(res, 201, { id });
}

async function attachPhoto(req, res, idParam) {
  const id = parseInt(idParam, 10);
  if (!Number.isInteger(id) || id <= 0)
    return json(res, 400, { error: "Which report is this photo for?" });
  const type = String(req.headers["content-type"] || "")
    .split(";")[0]
    .trim()
    .toLowerCase();
  const ext = PHOTO_TYPES[type];
  if (!ext)
    return json(res, 415, { error: "Photos can be JPEG, PNG or WebP." });
  const declared = parseInt(req.headers["content-length"] || "0", 10);
  if (declared > MAX_PHOTO)
    return json(res, 413, {
      error:
        "That photo is too big. The page resizes photos; try again from it.",
    });
  const bytes = await readBody(req, MAX_PHOTO);
  if (bytes === null || bytes.length > MAX_PHOTO)
    return json(res, 413, {
      error:
        "That photo is too big. The page resizes photos; try again from it.",
    });
  if (bytes.length === 0)
    return json(res, 400, { error: "The photo arrived empty." });

  const look = await rest(
    `cards?id=eq.${id}&select=id,source,photo_path,created_at`,
    { method: "GET" },
  );
  const rows = look.ok ? await look.json() : [];
  const card = rows && rows[0];
  const fresh = card && Date.now() - Date.parse(card.created_at) < 3600 * 1000;
  if (!card || card.source !== "site" || card.photo_path || !fresh) {
    return json(res, 404, {
      error: "No report is waiting for a photo under that number.",
    });
  }

  const path = `photos/${id}.${ext}`;
  const up = await fetch(`${SUPABASE_URL}/storage/v1/object/${path}`, {
    method: "POST",
    headers: {
      apikey: SERVICE_KEY,
      Authorization: `Bearer ${SERVICE_KEY}`,
      "Content-Type": type,
      "x-upsert": "false",
    },
    body: bytes,
  });
  if (!up.ok)
    return json(res, 502, {
      error: "The photo did not upload. The report itself is saved.",
    });
  await rest(`cards?id=eq.${id}`, {
    method: "PATCH",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({ photo_path: path }),
  });
  await rest("history", {
    method: "POST",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({ card_id: id, what: "photo attached from the site" }),
  });
  return json(res, 200, { ok: true });
}

module.exports = async function handler(req, res) {
  if (!SUPABASE_URL || !SERVICE_KEY) {
    return json(res, 503, {
      error: "The report box is not set up on this deployment.",
    });
  }
  const url = new URL(req.url, "http://localhost");
  if (req.method === "POST") return createReport(req, res);
  if (req.method === "PUT" && url.searchParams.has("photo"))
    return attachPhoto(req, res, url.searchParams.get("photo"));
  return json(
    res,
    405,
    { error: "Send a report with POST, or a photo with PUT ?photo=<id>." },
    { Allow: "POST, PUT" },
  );
};
