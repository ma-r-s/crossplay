// The inbox function, run under plain node with the board stubbed out.
//
// api/inbox.js is the gate between a passphrase and the board's write key,
// so the gate is what is asserted: a wrong passphrase reads nothing, a
// right one reads, an answer writes exactly one blocker closed, and marking a
// person's report read is SCOPED to a person's report -- mario_seen_at means
// "Mario has read this stranger's report", and the filter that keeps it true
// is invisible in the code and only a test can hold it.
//
//   node host-tests/site/inbox_fn.js <repo-root>

const path = require("node:path");
const crypto = require("node:crypto");

const root = process.argv[2] || path.join(__dirname, "..", "..");
process.env.SUPABASE_URL = "https://board.test";
process.env.SUPABASE_SERVICE_ROLE_KEY = "service-key-for-tests";
process.env.INBOX_PASSPHRASE_HASH = crypto
  .createHash("sha256")
  .update("open sesame")
  .digest("hex");
const handler = require(path.join(root, "site", "api", "inbox.js"));

let calls = [];
global.fetch = async function (url, opts) {
  opts = opts || {};
  calls.push({
    url: String(url),
    method: opts.method || "GET",
    body: opts.body,
  });
  const u = String(url);
  if (u.includes("/rest/v1/inbox"))
    return new Response(
      JSON.stringify([
        {
          blocker_id: 5,
          n: 1,
          card_id: 3,
          title: "Use Instapaper once",
          app: "instapaper",
          body: "since: proven",
          ask: "did it work?",
          default: "unverified",
        },
      ]),
      { status: 200 },
    );
  if (u.includes("/rest/v1/reports_from_people"))
    return new Response(
      JSON.stringify([
        {
          id: 9,
          title: "the sync server never sent a code",
          body: "the sync server never sent a code",
          app: "instapaper",
          kind: "bug",
          state: "reported",
          device: "sticky",
          version: "1.12.11",
          reporter_email: "someone@example.net",
          photo_path: null,
          created_at: "2026-09-06T09:51:36Z",
          mario_seen_at: null,
        },
      ]),
      { status: 200 },
    );
  // The board, not the function, is what enforces reporter=eq.user: a PATCH
  // carrying that filter matches card 9 and nothing else, exactly as postgres
  // would. A function that dropped the filter therefore fails here rather than
  // succeeding against a stub that answers every id.
  if (u.includes("/rest/v1/cards") && opts.method === "PATCH")
    return new Response(
      JSON.stringify(
        u.includes("id=eq.9") && u.includes("reporter=eq.user")
          ? [{ id: 9 }]
          : [],
      ),
      { status: 200 },
    );
  if (u.includes("/rest/v1/cards"))
    return new Response(
      JSON.stringify([
        {
          id: 3,
          title: "Use Instapaper once",
          app: "instapaper",
          state: "reported",
          parent: null,
        },
      ]),
      { status: 200 },
    );
  if (u.includes("/rest/v1/triage_backlog"))
    return new Response(
      JSON.stringify([{ waiting: 2, claimed: 0, for_mario: 1, oldest_h: 30, last_triaged_at: null, since_triage_h: null }]),
      { status: 200 },
    );
  if (u.includes("/rest/v1/blockers") && opts.method === "PATCH")
    return new Response(null, { status: 204 });
  if (u.includes("/rest/v1/history"))
    return new Response(null, { status: 201 });
  return new Response("[]", { status: 200 });
};

function fakeReq(body) {
  return { method: "POST", url: "/api/inbox", headers: {}, body, socket: {} };
}
function fakeRes() {
  const res = { statusCode: 200, headers: {}, body: "" };
  res.setHeader = (k, v) => {
    res.headers[k.toLowerCase()] = v;
  };
  res.end = (b) => {
    res.body = b || "";
    res.done();
  };
  res.finished = new Promise((r) => {
    res.done = r;
  });
  return res;
}
async function call(body) {
  const res = fakeRes();
  await handler(fakeReq(body), res);
  await res.finished;
  let j = null;
  try {
    j = JSON.parse(res.body);
  } catch (e) {}
  return { status: res.statusCode, json: j };
}

let pass = 0,
  fail = 0;
const ok = (m) => {
  pass++;
  console.log("  ok   " + m);
};
const bad = (m) => {
  fail++;
  console.log("  FAIL " + m);
};
const expect = (label, got, want) =>
  got === want
    ? ok(label)
    : bad(
        `${label} (got ${JSON.stringify(got)}, wanted ${JSON.stringify(want)})`,
      );

