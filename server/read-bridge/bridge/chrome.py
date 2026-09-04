"""The look of this bridge's own pages.

These pages sit on read.ma-r-s.com, a different origin from the site at
crossplay.ma-r-s.com, so nothing here can link that stylesheet: the CSS is
inline and the two typefaces are vendored under bridge/static/ (the Dockerfile
already copies the whole bridge/ directory, so they ship with no deploy
change). What is duplicated is the appearance, not the meaning -- the tokens
below are the same values site/styles.css defines, and if that palette ever
moves, these move with it by hand.

The rule the pages follow: someone arrives here holding a reader that is
showing a code, with no idea what this site is. Every page therefore says
where they are in a three-step rail before it says anything in a sentence,
and the two pages that ask for something -- the password, the code -- draw
what they are asking for. Words are the second explanation, not the first.
"""

from fastapi.responses import HTMLResponse

# The service, in the two forms the chrome needs it. Its twin at
# ../../study-bridge/bridge/chrome.py is the same file with these three lines
# changed; keeping them at the top is what makes that legible.
SERVICE = "Read later"
ACCOUNT = "Instapaper"
CARGO = "articles"
STEPS = ("Sign in", "Pair your reader", "Ready")

CSS = """
@font-face{font-family:"Jersey 25";src:url(/assets/jersey25.woff2)format("woff2");font-weight:400;font-display:swap}
@font-face{font-family:"Instrument Serif";src:url(/assets/instrumentserif.woff2)format("woff2");font-weight:400;font-display:swap}
:root{
--ink:#111110;--paper:#f5f2ea;--paper-2:#ebe7db;--edge:#a9a494;--muted:#46443c;
--display:"Jersey 25","Arial Narrow",sans-serif;
--serif:"Instrument Serif",Georgia,serif;
--mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace;
--body:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
--gutter:clamp(1.1rem,5vw,2rem);
}
@media(prefers-color-scheme:dark){:root{
--ink:#f2f1ec;--paper:#100f0e;--paper-2:#1a1917;--edge:#5a564c;--muted:#b8b5aa}}
*{box-sizing:border-box}
body{margin:0;background:var(--paper);color:var(--ink);font-family:var(--body);
font-size:17px;line-height:1.6;-webkit-font-smoothing:antialiased}

/* The band. The device's header is a solid black bar with knocked-out type,
   and it is the most recognisable thing the fork draws; it is what tells
   someone who has only ever seen the reader that this page is the same
   product. Fixed colours, not tokens: the bar is black in both themes. */
.band{background:#111110;color:#faf9f6;font-family:var(--display);
font-size:clamp(1.5rem,5vw,2.1rem);letter-spacing:.02em;
padding:.45em var(--gutter) .3em;display:flex;align-items:baseline;
justify-content:space-between;gap:1rem}
.band a{color:inherit;text-decoration:none;display:flex;align-items:center;gap:.5rem}
.band svg{width:1.1em;height:1.1em;flex:none;display:block}
.band .note{font-family:var(--mono);font-size:clamp(.6rem,1.6vw,.72rem);
letter-spacing:.14em;text-transform:uppercase;opacity:.75;white-space:nowrap}
.rule{height:3px;background:var(--ink)}

main{max-width:31rem;margin:0 auto;padding:1.6rem var(--gutter) 4rem}
/* On a desktop the column is the whole page, so it gets the site's own
   measure -- and it is centred in the window rather than hanging from the
   band. Half these pages are four lines long; left at the top of a 1440px
   screen, a short one stops reading as spare and starts reading as a page
   that failed to finish loading, which is the exact complaint the confirm
   page already earned once. margin-block:auto only centres what is short:
   a page taller than the window still just fills it. */
@media(min-width:760px){
main{max-width:34rem;padding-top:2.4rem;margin-block:auto}
body{min-height:100svh;display:flex;flex-direction:column}
.band,.rule{flex:none}
}

/* The step rail. Three discs and the line between them, in the order the
   thing actually happens. It is the whole answer to "what is going on and
   what do I do next" for someone who reads no prose at all: the filled disc
   is here, the ticked ones are behind you, the outlined one is next. */
.steps{display:flex;list-style:none;margin:0 0 2rem;padding:0}
.steps li{flex:1;text-align:center;position:relative;font-family:var(--mono);
font-size:.66rem;letter-spacing:.1em;text-transform:uppercase;color:var(--muted);
line-height:1.3}
.steps li::before{content:"";position:absolute;top:1.05rem;left:-50%;width:100%;
height:2px;background:var(--edge)}
.steps li:first-child::before{display:none}
.steps li.done::before,.steps li.now::before{background:var(--ink)}
.steps .dot{width:2.1rem;height:2.1rem;border:3px solid var(--edge);
background:var(--paper);color:var(--muted);border-radius:50%;
display:flex;align-items:center;justify-content:center;margin:0 auto .5rem;
font-family:var(--display);font-size:1.25rem;line-height:1;position:relative;z-index:1}
.steps .dot svg{width:1rem;height:1rem;display:block}
.steps li.now .dot{border-color:var(--ink);background:var(--ink);color:var(--paper)}
.steps li.now{color:var(--ink)}
.steps li.done .dot{border-color:var(--ink);color:var(--ink)}

h1{font-family:var(--display);font-weight:400;font-size:clamp(2.1rem,7vw,3rem);
line-height:1.02;margin:0 0 .5rem;letter-spacing:.01em}
.lede{font-family:var(--serif);font-size:1.2rem;line-height:1.4;margin:0 0 1.6rem;
color:var(--ink)}
p{margin:0 0 1rem}
small,.small{font-size:.85rem;color:var(--muted);line-height:1.5}
a{color:var(--ink)}

/* Form controls. 3px ink borders and Jersey on the button, the same pair the
   site uses for its calls to action, so the one thing to press looks pressed
   into the page rather than dropped on it. */
form{margin:0}
label{display:block;font-family:var(--mono);font-size:.68rem;letter-spacing:.12em;
text-transform:uppercase;color:var(--muted);margin:1rem 0 .3rem}
input{font:inherit;width:100%;padding:.6rem .7rem;border:2px solid var(--edge);
background:var(--paper);color:var(--ink);border-radius:0}
input:focus{outline:3px solid var(--ink);outline-offset:1px;border-color:var(--ink)}
button{font-family:var(--display);font-size:1.3rem;letter-spacing:.03em;
padding:.5em 1.15em .38em;border:3px solid var(--ink);background:var(--ink);
color:var(--paper);cursor:pointer;margin-top:1.4rem;width:100%;border-radius:0;
transition:transform .08s ease}
button:hover{transform:translateY(-2px)}
button:focus-visible{outline:3px solid var(--ink);outline-offset:3px}

/* The code field is the one input whose content the user is copying off a
   screen a foot away, so it is set big, in the mono face, and spaced out
   character by character to read as "these separate characters, in order". */
input.code{font-family:var(--mono);font-size:1.8rem;letter-spacing:.35em;
text-align:center;text-transform:uppercase;padding:.7rem .3rem;
border-width:3px;border-color:var(--ink)}

/* The figure blocks: the reader with a code on it, the flow of the password.
   Line art only, on the paper-2 well the site uses behind its screenshots. */
figure{margin:0 0 1.6rem;background:var(--paper-2);border:1px solid var(--edge);
padding:1.1rem}
figure svg{display:block;width:100%;height:auto}
figcaption{font-family:var(--mono);font-size:.66rem;letter-spacing:.1em;
text-transform:uppercase;color:var(--muted);text-align:center;margin-top:.7rem}

/* Outcome pages. A filled square with a tick, or a hollow one with a bar:
   the shape carries the verdict before the sentence under it does, and it
   reads the same to someone who cannot see colour, which is everyone here,
   the palette having none. */
.mark{width:3.4rem;height:3.4rem;border:3px solid var(--ink);display:flex;
align-items:center;justify-content:center;margin:0 0 1.2rem}
.mark svg{width:1.9rem;height:1.9rem;display:block}
.mark.ok{background:var(--ink);color:var(--paper)}
.mark.no{background:var(--paper);color:var(--ink)}

/* Paired readers. Each one is drawn, not bulleted: the row shows the object
   it stands for, so a list of two is two devices rather than two strings. */
ul.readers{list-style:none;margin:0 0 1.6rem;padding:0}
ul.readers li{display:flex;align-items:center;gap:.9rem;padding:.9rem 0;
border-bottom:1px solid var(--edge)}
ul.readers li:first-child{border-top:1px solid var(--edge)}
ul.readers svg{width:2rem;height:auto;flex:none;color:var(--ink)}
.reader-name{flex:1;min-width:0}
.reader-name b{font-weight:600;display:block}
.reader-name small{display:block}
ul.readers button{width:auto;margin:0;font-size:1rem;padding:.3em .8em .2em;
border-width:2px;background:var(--paper);color:var(--ink)}
/* A footer directly under a list that already ends in a rule drew a second
   rule with a band of nothing between them, which reads as a row that failed
   to render. */
footer{margin-top:2.4rem;border-top:1px solid var(--edge);padding-top:1rem}
ul.readers+footer{border-top:0;margin-top:1.4rem;padding-top:0}

/* The waiting line, and the link that gets you off a page that waits. */
.waiting{display:flex;align-items:center;gap:.7rem;font-family:var(--mono);
font-size:.72rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);
margin:0 0 1.6rem}
.dots{display:flex;gap:.3rem;flex:none}
.dots i{width:.42rem;height:.42rem;background:var(--edge);border-radius:50%;
display:block}
.dots i:first-child{background:var(--ink)}

/* Links that are the only way forward are set as the button they are. Every
   primary action on every other page is a filled bar, so a bare text link on
   the one page where someone is stuck is the weakest thing on the screen at
   the moment it matters most. */
a.btn{display:block;text-align:center;text-decoration:none;
font-family:var(--display);font-size:1.3rem;letter-spacing:.03em;
padding:.5em 1.15em .38em;border:3px solid var(--ink);background:var(--ink);
color:var(--paper);margin-top:1.4rem;transition:transform .08s ease}
a.btn:hover{transform:translateY(-2px)}
a.btn.quiet{background:var(--paper);color:var(--ink)}
"""

