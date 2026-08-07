#!/usr/bin/env python3
"""Render the images for a Reddit or Hacker News post.

Same approach as make-og.py and for the same reason: laying the cards out in
HTML and photographing them in the browser that renders the site means the
wordmark, the type and the panels are the real ones by construction, rather than
whatever font PIL matched.

    uv run --with playwright python site/make-post-images.py

Writes into assets/post/. Nothing on the page shows you these, so they go stale
silently: re-run after any change to the mark, the headline or the screenshots.

What each one is for
--------------------
* shelf-1080x1350.png   Reddit's feed crops landscape images hard and shows
                        portrait ones nearly whole, so the one that has to earn
                        the click is 4:5. It is six panels and no prose,
                        because r/eink scrolls past paragraphs and stops at
                        screens.
* nearby-1200x790.png   The second image in a Reddit gallery, or the one to
                        reply with. PLAY NEARBY is the part people ask about and
                        it is the hardest to believe from a description, so this
                        is the two devices mid-match.
* shelf-two-1080x1350.png
                        The third in the gallery, same shape as the first and
                        carrying the apps it has no room for. Eleven apps do not
                        fit in six panels, and an app nobody sees may as well
                        not ship. Together the two shelves show each app once.
* card-1200x630.png     For anywhere that unfurls a link. Hacker News itself
                        shows no image at all, so this is not for the HN page:
                        it is what appears when the URL is pasted into Slack,
                        Twitter, Discord or Mastodon, which is where most of an
                        HN post's traffic is actually shared.
"""

import asyncio
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "assets" / "post"

# The page's own tokens. Kept literal rather than parsed out of styles.css: this
# script has to keep working if the stylesheet is refactored, and a share image
# that silently changes colour is worse than one that needs a manual edit.
BASE = """
<style>
  @font-face { font-family:"Jersey 25"; src:url("../fonts/jersey25.woff2") format("woff2") }
  @font-face { font-family:"Instrument Serif"; src:url("../fonts/instrumentserif.woff2") format("woff2") }
  *{box-sizing:border-box} html,body{margin:0}
  body{background:#f5f2ea;color:#111110;overflow:hidden;
       font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
  /* The paper tooth, same 3px checkerboard the site uses. */
  body::before{content:"";position:fixed;inset:0;z-index:0;pointer-events:none;
    background-image:repeating-conic-gradient(rgba(17,17,16,.22) 0% 25%, transparent 0% 50%);
    background-size:3px 3px;opacity:.5}
  .band{position:relative;z-index:1;background:#111110;color:#f5f2ea;
        display:flex;align-items:baseline;justify-content:space-between;
        font-family:"Jersey 25";letter-spacing:.02em}
  .band .note{font:12px/1 ui-monospace,Menlo,monospace;letter-spacing:.16em;
              text-transform:uppercase;opacity:.75}
  .grid{position:relative;z-index:1;display:grid}
  .grid img{width:100%;display:block;border:3px solid #111110;background:#fff}
  .wm{display:flex;align-items:center;gap:12px}
  .wm svg{flex:none}
</style>
"""

MARK = """
<svg viewBox="0 0 32 32" width="%d" height="%d">
  <rect x="8.4" y="8.4" width="15.2" height="15.2" rx="1.6" fill="currentColor"/>
  <g fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="square">
    <path d="M2.6 10.4V2.6h7.8"/><path d="M29.4 10.4V2.6h-7.8"/>
    <path d="M2.6 21.6v7.8h7.8"/><path d="M29.4 21.6v7.8h-7.8"/>
  </g>
</svg>
"""

SHELF = (
    BASE
    + """
<style>
  body{width:1080px;height:1350px}
  .band{font-size:52px;padding:22px 34px 16px}
  .grid{grid-template-columns:repeat(3,1fr);gap:18px;padding:26px 34px}
  .foot{position:relative;z-index:1;padding:6px 34px 0;
        font:14px/1.5 ui-monospace,Menlo,monospace;letter-spacing:.14em;
        text-transform:uppercase;color:#46443c;display:flex;justify-content:space-between}
</style>
<div class="band"><div class="wm">"""
    + (MARK % (46, 46))
    + """<span>Crossplay</span></div>
  <span class="note">Xteink X4 Pro</span></div>
<div class="grid">
  <img src="../shots/chess.png"><img src="../shots/jaipur.png">
  <img src="../shots/dd.png"><img src="../shots/murdle.png">
  <img src="../shots/study.png"><img src="../shots/xkcd.png">
</div>
<div class="foot"><span>Open firmware &middot; no app store &middot; no account</span>
  <span>crossplay.ma-r-s.com</span></div>
"""
)

