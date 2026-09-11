"""Drive the Wikipedia page end to end in Chrome, against the mock pack.

plan.js is pinned under node; this is the other half. The page's folder picker
cannot be automated (it is a native dialog), so the check replaces
showDirectoryPicker with one that hands back the browser's own private
filesystem (OPFS), which is a real FileSystemDirectoryHandle with real
createWritable() streams. Everything after the picker runs unchanged: the
.crosspoint check, the scan, the copy, the hashing, the markers, the manifest.

Every state the page can reach is driven, asserted and photographed:
the fresh page, the wrong folder, a copy in progress, done on each route, a
second visit that skips what is there, the twenty-minute pause on both routes,
a lost connection with Resume, a part that arrives damaged twice, and the
by-hand page a browser without a picker gets.

Needs serve.py running (COOP/COEP; OPFS is fine either way) and the mock
pack built (site/wikipedia/mock/make_mock.py).

    python3 site/serve.py 8123 &
    uv run --with playwright python site/wikipedia/tests/flow.py http://127.0.0.1:8123 /tmp/wk-shots

Prints one line per check and exits non-zero on the first failure or on any
console error the page logs.
"""

import pathlib
import sys
import time

from playwright.sync_api import sync_playwright

if len(sys.argv) < 3:
    sys.exit("usage: flow.py <base-url> <outdir>")
BASE = sys.argv[1].rstrip("/")
OUT = pathlib.Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)
PAGE = BASE + "/wikipedia/"

# The OPFS root stands in for the card. `.crosspoint` is what the page checks.
CARD = """
window.showDirectoryPicker = async () => {
  const root = await navigator.storage.getDirectory();
  await root.getDirectoryHandle('.crosspoint', { create: true });
  return root;
};
"""
# A folder that is not the card: a plain subdirectory with nothing in it.
NOT_CARD = """
window.showDirectoryPicker = async () => {
  const root = await navigator.storage.getDirectory();
  return root.getDirectoryHandle('downloads', { create: true });
};
"""
# What Safari and Firefox look like to the page.
NO_PICKER = """
try { delete Window.prototype.showDirectoryPicker; } catch (e) {}
try { delete window.showDirectoryPicker; } catch (e) {}
"""
# What is on the "card" afterwards: every file under wikipedia/ with its size.
LIST_CARD = """
async () => {
  const root = await navigator.storage.getDirectory();
  let wiki;
  try { wiki = await root.getDirectoryHandle('wikipedia'); } catch (e) { return {}; }
  const out = {};
  async function walk(dir, prefix) {
    for await (const [name, h] of dir.entries()) {
      if (h.kind === 'directory') await walk(h, prefix + name + '/');
      else out[prefix + name] = (await h.getFile()).size;
    }
  }
  await walk(wiki, '');
  return out;
}
"""
READ_MARKERS = """
async () => {
  const root = await navigator.storage.getDirectory();
  const wiki = await root.getDirectoryHandle('wikipedia');
  const h = await wiki.getFileHandle('copied.json');
  return JSON.parse(await (await h.getFile()).text());
}
"""

checks = 0
failed = 0


def ok(what):
    global checks
    checks += 1
    print("  ok   " + what)


def bad(what):
    global checks, failed
    checks += 1
    failed += 1
    print("  FAIL " + what)


def expect(cond, what):
    (ok if cond else bad)(what)


def shot(page, name, width=1440, full=True):
    page.set_viewport_size({"width": width, "height": 1000})
    page.wait_for_timeout(250)
    path = OUT / f"{name}.png"
    page.screenshot(path=str(path), full_page=full)
    print("  shot " + str(path))


def open_page(browser, stub, query="?mock=1", scheme="light"):
    ctx = browser.new_context(
        viewport={"width": 1440, "height": 1000}, color_scheme=scheme
    )
    ctx.add_init_script(stub)
    page = ctx.new_page()
    errors = []
    page.on(
        "console",
        lambda m: (
            errors.append(m.type + ": " + m.text + " @ " + (m.location or {}).get("url", ""))
            if m.type == "error"
            else None
        ),
    )
    page.on("pageerror", lambda e: errors.append("pageerror: " + str(e)))
    page.goto(PAGE + query, wait_until="networkidle")
    page.wait_for_function(
        "!document.getElementById('pickBtn').disabled || !document.getElementById('unsupported').hidden"
    )
    return ctx, page, errors