# The wordmark, the same shape as site/assets/favicon.svg: a filled panel in
# four bracket corners.
#
# Every attribute below is quoted. Unquoted ones are legal HTML right up until
# the element self-closes: `height=40/>` parses the slash INTO the value, the
# tag never closes, and the whole figure renders as an empty box with a caption
# under it -- which is exactly what it did.
MARK = (
    '<svg viewBox="0 0 32 32" aria-hidden="true" focusable="false">'
    '<rect x="8.4" y="8.4" width="15.2" height="15.2" rx="1.6" fill="currentColor" />'
    '<g fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="square">'
    '<path d="M2.6 10.4V2.6h7.8" /><path d="M29.4 10.4V2.6h-7.8" />'
    '<path d="M2.6 21.6v7.8h7.8" /><path d="M29.4 21.6v7.8h-7.8" /></g></svg>'
)

TICK = (
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3.4" '
    'stroke-linecap="square" aria-hidden="true"><path d="M4 12.5l5.5 5.5L20 6" /></svg>'
)
BAR = (
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3.4" '
    'stroke-linecap="square" aria-hidden="true">'
    '<path d="M12 4v10" />'
    '<circle cx="12" cy="19.5" r="1.9" fill="currentColor" stroke="none" /></svg>'
)
# A reader, seen flat: the panel, the two keys down the right edge.
READER = (
    '<svg viewBox="0 0 34 48" fill="none" stroke="currentColor" stroke-width="2" '
    'aria-hidden="true"><rect x="1" y="1" width="32" height="46" rx="2.5" />'
    '<rect x="5" y="5" width="20" height="38" fill="currentColor" opacity="0.12" '
    'stroke="none" /><path d="M29 14v7M29 27v7" stroke-linecap="round" /></svg>'
)