# The other six. A Reddit gallery is swiped, not read, so the third panel is the
# same shape as the first and carries the apps SHELF has no room for: between
# them the two cover everything, with no app shown twice.
SHELF_TWO = (
    BASE
    + """
<style>
  body{width:1080px;height:1350px}
  .band{font-size:52px;padding:22px 34px 16px}
  .grid{grid-template-columns:repeat(3,1fr);gap:18px;padding:26px 34px}
  /* Solitaire is the one landscape app, so it is shorter than everything
     beside it. Centring in the cell makes that read as a deliberate pause
     rather than a panel that failed to load. */
  .grid img{align-self:center}
  .foot{position:relative;z-index:1;padding:6px 34px 0;
        font:14px/1.5 ui-monospace,Menlo,monospace;letter-spacing:.14em;
        text-transform:uppercase;color:#46443c;display:flex;justify-content:space-between}
</style>
<div class="band"><div class="wm">"""
    + (MARK % (46, 46))
    + """<span>Crossplay</span></div>
  <span class="note">Xteink X4 Pro</span></div>
<div class="grid">
  <img src="../shots/battleship.png"><img src="../shots/connections.png">
  <img src="../shots/solitaire.png"><img src="../shots/insider.png">
  <img src="../shots/hackernews.png"><img src="../shots/home.png">
</div>
<div class="foot"><span>Open firmware &middot; no app store &middot; no account</span>
  <span>crossplay.ma-r-s.com</span></div>
"""
)

NEARBY = (
    BASE
    + """
<style>
  body{width:1200px;height:790px}
  .band{font-size:46px;padding:18px 30px 14px}
  .grid{grid-template-columns:repeat(3,1fr);gap:22px;padding:26px 30px}
  .cap{position:relative;z-index:1;padding:2px 30px 0;
       font:14px/1.5 ui-monospace,Menlo,monospace;letter-spacing:.14em;
       text-transform:uppercase;color:#46443c}
</style>
<div class="band"><div class="wm">"""
    + (MARK % (40, 40))
    + """<span>Play nearby</span></div>
  <span class="note">Two devices, nothing to type</span></div>
<div class="grid">
  <img src="../shots/link-search.png">
  <img src="../shots/link-a.png">
  <img src="../shots/link-b.png">
</div>
<div class="cap">They find each other over ESP-NOW. No pairing screen, no room code,
  no account, no router, no internet.</div>
"""
)

CARD = (
    BASE
    + """
<style>
  body{width:1200px;height:630px;display:grid;grid-template-columns:1fr 430px;align-items:center}
  .left{position:relative;z-index:1;padding:0 0 0 76px}
  .wm{font-family:"Jersey 25";font-size:40px;line-height:1}
  h1{font-family:"Jersey 25";font-weight:400;font-size:96px;line-height:.96;margin:36px 0 24px}
  p{font-family:"Instrument Serif";font-size:30px;line-height:1.35;margin:0;max-width:23ch;color:#2a2822}
  .meta{margin-top:38px;font:12px/1 ui-monospace,Menlo,monospace;letter-spacing:.18em;
        text-transform:uppercase;color:#46443c;display:flex;gap:26px}
  .panel{position:relative;z-index:1;height:630px;display:flex;align-items:center;
         justify-content:center;overflow:hidden}
  .panel img{width:330px;border:3px solid #111110;display:block;
             transform:rotate(4deg) translateY(14px)}
</style>
<div class="left">
  <div class="wm">"""
    + (MARK % (40, 40))
    + """<span>Crossplay</span></div>
  <h1>E-ink is good<br>at waiting.</h1>
  <p>Everything else a still screen is good at.</p>
  <div class="meta"><span>A fork of CrossPoint</span><span>Xteink X4 Pro</span><span>MIT</span></div>
</div>
<div class="panel"><img src="../shots/games.png" alt=""></div>
"""
)

JOBS = [
    ("shelf-1080x1350.png", SHELF, 1080, 1350),
    ("shelf-two-1080x1350.png", SHELF_TWO, 1080, 1350),
    ("nearby-1200x790.png", NEARBY, 1200, 790),
    ("card-1200x630.png", CARD, 1200, 630),
]


async def main():
    from playwright.async_api import async_playwright

    OUT.mkdir(parents=True, exist_ok=True)
    async with async_playwright() as p:
        browser = await p.chromium.launch(channel="chrome")
        for name, html, w, h in JOBS:
            # Written beside the assets so the relative src paths resolve; the
            # file: origin has no server to root them against.
            tmp = OUT / f".{name}.html"
            tmp.write_text(html)
            try:
                page = await browser.new_page(
                    viewport={"width": w, "height": h}, device_scale_factor=2
                )
                await page.goto(tmp.as_uri(), wait_until="networkidle")
                # The webfonts load from disk; without a beat the wordmark
                # photographs in the fallback face.
                await page.wait_for_timeout(700)
                await page.screenshot(path=str(OUT / name))
                await page.close()
            finally:
                tmp.unlink(missing_ok=True)
            kb = (OUT / name).stat().st_size / 1024
            print(f"wrote assets/post/{name}  ({w}x{h} at 2x, {kb:.0f} KB)")
        await browser.close()


asyncio.run(main())
