#!/usr/bin/env python3
"""One page. If it needs two, it is not a reference."""

from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.platypus import (
    SimpleDocTemplate,
    Paragraph,
    Spacer,
    Table,
    TableStyle,
    HRFlowable,
)

# Write into THIS checkout's docs/, wherever this checkout is. An absolute
# path here once pointed at the integration tree, so running the generator
# from a worktree wrote into a tree it was not working in.
from pathlib import Path

OUT = str(Path(__file__).resolve().parents[2] / "docs" / "apps" / "study-quick-reference.pdf")

styles = getSampleStyleSheet()
H1 = ParagraphStyle(
    "H1",
    parent=styles["Title"],
    fontName="Helvetica-Bold",
    fontSize=17,
    leading=20,
    spaceAfter=1,
    alignment=0,
)
SUB = ParagraphStyle(
    "SUB",
    parent=styles["Normal"],
    fontName="Helvetica",
    fontSize=8.5,
    leading=11,
    textColor=colors.HexColor("#555555"),
    spaceAfter=7,
)
H2 = ParagraphStyle(
    "H2",
    parent=styles["Normal"],
    fontName="Helvetica-Bold",
    fontSize=9.5,
    leading=12,
    spaceBefore=8,
    spaceAfter=3,
    textColor=colors.HexColor("#111111"),
)
BODY = ParagraphStyle(
    "BODY",
    parent=styles["Normal"],
    fontName="Helvetica",
    fontSize=8.2,
    leading=10.4,
    spaceAfter=2,
)
CODE = ParagraphStyle(
    "CODE",
    parent=styles["Normal"],
    fontName="Courier",
    fontSize=7.1,
    leading=9,
    textColor=colors.HexColor("#1a1a1a"),
    backColor=colors.HexColor("#f4f4f4"),
    borderPadding=(4, 4, 4, 4),
    spaceBefore=2,
    spaceAfter=4,
)
NOTE = ParagraphStyle(
    "NOTE", parent=BODY, fontSize=7.8, leading=10, textColor=colors.HexColor("#444444")
)

doc = SimpleDocTemplate(
    OUT,
    pagesize=A4,
    leftMargin=16 * mm,
    rightMargin=16 * mm,
    topMargin=13 * mm,
    bottomMargin=12 * mm,
    title="Study - quick reference",
    author="CrossPlay / Xteink X4 Pro",
)

s = []
s.append(Paragraph("Study", H1))
s.append(
    Paragraph(
        "Anki flashcards on the Xteink X4 Pro. FSRS scheduling, your own decks, "
        "your own fonts. Reviews sync back to Anki.",
        SUB,
    )
)
s.append(HRFlowable(width="100%", thickness=1.1, color=colors.black, spaceAfter=6))

# --- the loop -------------------------------------------------------------
s.append(Paragraph("The daily loop", H2))
loop = Table(
    [
        [
            Paragraph("<b>1. Put a deck on the card</b>", BODY),
            Paragraph("<b>2. Study</b>", BODY),
            Paragraph("<b>3. Sync back</b>", BODY),
        ],
        [
            Paragraph(
                "Convert from your live Anki collection, copy to the SD card.", NOTE
            ),
            Paragraph(
                "Apps &gt; STUDY. Tap to reveal, tap a button to grade, UNDO to take one back.",
                NOTE,
            ),
            Paragraph("One command applies your reviews and pushes to AnkiWeb.", NOTE),
        ],
    ],
    colWidths=[58 * mm, 58 * mm, 58 * mm],
)
loop.setStyle(
    TableStyle(
        [
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 0),
            ("RIGHTPADDING", (0, 0), (-1, -1), 8),
            ("TOPPADDING", (0, 0), (-1, -1), 1),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 2),
        ]
    )
)
s.append(loop)

# --- commands -------------------------------------------------------------
s.append(Paragraph("The two commands", H2))
s.append(
    Paragraph(
        "Run from <font face='Courier'>firmware-next/</font>. Both work out what they can "
        "and ask once for what they cannot, then remember it.",
        BODY,
    )
)

s.append(Paragraph("<b>Once</b> -- puts a deck and its fonts on the card", BODY))
s.append(Paragraph("./tools_local/study/study.py setup", CODE))
s.append(
    Paragraph(
        "Finds your Anki profile, lists your decks so you pick one, finds the SD card, "
        "converts, and builds the fonts only if they are missing or no longer cover the "
        "deck. Run it again to switch deck. Nearly any note type converts (first field = "
        "word, second = meaning; <font face='Courier'>--map</font> overrides; cloze "
        "notes become one card per hole). Non-CJK decks need no fonts; setup offers a big "
        "headword face from a system font, or bring one with "
        "<font face='Courier'>--font YourFont.ttf</font>. The long version of everything "
        "on this page is docs/apps/study.md.",
        NOTE,
    )
)

s.append(
    Paragraph("<b>After every session</b> -- puts your reviews back into Anki", BODY)
)
s.append(Paragraph("./tools_local/study/study.py sync --ankiweb", CODE))
s.append(
    Paragraph(
        "Refuses to run while Anki is open, backs the collection up first, and is safe to "
        "re-run. Drop <font face='Courier'>--ankiweb</font> to apply locally only; add "
        "<font face='Courier'>--dry-run</font> to look before you leap, or "
        "<font face='Courier'>--reconvert</font> to refresh the card afterwards.",
        NOTE,
    )
)