def steps_rail(current: int) -> str:
    """The three-step rail, `current` being 1, 2 or 3. Steps behind the current
    one are ticked; the current one is filled; the rest are outlined."""
    out = ["<ol class=steps aria-label='Where you are'>"]
    for i, label in enumerate(STEPS, start=1):
        cls = "done" if i < current else ("now" if i == current else "")
        inner = TICK if i < current else str(i)
        here = " aria-current=step" if i == current else ""
        out.append(
            f"<li class='{cls}'{here}><span class=dot aria-hidden=true>{inner}</span>"
            f"<span>{label}</span></li>"
        )
    out.append("</ol>")
    return "".join(out)


def mark(ok: bool) -> str:
    """The outcome square. Filled and ticked, or hollow and barred."""
    cls = "ok" if ok else "no"
    label = "Done" if ok else "Not done"
    glyph = TICK if ok else BAR
    return f"<div class='mark {cls}' role=img aria-label='{label}'>{glyph}</div>"


def reader_with_code() -> str:
    """The figure on the pairing page: the reader showing a code, an arrow
    curving out of its screen and down into the box below. Someone who reads
    none of the words still knows the characters on the panel in their hand go
    in the field under this picture.

    The reader is drawn large for one reason: the code is eight characters
    (bridge/pairing.py), and eight cells inside a small panel stop reading as
    characters and start reading as hatching."""
    return (
        '<figure><svg viewBox="0 0 300 132" fill="none" stroke="currentColor" '
        'stroke-width="2" aria-hidden="true">'
        # the reader
        '<rect x="8" y="8" width="104" height="116" rx="5" />'
        '<rect x="17" y="18" width="86" height="90" fill="currentColor" '
        'opacity="0.08" stroke="none" />'
        '<path d="M108 42v16M108 68v16" stroke-linecap="round" />'
        # The code, set on the panel in the mono face. It was eight outlined
        # cells first: at the width this figure ever actually renders, each
        # cell is about nine pixels and eight of them read as hatching, not as
        # characters. A sample string reads as a code at any size.
        '<text x="60" y="70" font-size="13" letter-spacing="1.2" '
        'font-family="ui-monospace,SFMono-Regular,Menlo,monospace" '
        'fill="currentColor" stroke="none" text-anchor="middle">K7P4M2XR</text>'
        # the arrow, from the panel over to the field
        '<path d="M120 56c22-22 56-26 92-12" stroke-dasharray="6 6" '
        'stroke-linecap="round" />'
        '<path d="M205 33l9 11-13 3" stroke-linecap="round" '
        'stroke-linejoin="round" />'
        # the field it lands in, with the same eight places waiting empty
        '<rect x="158" y="58" width="134" height="44" stroke-width="3" />'
        '<g stroke-width="3" stroke-linecap="round" opacity="0.4">'
        '<path d="M166 90h10M181.4 90h10M196.8 90h10M212.2 90h10M227.6 90h10'
        'M243 90h10M258.4 90h10M273.8 90h10" /></g>'
        '</svg><figcaption>The code on the reader goes here</figcaption></figure>'
    )


