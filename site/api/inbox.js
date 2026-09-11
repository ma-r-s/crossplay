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
//   list     -> {inbox: [open blockers that need Mario, with their card], cards: [every card],
//                people: [reports from people he has not read],
//                triage: {waiting, claimed, for_mario, oldest_h, last_triaged_at, since_triage_h} or null}
//   numbers  -> {heard, fresh, now, devices, field, crashes, byVersion, daily, battery, services, errors, pulse, weekly, dwell, latency, byApp}
//   answer   -> closes one blocker: {card_id, n, choice, note}
//   seen     -> marks one report from a person as read: {card_id, note}

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

// A photo lives in the board's private bucket. A link the browser can open
// is a signed URL, good for an hour, made here with the service key so the
// key never reaches the page. No link when the bucket will not sign.
async function photoUrl(path) {
  try {
    const r = await fetch(`${SUPABASE_URL}/storage/v1/object/sign/${path}`, {
      method: "POST",
      headers: {
        apikey: SERVICE_KEY,
        Authorization: `Bearer ${SERVICE_KEY}`,
        "Content-Type": "application/json",
      },
      body: JSON.stringify({ expiresIn: 3600 }),
    });
    if (!r.ok) return null;
    const j = await r.json();
    return j && j.signedURL ? `${SUPABASE_URL}/storage/v1${j.signedURL}` : null;
  } catch (e) {
    return null;
  }
}

async function opList() {
  const [inbox, cards, people, triage] = await Promise.all([
    rest("inbox?select=*"),
    rest("cards?select=id,title,app,state,parent,updated_at&order=id.desc"),
    // What people reported through the site and he has not read. Not blockers,
    // and above them on the page: a stranger's report is rare and worth
    // interrupting for, a session's routine ask is not.
    //
    // This read is NOT allowed to fail quietly. The first version caught the
    // error and returned [], which is the original bug rebuilt: an inbox that
    // shows no reports and says nothing about why is exactly what he asked to
    // have fixed, and a dropped migration would have looked like a quiet week.
    // The error is carried instead, and the page prints it where the reports
    // would be. `triage` below still degrades, because that is one decorative
    // line and this is the content.
    rest(
      "reports_from_people?mario_seen_at=is.null&select=*&order=created_at.asc",
    ).then(
      (rows) => ({ rows: rows || [] }),
      (err) => ({ rows: [], error: err.message || "the board did not answer" }),
    ),
    // How far behind triage is, for one line at the top of the page. A board
    // without the view (or a failing read) leaves the line out; the inbox
    // itself must not depend on it.
    rest("triage_backlog?select=*").catch(() => []),
  ]);
  // Mario, 2026-09-11: "I need a way to see everything they sent, including
  // images." The photo rides as a signed link, made here so the key stays
  // here; a report without one gets null and the page shows no picture.
  const rows = await Promise.all(
    (people.rows || []).map(async (r) =>
      Object.assign({}, r, { photo_url: r.photo_path ? await photoUrl(r.photo_path) : null }),
    ),
  );
  return {
    inbox: inbox || [],
    cards: cards || [],
    people: rows,
    people_error: people.error || null,
    triage: (triage || [])[0] || null,
  };
}

async function opNumbers() {
  const q = (p) => rest(p).catch(() => []);
  const [
    heard,
    fresh,
    now,
    devices,
    field,
    crashes,
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
    // The owner's facts first (20260910000200_owner_views.sql): distinct
    // devices per window, growth, what runs right now with each device once,
    // every device once, and whether the firmware hurt anyone this week.
    q("devices_heard_from?select=*"),
    q("devices_new?select=*"),
    q("versions_now?select=*"),
    q("devices_now?select=*"),
    q("field_7d?select=*"),
    q("crashes_7d?select=*"),
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
    heard: (heard || [])[0] || null,
    fresh: (fresh || [])[0] || null,
    now,
    devices,
    field: (field || [])[0] || null,
    crashes,
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

// He read it, and if he said what should happen that sentence has to land
// where triage looks. History is not that place: no view selects it, no
// command surfaces it, no step of the orchestrator's runbook visits it. A note
// filed only there swaps "he never sees the report" for "he sees it, writes
// down what to do, and nobody ever reads it". So the note is appended to the
// card's BODY, and a card he has answered moves out of `reported` -- it no
// longer needs a triager to decide what it is. With no note he has read it and
// said nothing, so it stays where it was for the ordinary sweep.
//
// Mirrors `board seen` in tools_local/board/board.py; the prefix is spelled
// the same in both, and host-tests/bugflow checks that it is.
const MARIO_SAID = "Mario, on reading this:";

async function opSeen(body) {
  const cardId = parseInt(body.card_id, 10);
  const note = String(body.note || "")
    .trim()
    .slice(0, 2000)
    .replace(/\s+/g, " ");
  if (!Number.isInteger(cardId)) throw new Error("which report?");
  // reporter=eq.user is the guard, not a filter for convenience: mario_seen_at
  // means "Mario has read this person's report", and setting it on one of our
  // own cards would put a fact on the board that nothing else could explain.
  // It is on the read AND on the write, so neither half can be reached alone.
  const found = await rest(
    `cards?id=eq.${cardId}&reporter=eq.user&select=id,body,state`,
  );
  if (!found || !found.length)
    throw new Error("that card is not a report from a person");
  const card = found[0];
  const patch = { mario_seen_at: new Date().toISOString() };
  if (note) {
    patch.body =
      ((card.body || "").replace(/\s+$/, "") + (card.body ? "\n\n" : "")) +
      `${MARIO_SAID} ${note}`;
    if (card.state === "reported") patch.state = "triaged";
  }
  const rows = await rest(`cards?id=eq.${cardId}&reporter=eq.user&select=id`, {
    method: "PATCH",
    headers: { Prefer: "return=representation" },
    body: JSON.stringify(patch),
  });
  if (!rows || !rows.length)
    throw new Error("that card is not a report from a person");
  await rest("history", {
    method: "POST",
    headers: { Prefer: "return=minimal" },
    body: JSON.stringify({
      card_id: cardId,
      what: "Mario read the report" + (note ? `: ${note}` : ""),
    }),
  });
  if (note && patch.state)
    await rest("history", {
      method: "POST",
      headers: { Prefer: "return=minimal" },
      body: JSON.stringify({ card_id: cardId, what: "state triaged" }),
    });
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
    if (body.op === "seen") return json(res, 200, await opSeen(body));
    return json(res, 400, { error: "Unknown operation." });
  } catch (err) {
    return json(res, 502, {
      error: err.message || "The board did not answer.",
    });
  }
};
