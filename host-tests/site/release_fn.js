// release.js names the version the Install button installs. It asks GitHub
// from the visitor's browser, and when that fails it asks the site's own
// /api/latest, which serves its last good copy when GitHub refuses it. Card
// #585 was an install ended by "Could not reach GitHub" while the site held a
// good answer. The page's own file is run here against a stubbed fetch.
//
//   node host-tests/site/release_fn.js <repo root>
const fs = require("fs");
const path = require("path");
const src = fs.readFileSync(path.join(process.argv[2], "site/assets/release.js"), "utf8");

let failed = 0;
function check(label, got, want) {
  if (got === want) console.log("  ok   " + label);
  else { failed++; console.log("  FAIL " + label + " (got " + JSON.stringify(got) + ", wanted " + JSON.stringify(want) + ")"); }
}

// One fresh page per case: the module keeps one promise per load, by design.
function page(answers) {
  const asked = [];
  const window = {};
  const fetch = (url) => {
    asked.push(url);
    const a = answers[url.startsWith("https://api.github.com") ? "github" : url];
    if (a === "throw") return Promise.reject(new TypeError("Failed to fetch"));
    if (!a) return Promise.resolve({ ok: false, status: 404, json: async () => ({}) });
    return Promise.resolve({ ok: a.status === 200, status: a.status, json: async () => a.body });
  };
  new Function("window", "fetch", src)(window, fetch);
  return { latest: window.crossplayLatestRelease, asked };
}
const good = (tag) => ({ status: 200, body: { tag_name: tag } });

(async () => {
  let p = page({ github: good("v9.9.9") });
  check("GitHub answering is the answer", ((await p.latest()) || {}).tag_name, "v9.9.9");
  check("and the site's proxy is not asked", p.asked.length, 1);

  p = page({ github: { status: 403, body: { message: "API rate limit exceeded" } }, "/api/latest": good("v9.9.8") });
  check("a rate-limited visitor gets the site's copy", ((await p.latest()) || {}).tag_name, "v9.9.8");
  check("asking GitHub first, the proxy second", p.asked.join(" then "), "https://api.github.com/repos/ma-r-s/crossplay/releases/latest then /api/latest");

  p = page({ github: "throw", "/api/latest": good("v9.9.7") });
  check("an unreachable GitHub gets the site's copy", ((await p.latest()) || {}).tag_name, "v9.9.7");

  p = page({ github: "throw", "/api/latest": { status: 502, body: {} } });
  check("both down is null, never a rejection", await p.latest(), null);

  p = page({ github: { status: 200, body: { message: "Not Found" } }, "/api/latest": good("v9.9.6") });
  check("a 200 with no tag is not an answer", ((await p.latest()) || {}).tag_name, "v9.9.6");

  p = page({ github: good("v9.9.9") });
  const [a, b] = [p.latest(), p.latest()];
  check("two callers on one load share one request", a === b && p.asked.length === 1, true);

  console.log(failed ? `release_fn: ${failed} failed` : "release_fn: all passed");
  process.exit(failed ? 1 : 0);
})();