def confirm_on_reader() -> str:
    """The figure after a code is claimed. The reader is drawn with the key it
    wants pressed ringed, because the next move is not on this screen at all
    and nothing else on the page can say that.

    No arrow into the device: with the press ripples radiating out of the
    key, an inbound arrow made a cold reader see something being beamed AT
    the reader. Two arrows in opposite directions is one too many."""
    return (
        '<figure><svg viewBox="0 0 300 132" fill="none" stroke="currentColor" '
        'stroke-width="2" aria-hidden="true">'
        '<rect x="60" y="8" width="86" height="116" rx="5" />'
        '<rect x="69" y="18" width="68" height="88" fill="currentColor" '
        'opacity="0.08" stroke="none" />'
        # a name set on the panel, as two ruled lines
        '<path d="M80 48h50M80 62h32" stroke-width="3.4" stroke-linecap="round" '
        'opacity="0.55" />'
        # the two side keys; the lower one is filled and ringed
        '<rect x="142" y="34" width="9" height="18" rx="4.5" />'
        '<rect x="142" y="64" width="9" height="22" rx="4.5" fill="currentColor" />'
        '<circle cx="146" cy="75" r="19" stroke-dasharray="5 6" />'
        # two ripple arcs off that key, the way a press is drawn
        '<path d="M176 63a26 26 0 010 24" stroke-linecap="round" opacity="0.75" />'
        '<path d="M190 55a40 40 0 010 40" stroke-linecap="round" opacity="0.4" />'
        '</svg><figcaption>Press the key the reader names</figcaption></figure>'
    )


