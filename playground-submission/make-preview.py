#!/usr/bin/env python3
"""Render the Sticky Playground identity artwork for the CrossPlay entry.

Same method as site/make-og.py, and for the same reason: the wordmark is laid
out in HTML and photographed in a real browser, so the mark and Jersey 25 are
the real ones by construction rather than whatever PIL matched.

    uv run --with playwright python playground-submission/make-preview.py

Writes firmwares/crossplay/assets/preview.png at 1200x630.

THE MARK AND THE WORD CROSSPLAY, AND NOTHING ELSE. Mario's call, 2026-09-05.
This artwork is drawn at 1200x630 and shown on the Playground as a tile a few
hundred pixels wide, so everything in it has to survive being shrunk by three or
four. The first version was the site's hero -- a 112px headline, three lines of
serif body copy and a metadata row -- which at tile size left the body copy
illegible and the wordmark a few pixels tall, and read as a screenshot of some
other page rather than as an identity. Every other tile in the catalog is a
lockup. The tile's own caption underneath already carries the summary, so the
image does not need to repeat it, and one thing drawn large beats four drawn
small.

The Playground asks partner entries for official identity artwork rather than a
device photo, so this card carries no screenshot: nothing in it can be mistaken
for a picture of a Sticky. Keep it that way. If a real photo of CrossPlay on a
Sticky ever exists, it replaces this file rather than joining it.

Jersey 25 renders a capital V as something closer to a U, so keep V out of
anything set in it. "CrossPlay" has none; a descriptor line added later might.
"""

import asyncio
import pathlib

HERE = pathlib.Path(__file__).resolve().parent
SITE = HERE.parent / "site"
OUT = HERE / "firmwares" / "crossplay" / "assets" / "preview.png"

CARD = """
<style>
  @font-face { font-family:"Jersey 25"; src:url("FONTS/jersey25.woff2") format("woff2") }
  *{box-sizing:border-box} html,body{margin:0}
  /* The fork's paper and ink, the same pair the site and the device use. */
  body{width:1200px;height:630px;background:#faf9f6;color:#111110;overflow:hidden;
       display:flex;align-items:center;justify-content:center}
  /* One lockup, centred, sized so it still reads when the tile shrinks this
     card to about a third. The mark is set to the wordmark's cap height so the
     two sit on one line rather than one hanging off the other. */
  .lockup{display:flex;align-items:center;gap:38px;
          font-family:"Jersey 25";font-weight:400;font-size:168px;line-height:1}
  .lockup svg{width:140px;height:140px;flex:none}
  .lockup span{padding-bottom:12px}
</style>
  <div class="lockup">
    <svg viewBox="0 0 32 32" role="img" aria-label="CrossPlay">
      <rect x="8.4" y="8.4" width="15.2" height="15.2" rx="1.6" fill="#111110"/>
      <g fill="none" stroke="#111110" stroke-width="2.6" stroke-linecap="square">
        <path d="M2.6 10.4V2.6h7.8"/><path d="M29.4 10.4V2.6h-7.8"/>
        <path d="M2.6 21.6v7.8h7.8"/><path d="M29.4 21.6v7.8h-7.8"/>
      </g>
    </svg><span>CrossPlay</span>
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
            # The webfont loads from disk; give the layout a beat to settle or
            # the wordmark photographs in the fallback face.
            await page.wait_for_timeout(600)
            await page.screenshot(path=str(OUT))
            await browser.close()
    finally:
        page_path.unlink(missing_ok=True)
    print(f"wrote {OUT} ({OUT.stat().st_size / 1024:.0f} KB)")


asyncio.run(main())
