// POST /api/inbox   {pass, op, ...}
//
// The inbox page's one door to the board. A passphrase instead of an email
// link: the page sends it with every call, this checks its hash against the
// INBOX_PASSPHRASE_HASH environment variable (sha256, hex), and only then
// reads or writes the board with the service key. The passphrase never
// reaches the board and is never stored anywhere but the reader's browser.
//
// Change the passphrase: printf '%s' 'new one' | shasum -a 256, then set
// INBOX_PASSPHRASE_HASH on Vercel to the hex and redeploy.
//
// Operations:
//   list     -> {inbox: [open blockers that need Mario, with their card], cards: [every card]}
//   numbers  -> {byVersion, daily, battery, services, errors, pulse, weekly, dwell, latency, byApp}
//   answer   -> closes one blocker: {card_id, n, choice, note}

const crypto = require("node:crypto");

const SUPABASE_URL = process.env.SUPABASE_URL || "";
const SERVICE_KEY = process.env.SUPABASE_SERVICE_ROLE_KEY || "";
const PASS_HASH = (process.env.INBOX_PASSPHRASE_HASH || "")
  .trim()
  .toLowerCase();

function json(res, status, body) {
  res.statusCode = status;
  res.setHeader("Content-Type", "application/json");
  res.setHeader("Cache-Control", "no-store");
  res.end(JSON.stringify(body));
}

async function readBody(req) {
  if (req.body !== undefined && req.body !== null) {
    if (Buffer.isBuffer(req.body)) return req.body.toString("utf8");
    if (typeof req.body === "string") return req.body;
    return JSON.stringify(req.body);
  }
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > 64 * 1024) return null;
    chunks.push(chunk);
  }
  return Buffer.concat(chunks).toString("utf8");
}

function passOk(pass) {
  if (!PASS_HASH || typeof pass !== "string" || !pass.length) return false;
  const got = crypto.createHash("sha256").update(pass).digest("hex");
  if (got.length !== PASS_HASH.length) return false;
  return crypto.timingSafeEqual(Buffer.from(got), Buffer.from(PASS_HASH));
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
  const r = await fetch(
    `${SUPABASE_URL}/rest/v1/${path}`,
    Object.assign({}, init, { headers }),
  );
  if (!r.ok) throw new Error(`board ${r.status} on ${path.split("?")[0]}`);
  const text = await r.text();
  return text ? JSON.parse(text) : null;
}

async function opList() {
  const [inbox, cards] = await Promise.all([
    rest("inbox?select=*"),
    rest("cards?select=id,title,app,state,parent,updated_at&order=id.desc"),
  ]);
  return { inbox: inbox || [], cards: cards || [] };
}

async function opNumbers() {
  const q = (p) => rest(p).catch(() => []);
  const [
    byVersion,
    daily,
    battery,
    services,
    errors,
    pulse,
    weekly,
    dwell,
    latency,
    byApp,
  ] = await Promise.all([
    q("devices_by_version?select=*"),
    q("daily_active_devices?select=*"),
    q("battery_by_version?select=*"),
    q("service_users?select=*"),
    q(
      "error_fingerprints?select=service,message,count,last_seen,card_id&order=last_seen.desc&limit=20",
    ),
    q("pulse_hosts?select=*"),
    q("workflow_weekly?select=*"),
    q("state_dwell?select=*"),
    q("inbox_latency?select=*"),
    q("open_cards_by_app?select=*"),
  ]);
  return {
    byVersion,
    daily,
    battery,
    services,
    errors,
    pulse,
    weekly,
    dwell,
    latency: (latency || [])[0] || null,
    byApp,
  };
}

async function opAnswer(body) {
  const cardId = parseInt(body.card_id, 10);
  const n = parseInt(body.n, 10);
  const choice = String(body.choice || "")
    .trim()
    .slice(0, 500);
  const note = String(body.note || "")
    .trim()
    .slice(0, 2000);
  if (!Number.isInteger(cardId) || !Number.isInteger(n) || !choice)
    throw new Error("card, blocker and a choice are needed");
  await rest(`blockers?card_id=eq.${cardId}&n=eq.${n}&open=is.true`, {
    method: "PATCH",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({
      open: false,
      answer_choice: choice,
      answer_note: note,
      answered_at: new Date().toISOString(),
    }),
  });
  await rest("history", {
    method: "POST",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({
      card_id: cardId,
      what: `answered from the inbox: ${choice}`,
    }),
  });
  // "Tell me how": the card goes back to its owner with Mario's words, as an
  // info blocker the orchestrator routes; it returns to the inbox with steps.
  if (choice === "needs-steps") {
    const existing = await rest(`blockers?card_id=eq.${cardId}&select=n`);
    const next = 1 + Math.max(0, ...(existing || []).map((b) => b.n));
    await rest("blockers", {
      method: "POST",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify({
        card_id: cardId,
        n: next,
        need: "info",
        by_session: "mario",
        ask: `Mario needs the steps before he can do this${note ? ": " + note : ""}. Write them (numbered, one per line) and re-ask him with --steps.`,
        default: "The card waits until the steps come back.",
      }),
    });
  }
  return { ok: true };
}

module.exports = async function handler(req, res) {
  if (req.method !== "POST") return json(res, 405, { error: "POST only." });
  if (!SUPABASE_URL || !SERVICE_KEY || !PASS_HASH)
    return json(res, 503, {
      error: "The inbox is not set up on this deployment.",
    });
  const raw = await readBody(req);
  let body;
  try {
    body = JSON.parse(raw || "{}");
  } catch (err) {
    return json(res, 400, { error: "Unreadable request." });
  }
  if (!passOk(body.pass)) {
    await new Promise((r) => setTimeout(r, 400));
    return json(res, 401, { error: "That is not the passphrase." });
  }
  try {
    if (body.op === "list") return json(res, 200, await opList());
    if (body.op === "numbers") return json(res, 200, await opNumbers());
    if (body.op === "answer") return json(res, 200, await opAnswer(body));
    return json(res, 400, { error: "Unknown operation." });
  } catch (err) {
    return json(res, 502, {
      error: err.message || "The board did not answer.",
    });
  }
};