def text(page, id_):
    return page.evaluate(f"document.getElementById('{id_}').textContent").strip()


def visible(page, id_):
    return page.evaluate(f"!document.getElementById('{id_}').hidden")


def wait_done(page, timeout=60000):
    page.wait_for_function(
        "!document.getElementById('doneBox').hidden || !document.getElementById('stopBox').hidden || !document.getElementById('pauseBox').hidden",
        timeout=timeout,
    )


# Vercel's analytics script is not served locally, and the page logs the
# reason it stopped with console.error on purpose; a scenario that stops names
# that line in `allow`. Anything else the page logs as an error is a failure.
def no_errors(errors, where, allow=()):
    unexpected = [
        e
        for e in errors
        if "_vercel/insights" not in e and not any(a in e for a in allow)
    ]
    expect(
        not unexpected,
        f"{where}: no unexpected console errors"
        + (" " + "; ".join(unexpected) if unexpected else ""),
    )


with sync_playwright() as p:
    browser = p.chromium.launch(channel="chrome")

    # --- 1. the fresh page, both schemes and a phone -------------------------
    ctx, page, errors = open_page(browser, CARD)
    expect(
        "Wikipedia from May 2026" in text(page, "packLine"),
        "pack line names the snapshot",
    )
    expect(page.is_checked("#routeEssentials"), "essentials is selected first")
    expect(
        text(page, "pickBtn") == "Choose the reader", "button says Choose the reader"
    )
    expect(
        "articles" in text(page, "routeEssentialsMeta"),
        "essentials card carries its count",
    )
    expect(
        "12 MB" in text(page, "routeAllMeta"),
        "all card carries its size: " + text(page, "routeAllMeta"),
    )
    shot(page, "01-fresh-light")
    page.click("#routeAll")
    expect(text(page, "pickBtn") == "Choose the card", "button follows the route")
    page.click("#routeEssentials")
    shot(page, "02-fresh-phone", width=390)
    no_errors(errors, "fresh page")
    ctx.close()

    ctx, page, errors = open_page(browser, CARD, scheme="dark")
    shot(page, "03-fresh-dark")
    ctx.close()

    # --- 2. not the reader's card --------------------------------------------
    ctx, page, errors = open_page(browser, NOT_CARD)
    page.click("#pickBtn")
    page.wait_for_function(
        "document.getElementById('pickStatus').textContent.length > 0"
    )
    expect(
        text(page, "pickStatus").startswith("That is not the reader's card"),
        "refuses a folder without .crosspoint",
    )
    expect(not visible(page, "stepCopy"), "and does not start a copy")
    shot(page, "04-not-the-card")
    no_errors(errors, "not the card")
    ctx.close()

    # --- 3. the essentials over a throttled link, photographed mid-copy ------
    ctx, page, errors = open_page(browser, CARD, "?mock=1&rate=1")
    fetched = []
    page.on("request", lambda r: fetched.append(r.url) if "/mock/" in r.url else None)
    page.click("#pickBtn")
    page.wait_for_function(
        "document.getElementById('rateLine').textContent.length > 0", timeout=20000
    )
    page.wait_for_timeout(1500)
    expect(visible(page, "stepCopy"), "the copy step appears")
    expect(
        "MB/s" in text(page, "rateLine"),
        "a measured rate is shown: " + text(page, "rateLine"),
    )
    expect(
        "left" in text(page, "leftLine") or text(page, "leftLine") == "",
        "time left is measured or absent: " + text(page, "leftLine"),
    )
    expect(
        " of 8 MB" in text(page, "bytesLine"),
        "bytes done of the tier's total: " + text(page, "bytesLine"),
    )
    expect(page.is_disabled("#routeAll"), "routes lock while copying")
    shot(page, "05-copying", full=False)
    wait_done(page)
    expect(visible(page, "doneBox"), "the copy finishes")
    expect(
        text(page, "doneLine") == "Eject the reader, then unplug it.",
        "the cable route's closing line",
    )
    expect(
        "49,938 articles" in text(page, "copyStatus"),
        "status names the tier's count: " + text(page, "copyStatus"),
    )
    shot(page, "06-done-essentials")
    files = page.evaluate(LIST_CARD)
    expect(files.get("manifest.json", 0) > 0, "manifest.json is on the card")
    expect(files.get("shards/000.blk") == 2000000, "part 1 is on the card, whole")
    expect("shards/001.blk" not in files, "part 2 is not (essentials only)")
    expect(files.get("titles.idx") == 6000000, "the title index is on the card")
    markers = page.evaluate(READ_MARKERS)
    expect(
        set(markers["files"])
        == {"dict.zst", "titles.idx", "blocks.dir", "shards/000.blk"},
        "markers name the four files written",
    )
    expect(
        not page.evaluate(
            "document.getElementById('bar').getAttribute('aria-valuenow') !== '100'"
        ),
        "the bar reads 100",
    )
    no_errors(errors, "essentials copy")

    # --- 4. a second visit adds the rest and skips what is there -------------
    fetched.clear()
    page.reload(wait_until="networkidle")
    page.wait_for_function("!document.getElementById('pickBtn').disabled")
    page.click("#routeAll")
    page.click("#pickBtn")
    wait_done(page, timeout=90000)
    expect(visible(page, "doneBox"), "the second copy finishes")
    expect(
        text(page, "doneLine") == "Put the card back in the reader and open Wikipedia.",
        "the card route's closing line",
    )
    names = [u.split("/mock/")[1] for u in fetched]
    expect(
        "shards/001.blk" in names and "shards/002.blk" in names,
        "parts 2 and 3 were fetched",
    )
    expect(
        "titles.idx" not in names and "shards/000.blk" not in names,
        "the title index and part 1 were NOT fetched again: " + ", ".join(names),
    )
    files = page.evaluate(LIST_CARD)
    expect(files.get("shards/002.blk") == 2000000, "part 3 is on the card")
    shot(page, "07-done-all-after-resume")
    no_errors(errors, "second visit")

    # --- 5. a file of the right size with no marker is verified, not fetched --
    page.evaluate("""async () => {
      const root = await navigator.storage.getDirectory();
      const wiki = await root.getDirectoryHandle('wikipedia');
      await wiki.removeEntry('copied.json');
      await wiki.removeEntry('manifest.json');
    }""")
    fetched.clear()
    page.reload(wait_until="networkidle")
    page.wait_for_function("!document.getElementById('pickBtn').disabled")
    page.click("#routeAll")
    page.click("#pickBtn")
    wait_done(page, timeout=90000)
    names = [u.split("/mock/")[1] for u in fetched]
    expect(
        names == ["manifest.json"],
        "without markers every part is read back, none re-fetched: " + ", ".join(names),
    )
    expect(visible(page, "doneBox"), "and the manifest is written again")
    no_errors(errors, "verify pass")
    ctx.close()

    # --- 6. the twenty-minute rule, all of Wikipedia ------------------------
    # 2 MB/s and a 3-second budget: after the 6 MB title index the projection
    # for 12 MB is 6 s, past the budget, so the page pauses before part 1.
    ctx, page, errors = open_page(browser, CARD, "?mock=1&rate=2&minutes=0.05")
    page.click("#routeAll")
    page.click("#pickBtn")
    wait_done(page, timeout=60000)
    expect(visible(page, "pauseBox"), "pauses on the all route")
    expect(
        "promised twenty minutes" in text(page, "pauseText"), "says what it promised"
    )
    expect(
        "MB/s" in text(page, "pauseText"),
        "names the measured rate: " + text(page, "pauseText"),
    )
    expect(
        text(page, "switchBtn") == "Stop at the essentials", "offers the other route"
    )
    files = page.evaluate(LIST_CARD)
    expect(
        "manifest.json" not in files,
        "no manifest yet: the pack is not present while paused",
    )
    shot(page, "08-pause-all")
    page.click("#keepBtn")
    wait_done(page, timeout=60000)
    expect(
        visible(page, "doneBox"), "keep going finishes the copy without asking again"
    )
    no_errors(errors, "pause, all")
    ctx.close()

    # --- 7. the twenty-minute rule, essentials over a slow cable ------------
    ctx, page, errors = open_page(browser, CARD, "?mock=1&rate=2&minutes=0.05")
    page.click("#pickBtn")
    wait_done(page, timeout=60000)
    expect(visible(page, "pauseBox"), "pauses on the cable route")
    expect(
        text(page, "switchBtn") == "Use the card in the computer instead",
        "offers the card in the computer",
    )
    shot(page, "09-pause-essentials")
    page.click("#switchBtn")
    page.wait_for_timeout(300)
    expect(page.is_checked("#routeAll"), "switching selects the card route")
    expect(not visible(page, "stepCopy"), "and returns to step 1")
    expect(
        text(page, "pickStatus").startswith("Eject the reader"),
        "with the eject instruction: " + text(page, "pickStatus"),
    )
    shot(page, "10-switched-to-card")
    no_errors(errors, "pause, essentials")
    ctx.close()

    # --- 8. a lost connection, then Resume -----------------------------------
    ctx, page, errors = open_page(browser, CARD, "?mock=1&rate=4")
    dropped = {"n": 0}

    def drop_once(route):
        if dropped["n"] == 0:
            dropped["n"] += 1
            route.abort("connectionreset")
        else:
            route.continue_()

    page.route("**/mock/shards/001.blk", drop_once)
    page.click("#routeAll")
    page.click("#pickBtn")
    wait_done(page, timeout=60000)
    expect(visible(page, "stopBox"), "stops when the connection drops")
    expect(
        "connection dropped" in text(page, "stopText"),
        "and says so: " + text(page, "stopText"),
    )
    expect("shards/001.blk" in text(page, "stopText"), "naming the part")
    expect(
        text(page, "rateLine") == "" and text(page, "leftLine") == "",
        "no stale rate or time left once stopped",
    )
    files = page.evaluate(LIST_CARD)
    expect(
        files.get("shards/000.blk") == 2000000 and "shards/001.blk" not in files,
        "what was copied is kept, the cut part is not half there",
    )
    shot(page, "11-connection-dropped")
    page.click("#resumeBtn")
    wait_done(page, timeout=60000)
    expect(visible(page, "doneBox"), "Resume finishes the copy")
    files = page.evaluate(LIST_CARD)
    expect(files.get("shards/001.blk") == 2000000, "the cut part is whole after Resume")
    no_errors(errors, "lost connection", allow=("ERR_CONNECTION_RESET", "copy stopped PackError: The connection dropped"))
    ctx.close()

    # --- 9. a part that arrives damaged, twice --------------------------------
    ctx, page, errors = open_page(browser, CARD, "?mock=1&rate=4")
    served = {"n": 0}
    junk = bytes([0x5A]) * 2000000

    def damage(route):
        served["n"] += 1
        route.fulfill(status=200, body=junk, content_type="application/octet-stream")

    page.route("**/mock/shards/000.blk", damage)
    page.click("#pickBtn")
    wait_done(page, timeout=60000)
    expect(visible(page, "stopBox"), "stops on a damaged part")
    expect(served["n"] == 2, f"tried exactly twice (served {served['n']})")
    expect(
        "arrived damaged twice" in text(page, "stopText"),
        "and says so: " + text(page, "stopText"),
    )
    files = page.evaluate(LIST_CARD)
    expect("shards/000.blk" not in files, "the damaged part is not left on the card")
    expect("manifest.json" not in files, "and no manifest was written")
    shot(page, "12-damaged")
    no_errors(errors, "damaged part", allow=("copy stopped PackError: shards/000.blk arrived damaged twice",))
    ctx.close()

    # --- 10. a browser without a folder picker --------------------------------
    ctx, page, errors = open_page(browser, NO_PICKER)
    expect(visible(page, "unsupported"), "the by-hand section shows")
    expect(not visible(page, "stepPick"), "and the picker step does not")
    links = page.evaluate(
        "Array.from(document.querySelectorAll('#fileList a')).map(a => a.textContent)"
    )
    expect(
        links
        == ["dict.zst", "titles.idx", "blocks.dir", "shards/000.blk", "manifest.json"],
        "essentials file list, manifest last: " + ", ".join(links),
    )
    page.click("#routeAll")
    links = page.evaluate(
        "Array.from(document.querySelectorAll('#fileList a')).map(a => a.textContent)"
    )
    expect(
        len(links) == 7 and links[-1] == "manifest.json", "the list follows the route"
    )
    shot(page, "13-by-hand")
    no_errors(errors, "no picker")
    ctx.close()

    browser.close()

print(f"{checks} checks, {failed} failed")
sys.exit(1 if failed else 0)
