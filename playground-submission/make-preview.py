#!/usr/bin/env python3
"""Render the Sticky Playground identity artwork for the CrossPlay entry.

Same method as site/make-og.py, and for the same reason: the wordmark is laid
out in HTML and photographed in a real browser, so the mark, Jersey 25 and
Instrument Serif are the real ones by construction rather than whatever PIL
matched.

    uv run --with playwright python playground-submission/make-preview.py

Writes firmwares/crossplay/assets/preview.png at 1200x630.

The Playground asks partner entries for official identity artwork rather than a
device photo, so this card carries no screenshot: nothing in it can be mistaken
for a picture of a Sticky. Keep it that way. If a real photo of CrossPlay on a
Sticky ever exists, it replaces this file rather than joining it.

Jersey 25 renders a capital V as something closer to a U, so keep V out of the
two lines set in it (the wordmark and the headline).
"""

import asyncio
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
SITE = HERE.parent / "site"
OUT = HERE / "firmwares" / "crossplay" / "assets" / "preview.png"

CARD = """
<style>
  @font-face { font-family:"Jersey 25"; src:url("FONTS/jersey25.woff2") format("woff2") }
  @font-face { font-family:"Instrument Serif"; src:url("FONTS/instrumentserif.woff2") format("woff2") }
  *{box-sizing:border-box} html,body{margin:0}
  body{width:1200px;height:630px;background:#faf9f6;color:#111110;overflow:hidden;
       display:flex;flex-direction:column;align-items:center;justify-content:center;
       text-align:center;
       font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
  .wm{display:flex;align-items:center;gap:16px;font-family:"Jersey 25";font-size:46px;line-height:1}
  .wm svg{width:46px;height:46px}
  h1{font-family:"Jersey 25";font-weight:400;font-size:112px;line-height:.96;margin:44px 0 30px}
  p{font-family:"Instrument Serif";font-size:34px;line-height:1.35;margin:0;max-width:26ch;color:#2a2822}
  .meta{margin-top:52px;font:13px/1 ui-monospace,Menlo,monospace;letter-spacing:.18em;
        color:#5a584e;display:flex;gap:30px}
</style>
  <div class="wm">
    <svg viewBox="0 0 32 32">
      <rect x="8.4" y="8.4" width="15.2" height="15.2" rx="1.6" fill="#111110"/>
      <g fill="none" stroke="#111110" stroke-width="2.6" stroke-linecap="square">
        <path d="M2.6 10.4V2.6h7.8"/><path d="M29.4 10.4V2.6h-7.8"/>
        <path d="M2.6 21.6v7.8h7.8"/><path d="M29.4 21.6v7.8h-7.8"/>
      </g>
    </svg><span>CrossPlay</span>
  </div>
  <h1>E-ink is good<br>at waiting.</h1>
  <p>An e-reader that also plays. Games, flashcards
     and comics for reTerminal Sticky.</p>
  <div class="meta">
    <span>A FORK OF CROSSPOINT</span><span>MIT</span>
  </div>
"""


async def main():
    from playwright.async_api import async_playwright

    page_path = HERE / ".preview-card.html"
    page_path.write_text(
        CARD.replace("FONTS", SITE.joinpath("assets", "fonts").as_uri())
    )
    OUT.parent.mkdir(parents=True, exist_ok=True)
    try:
        async with async_playwright() as p:
            browser = await p.chromium.launch(channel="chrome")
            page = await browser.new_page(
                viewport={"width": 1200, "height": 630}, device_scale_factor=1
            )
            await page.goto(page_path.as_uri(), wait_until="networkidle")
            # The webfonts load from disk; give the layout a beat to settle or
            # the wordmark photographs in the fallback face.
            await page.wait_for_timeout(600)
            await page.screenshot(path=str(OUT))
            await browser.close()
    finally:
        page_path.unlink(missing_ok=True)
    print(f"wrote {OUT} ({OUT.stat().st_size / 1024:.0f} KB)")


asyncio.run(main())