(async () => {
  calls = [];
  let r = await call({ pass: "wrong", op: "list" });
  expect("a wrong passphrase is refused", r.status, 401);
  expect("and reads nothing from the board", calls.length, 0);
  r = await call({ op: "list" });
  expect("no passphrase is refused", r.status, 401);

  calls = [];
  r = await call({ pass: "open sesame", op: "list" });
  expect("the right passphrase reads the inbox", r.status, 200);
  expect(
    "with the open blockers",
    r.json && r.json.inbox && r.json.inbox.length,
    1,
  );
  expect("and every card", r.json && r.json.cards && r.json.cards.length, 1);
  expect(
    "and what people reported and he has not read",
    r.json && r.json.people && r.json.people[0] && r.json.people[0].id,
    9,
  );
  expect(
    "unread only, oldest first",
    calls.some(
      (c) =>
        c.url.includes("reports_from_people") &&
        c.url.includes("mario_seen_at=is.null") &&
        c.url.includes("order=created_at.asc"),
    ),
    true,
  );

  // Marking a report read is the only thing that takes it out of the one place
  // Mario looks, so it must touch nothing else: no state change, no blocker.
  calls = [];
  r = await call({
    pass: "open sesame",
    op: "seen",
    card_id: 9,
    note: "replied; file it against the bridge",
  });
  expect("a report can be marked read", r.status, 200);
  const seen = calls.find(
    (c) => c.method === "PATCH" && c.url.includes("/rest/v1/cards"),
  );
  expect(
    "scoped to a card a person reported",
    seen ? seen.url.includes("reporter=eq.user") : false,
    true,
  );
  expect(
    "writing only when he read it",
    seen ? Object.keys(JSON.parse(seen.body)).join(",") : null,
    "mario_seen_at",
  );
  expect(
    "his note goes on the card, once",
    calls.filter(
      (c) =>
        c.url.includes("/rest/v1/history") &&
        JSON.parse(c.body).what.includes("replied; file it against the bridge"),
    ).length,
    1,
  );
  expect(
    "and no blocker is opened or closed",
    calls.filter((c) => c.url.includes("/rest/v1/blockers")).length,
    0,
  );
  r = await call({ pass: "open sesame", op: "seen", card_id: 3 });
  expect("a card no person reported cannot be marked read", r.status, 502);
  r = await call({ pass: "open sesame", op: "seen" });
  expect("and neither can no card at all", r.status, 502);

  calls = [];
  r = await call({
    pass: "open sesame",
    op: "answer",
    card_id: 3,
    n: 1,
    choice: "it worked",
    note: "",
  });
  expect("an answer succeeds", r.status, 200);
  const patch = calls.find(
    (c) => c.method === "PATCH" && c.url.includes("/rest/v1/blockers"),
  );
  expect(
    "closes exactly the named blocker",
    patch ? patch.url.includes("card_id=eq.3&n=eq.1&open=is.true") : false,
    true,
  );
  expect(
    "with the choice on it",
    patch ? JSON.parse(patch.body).answer_choice : null,
    "it worked",
  );
  expect(
    "and writes one history line",
    calls.filter((c) => c.url.includes("/rest/v1/history")).length,
    1,
  );

  calls = [];
  r = await call({
    pass: "open sesame",
    op: "answer",
    card_id: 3,
    n: 1,
    choice: "needs-steps",
    note: "where is the tunnel token",
  });
  expect("tell-me-how closes the ask", r.status, 200);
  const bounce = calls.find(
    (c) => c.method === "POST" && c.url.includes("/rest/v1/blockers"),
  );
  expect(
    "and opens an info blocker for the owner",
    bounce ? JSON.parse(bounce.body).need : null,
    "info",
  );
  expect(
    "carrying Mario's words",
    bounce
      ? JSON.parse(bounce.body).ask.includes("where is the tunnel token")
      : false,
    true,
  );

  r = await call({ pass: "open sesame", op: "answer", card_id: 3 });
  expect("an answer without a choice is refused", r.status, 502);
  r = await call({ pass: "open sesame", op: "nonsense" });
  expect("an unknown operation is refused", r.status, 400);

  calls = [];
  r = await call({ pass: "open sesame", op: "numbers" });
  expect("numbers answers", r.status, 200);
  [
    "devices_by_version",
    "daily_active_devices",
    "battery_by_version",
    "pulse_hosts",
    "workflow_weekly",
    "state_dwell",
    "inbox_latency",
    "open_cards_by_app",
  ].forEach(function (v) {
    expect(
      "and reads " + v,
      calls.some(function (c) {
        return c.url.includes("/rest/v1/" + v);
      }),
      true,
    );
  });
  expect(
    "with the pulse in the answer",
    Array.isArray(r.json && r.json.pulse),
    true,
  );
  expect(
    "and the battery table",
    Array.isArray(r.json && r.json.battery),
    true,
  );

  calls = [];
  r = await call({ pass: "open sesame", op: "list" });
  expect(
    "the list reads the triage backlog",
    calls.some(function (c) { return c.url.includes("/rest/v1/triage_backlog"); }),
    true,
  );
  expect("and carries it", r.json && r.json.triage && r.json.triage.waiting, 2);

  console.log(`${pass + fail} checks, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => {
  console.log("  FAIL harness crashed: " + e.stack);
  process.exit(1);
});
