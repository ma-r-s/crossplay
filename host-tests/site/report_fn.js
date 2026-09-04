// The report function, run under plain node with the board stubbed out.
//
// api/report.js is the one place a stranger's input meets the board's write
// key, so every refusal it makes is asserted here, and so is the one thing it
// must never do: store a report whose honeypot was filled. The stub records
// every call it receives, which is how "nothing was stored" is checked rather
// than assumed.
//
//   node host-tests/site/report_fn.js <repo-root>

const path = require("node:path");

const root = process.argv[2] || path.join(__dirname, "..", "..");
process.env.SUPABASE_URL = "https://board.test";
process.env.SUPABASE_SERVICE_ROLE_KEY = "service-key-for-tests";
const handler = require(path.join(root, "site", "api", "report.js"));

let calls = [];
let recent = 0;
global.fetch = async function (url, opts) {
  opts = opts || {};
  calls.push({
    url: String(url),
    method: opts.method || "GET",
    body: opts.body,
    headers: opts.headers || {},
  });
  const u = String(url);
  if (u.includes("reporter_hash=eq.")) {
    return new Response("[]", {
      status: 200,
      headers: { "content-range": `0-0/${recent}` },
    });
  }
  if (u.endsWith("/rest/v1/cards?select=id") && opts.method === "POST") {
    return new Response(JSON.stringify([{ id: 42 }]), { status: 201 });
  }
  if (u.includes("/rest/v1/cards?id=eq.42&select=")) {
    return new Response(
      JSON.stringify([
        {
          id: 42,
          source: "site",
          photo_path: null,
          created_at: new Date().toISOString(),
        },
      ]),
      { status: 200 },
    );
  }
  if (u.includes("/rest/v1/cards?id=eq.7&select=")) {
    return new Response(
      JSON.stringify([
        {
          id: 7,
          source: "session",
          photo_path: null,
          created_at: new Date().toISOString(),
        },
      ]),
      { status: 200 },
    );
  }
  if (u.includes("/storage/v1/object/"))
    return new Response("{}", { status: 200 });
  return new Response(null, { status: 204 });
};

function fakeReq(method, body, query, headers) {
  const h = Object.assign({ "x-forwarded-for": "203.0.113.9" }, headers || {});
  return {
    method,
    url: "/api/report" + (query || ""),
    headers: h,
    body,
    socket: { remoteAddress: "127.0.0.1" },
  };
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
async function call(method, body, query, headers) {
  const res = fakeRes();
  await handler(fakeReq(method, body, query, headers), res);
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
  const good = {
    kind: "bug",
    what: "The page turn takes four seconds on the X4 Pro since 1.12.2.",
    device: "x4pro",
    version: "1.12.9",
    email: "",
  };

  let r = await call("POST", good);
  expect("a good report is created", r.status, 201);
  expect("and answers with its id", r.json && r.json.id, 42);
  const ev = calls.find(
    (c) => c.url.endsWith("/rest/v1/events") && c.method === "POST",
  );
  expect(
    "and posts one site/report event",
    ev ? JSON.parse(ev.body).event : null,
    "report",
  );
  expect(
    "naming the card it became",
    ev ? JSON.parse(ev.body).props.card : null,
    42,
  );
  const insert = calls.find((c) => c.url.endsWith("/rest/v1/cards?select=id"));
  const row = insert ? JSON.parse(insert.body) : {};
  expect(
    "stored as a bug from the site",
    `${row.kind}/${row.source}/${row.device}`,
    "bug/site/x4pro",
  );
  expect(
    "title is the first line",
    row.title,
    "The page turn takes four seconds on the X4 Pro since 1.12.2.",
  );
  expect("no email means null, not an empty string", row.reporter_email, null);
  expect(
    "the address is stored hashed, not raw",
    typeof row.reporter_hash === "string" &&
      row.reporter_hash.length === 64 &&
      !row.reporter_hash.includes("203.0"),
    true,
  );

  calls = [];
  r = await call(
    "POST",
    Object.assign({}, good, { kind: "idea", website: "http://spam.example" }),
  );
  expect("a filled honeypot gets a polite 200", r.status, 200);
  expect("and stores nothing", calls.length, 0);

  r = await call("POST", { kind: "bug", what: "short", device: "x4pro" });
  expect("too little text is refused", r.status, 400);
  r = await call("POST", Object.assign({}, good, { version: "latest" }));
  expect("a version that is not three numbers is refused", r.status, 400);
  r = await call("POST", Object.assign({}, good, { email: "not an email" }));
  expect("a bad email is refused", r.status, 400);
  r = await call(
    "POST",
    Object.assign({}, good, { kind: "idea", device: "toaster" }),
  );
  calls = [];
  r = await call(
    "POST",
    Object.assign({}, good, { kind: "idea", device: "toaster" }),
  );
  const row2 = JSON.parse(
    calls.find((c) => c.url.endsWith("/rest/v1/cards?select=id")).body,
  );
  expect("an idea is a feature card", row2.kind, "feature");
  expect("an unknown device is 'unknown'", row2.device, "unknown");

  recent = 10;
  r = await call("POST", good);
  expect("the eleventh report in an hour is refused", r.status, 429);
  recent = 0;

  r = await call("GET", undefined);
  expect("GET is not a way in", r.status, 405);

  calls = [];
  r = await call("PUT", Buffer.from("jpegbytes"), "?photo=42", {
    "content-type": "image/jpeg",
    "content-length": "9",
  });
  expect("a photo for a fresh site card is stored", r.status, 200);
  const up = calls.find((c) => c.url.includes("/storage/v1/object/"));
  expect(
    "under the card's number",
    up ? up.url.endsWith("/storage/v1/object/photos/42.jpg") : false,
    true,
  );
  const patch = calls.find(
    (c) => c.method === "PATCH" && c.url.includes("cards?id=eq.42"),
  );
  expect(
    "and the card points at it",
    patch ? JSON.parse(patch.body).photo_path : null,
    "photos/42.jpg",
  );

  r = await call("PUT", Buffer.from("x"), "?photo=42", {
    "content-type": "text/plain",
    "content-length": "1",
  });
  expect("a non-image is refused", r.status, 415);
  r = await call("PUT", Buffer.from("x"), "?photo=42", {
    "content-type": "image/png",
    "content-length": String(5 * 1000 * 1000),
  });
  expect("an oversize photo is refused", r.status, 413);
  r = await call("PUT", Buffer.from("x"), "?photo=7", {
    "content-type": "image/png",
    "content-length": "1",
  });
  expect(
    "a photo for a card the site did not create is refused",
    r.status,
    404,
  );

  console.log(`${pass + fail} checks, ${fail} failed`);
  process.exit(fail ? 1 : 0);
})().catch((e) => {
  console.log("  FAIL harness crashed: " + e.stack);
  process.exit(1);
});