s.append(Paragraph("<b>Any time</b> -- what is set up, and what is waiting", BODY))
s.append(Paragraph("./tools_local/study/study.py status", CODE))
s.append(
    Paragraph(
        "Underneath are four single-purpose tools (anki_to_deck, deck_to_anki, make_fonts, "
        "check_deck) that take explicit paths. Reach for them when something goes wrong or "
        "you want one step only.",
        NOTE,
    )
)

# --- two columns ----------------------------------------------------------
left = []
left.append(Paragraph("On the SD card", H2))
left.append(
    Paragraph(
        "<font face='Courier'>/study/&lt;deck&gt;/</font> &nbsp;the deck, named after itself (~70 KB)<br/>"
        "<font face='Courier'>/study/fonts/</font> &nbsp;&nbsp;&nbsp;five CJK faces (~32 MB)",
        NOTE,
    )
)
left.append(
    Paragraph(
        "<b>deck.dat</b> card text, never written by the device.<br/>"
        "<b>cards.dat</b> scheduling state.<br/>"
        "<b>revlog.dat</b> your reviews, waiting to sync.",
        NOTE,
    )
)
left.append(Paragraph("Retraining FSRS", H2))
left.append(
    Paragraph(
        "There is no optimiser on the device and there should not be. Sync your reviews, "
        "then in Anki run <b>Deck options &gt; FSRS &gt; Optimize</b>. Then run "
        "<font face='Courier'>study.py setup</font> again -- the new weights travel with the deck.",
        NOTE,
    )
)

right = []
right.append(Paragraph("Good to know", H2))
right.append(
    Paragraph(
        "<b>Fonts change every card.</b> Five faces, picked at random, so you never lock "
        "onto one letterform. Same five your Anki template uses.",
        NOTE,
    )
)
right.append(
    Paragraph(
        "<b>Scheduling matches Anki.</b> Same FSRS weights, same learning steps, read from "
        "your deck preset. Individual intervals differ by a few percent only because Anki "
        "randomises its own and this does not.",
        NOTE,
    )
)
right.append(
    Paragraph(
        "<b>Reviews are never lost.</b> Every answer is flushed to the card immediately, "
        "and sync is idempotent.",
        NOTE,
    )
)
right.append(
    Paragraph("<b>Back</b> leaves at any point. Everything else is a tap.", NOTE)
)

cols = Table([[left, right]], colWidths=[86 * mm, 92 * mm])
cols.setStyle(
    TableStyle(
        [
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (0, 0), 0),
            ("LEFTPADDING", (1, 0), (1, 0), 8),
            ("RIGHTPADDING", (0, 0), (-1, -1), 0),
            ("TOPPADDING", (0, 0), (-1, -1), 0),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 0),
        ]
    )
)
s.append(cols)

# --- not there yet --------------------------------------------------------
s.append(
    HRFlowable(
        width="100%",
        thickness=0.6,
        color=colors.HexColor("#999999"),
        spaceBefore=8,
        spaceAfter=5,
    )
)
s.append(Paragraph("Not there yet", H2))
gaps = Table(
    [
        [
            Paragraph("<b>Sentence images</b>", NOTE),
            Paragraph(
                "290 of your 301 cards have one. The device shows text only, so those cards "
                "are missing a cue Anki gives you. The biggest gap.",
                NOTE,
            ),
        ],
        [
            Paragraph("<b>Real hardware</b>", NOTE),
            Paragraph(
                "Everything so far is verified in the simulator through the real render path. "
                "Untested on the device itself.",
                NOTE,
            ),
        ],
        [
            Paragraph("<b>Several decks</b>", NOTE),
            Paragraph(
                "Run setup once per deck; adding alongside is the default. The DECK 1/2 row "
                "on the reader's deck screen cycles between them, and sync covers them all.",
                NOTE,
            ),
        ],
        [
            Paragraph("<b>Leech tags</b>", NOTE),
            Paragraph(
                "Your leech action is Tag Only, so Anki keeps showing them too -- the device "
                "already matches. Only the tag itself is not added here; lapses still sync.",
                NOTE,
            ),
        ],
        [
            Paragraph("<b>Undo</b>", NOTE),
            Paragraph(
                "UNDO in the footer takes back the last answer: the card comes straight back on its "
                "answer side, ready to re-grade. One level, and only during a session -- once "
                '"Done" is showing, the last card of the day is settled.',
                NOTE,
            ),
        ],
        [
            Paragraph("<b>No editing</b>", NOTE),
            Paragraph("No adding, editing, or custom study. Do those in Anki.", NOTE),
        ],
        [
            Paragraph("<b>No audio</b>", NOTE),
            Paragraph("The X4 Pro has no speaker. Permanent.", NOTE),
        ],
    ],
    colWidths=[26 * mm, 152 * mm],
)
gaps.setStyle(
    TableStyle(
        [
            ("VALIGN", (0, 0), (-1, -1), "TOP"),
            ("LEFTPADDING", (0, 0), (-1, -1), 0),
            ("RIGHTPADDING", (0, 0), (-1, -1), 0),
            ("TOPPADDING", (0, 0), (-1, -1), 1.2),
            ("BOTTOMPADDING", (0, 0), (-1, -1), 1.2),
        ]
    )
)
s.append(gaps)

s.append(Spacer(1, 5))
s.append(
    Paragraph(
        "Full detail: docs/apps/study-deck-format.md &nbsp;&middot;&nbsp; "
        "scheduler notes in src/apps_local/study/StudyFsrs.h",
        NOTE,
    )
)

doc.build(s)
print(f"wrote {OUT}")