def service_flow() -> str:
    """The figure on the sign-in page: account, this page, reader, in the order
    the thing travels.

    It used to also carry the password promise, as a padlock and a struck-out
    box. A cold reader took the struck box to mean "leave the password field
    blank" -- which is what the field directly under it invites -- and got
    nothing at all from the padlock. The promise is one sentence and reads
    perfectly as a sentence; a picture that argues with the form it sits above
    is worse than no picture. This one orients, and only orients."""
    return (
        '<figure><svg viewBox="0 0 300 86" fill="none" stroke="currentColor" '
        'stroke-width="2" aria-hidden="true">'
        '<rect x="2" y="24" width="86" height="40" />'
        '<text x="45" y="49" font-size="11" font-family="ui-monospace,monospace" '
        f'fill="currentColor" stroke="none" text-anchor="middle">{ACCOUNT.upper()}</text>'
        '<rect x="112" y="24" width="82" height="40" stroke-width="3" />'
        '<text x="153" y="49" font-size="11" font-family="ui-monospace,monospace" '
        'fill="currentColor" stroke="none" text-anchor="middle">THIS PAGE</text>'
        '<rect x="238" y="14" width="44" height="60" rx="3" />'
        '<rect x="245" y="21" width="30" height="42" fill="currentColor" '
        'opacity="0.1" stroke="none" />'
        '<path d="M279 32v8M279 46v8" stroke-linecap="round" />'
        '<path d="M90 44h20M196 44h40" stroke-linecap="round" />'
        '<path d="M104 39l6 5-6 5M230 39l6 5-6 5" stroke-linecap="round" '
        'stroke-linejoin="round" />'
        '</svg><figcaption>'
        f'Your {ACCOUNT} {CARGO} reach your reader through this page'
        '</figcaption></figure>'
    )


def waiting() -> str:
    """The figure on the page that waits for the device.

    That page had no control, no indicator and no way out, and a cold reader's
    first words about it were "did this page not finish loading?". A page whose
    correct behaviour is to do nothing has to SAY it is doing nothing on
    purpose -- see the reader-confirm figure above it, then this line."""
    return (
        '<p class=waiting><span class=dots aria-hidden=true>'
        '<i></i><i></i><i></i></span>'
        'This page will not change by itself. Look at the reader.</p>'
    )


def page(title: str, body: str, *, step: int | None = None) -> HTMLResponse:
    """One page. `step` draws the rail; pages that are not on the path through
    (an error, a rate limit) pass None and get the band and the type without
    claiming a position in a sequence they interrupted."""
    rail = steps_rail(step) if step else ""
    return HTMLResponse(
        "<!doctype html><html lang=en><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<meta name=color-scheme content='light dark'>"
        f"<title>{title} &middot; CrossPlay</title>"
        f"<style>{CSS}</style>"
        f'<header class=band><a href="/">{MARK}<span>CrossPlay</span></a>'
        f"<span class=note>{SERVICE}</span></header><div class=rule></div>"
        f"<main>{rail}{body}</main></html>"
    )
