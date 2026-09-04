// The inbox function, run under plain node with the board stubbed out.
//
// api/inbox.js is the gate between a passphrase and the board's write key,
// so the gate is what is asserted: a wrong passphrase reads nothing, a
// right one reads, an answer writes exactly one blocker closed.
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

  console.log(`${pass + fail} checks, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => {
  console.log("  FAIL harness crashed: " + e.stack);
  process.exit(1);
});
