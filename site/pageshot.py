"""Render the site to PNGs so it can actually be looked at.

The interactive browser pane went blank after a programmatic scroll and stopped
recovering, and "it renders fine" is exactly the claim that must never be made
without looking. This drives the cached Playwright chromium instead: reliable,
headless, and it produces full-page captures the critic can be handed too.

  uv run --with playwright python pageshot.py <url> <outdir> [width] [scheme]
"""

import sys, asyncio, pathlib
from playwright.async_api import async_playwright


async def main():
    url = sys.argv[1]
    outdir = pathlib.Path(sys.argv[2])
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 1440
    scheme = sys.argv[4] if len(sys.argv) > 4 else "light"
    outdir.mkdir(parents=True, exist_ok=True)

    async with async_playwright() as p:
        # channel="chrome" drives the Chrome already on this Mac. The cached
        # playwright build is 1228 and this client wants 1234, and downloading a
        # second chromium to look at a local page is not a good trade.
        browser = await p.chromium.launch(channel="chrome")
        page = await browser.new_page(
            viewport={"width": width, "height": 1000},
            color_scheme=scheme,
            device_scale_factor=2,
        )
        errors = []
        page.on(
            "console",
            lambda m: errors.append(f"console.{m.type}: {m.text}")
            if m.type in ("error", "warning")
            else None,
        )
        page.on("pageerror", lambda e: errors.append(f"pageerror: {e}"))
        resp = await page.goto(url, wait_until="networkidle")
        await page.wait_for_timeout(700)

        tag = f"{width}-{scheme}"
        # Full page in one image, then viewport-sized slices, because a 7000px
        # tall PNG scaled to fit is unreadable and reviewing is the whole point.
        await page.screenshot(path=str(outdir / f"full-{tag}.png"), full_page=True)

        height = await page.evaluate("document.documentElement.scrollHeight")
        step = 1000
        n = 0
        for y in range(0, height, step):
            await page.evaluate(f"window.scrollTo(0,{y})")
            await page.wait_for_timeout(180)
            await page.screenshot(path=str(outdir / f"slice-{tag}-{n:02d}.png"))
            n += 1

        print(
            f"status={resp.status} height={height} slices={n} scheme={scheme} width={width}"
        )
        if errors:
            print("PAGE ERRORS:")
            for e in errors[:15]:
                print("  " + e)
        else:
            print("no console errors")
        await browser.close()


asyncio.run(main())
