# Calculator

A calculator for the X4 Pro, in the TOYBOX look. What is settled, and the things
that had to be fixed at the source rather than styled around.

## TOYBOX, and the four that were deleted

Mario chose TOYBOX on 2026-09-15 out of five looks rendered side by side. The
other four -- INSTRUMENT, NIGHT, SWISS, LEDGER -- and the `Skin` struct that
carried them are gone in the same commit, along with the Ubuntu Bold and Noto
Serif cuts only they used. A variant macro that survives a decision is a second
design nobody maintains, and those two faces were 450KB of flash for a look
nothing draws.

Two rules from that pass are settled and stay:

**No rounded corners.** Mario's call, twice.

**No unexplained space.** Every vertical band is DERIVED from a cut metric, not
picked: `displayHeight()` is the pending line's own line height, plus the
number's cap height and its air, plus the rule. Nothing is left over.

That is not tidiness, it is the fix for a real defect. In the first pass every
skin carried a guessed display height, the number was pinned to the bottom of it,
and the remainder came out as a band of empty panel over every result --
different in each skin, explained by nothing. `host-tests/calculator` now refuses
a layout that leaves more than one pixel a row unaccounted for, and
`label_fit.py` checks `CalcCutMetrics.h` against the real font headers, so a
regenerated cut cannot move a band without going red. The derivation earned
itself immediately: LEDGER asked for four tape lines, which left 55px key rows
against a 61px touch floor, and the suite said so.

## Nothing can overflow, and the bound is at the source

Mario, 2026-09-15: *"I NEVER want to see overlapping text with the borders or
numbers that dont read nicely."*

A display cannot promise that by being careful about what it draws. It has to be
impossible, and the only place it can be made impossible is where the string is
produced.

**`kMaxDisplayChars` is 16**, and it is a limit on the ENGINE. Sixteen characters
is what the smallest of the four number cuts clears on this panel. Twelve of them
is a sign, ten digits and a point, which is why the engine carries **ten**
significant digits; the other four are for the exponent form, `-9.999999999e-99`.

Above that bound the display picks the largest of four cuts the string actually
fits in -- 56, 44, 34, 26 -- **measured** through the renderer's own advance
widths rather than counted, because Jersey's digits are not tabular: a `1` is
37px against a `0` at 57px in the 56 cut, so counting characters would step every
number down a size it did not need.

### The claim was false once, and that is why it is checked this hard

The first version of this section said the same thing and was wrong. `pressPercent`
wrote its result back into the TYPED entry, filling the full sixteen-character
budget -- and then `refresh()` prepended a sign and Dot appended a point, neither
of them counted. `9999999999 x 9999999999 = % % . +/-` put **eighteen**
characters on the panel, and it failed to clip only because the widest reachable
string came to 445px in a 448px box. Three pixels.

A cold review found it by walking the pad at random: 86 strings over the bound
out of 627,772. The list of "worst cases" the gate had been measuring was nine
key sequences somebody wrote down, none of them eighteen characters long.

Three things changed, and each closes the hole at a different depth:

- **A percent result is a RESULT.** It goes into a computed operand, not into
  editable text. Nothing can be typed on top of a formatted string any more --
  which also closed both routes by which Infinity and NaN used to reach the
  display and render as `0`.
- **The gate drives the engine instead of listing its limits.** 400,000 random
  key presses, every distinct string captured with the cut it is really drawn in,
  every one measured against its box. That is 250,000 checks and it is the claim
  itself, not a sample of it.
- **The draw site can refuse.** It measures every rung including the last -- the
  first version measured three of four and returned the fourth unchecked -- and
  if nothing fits it says so rather than drawing through the border.

## The symbols are type now, and that was the whole problem

The first five renders had hand-drawn operator glyphs and they looked homemade,
because they were. The cause was one line in `gen_toybox_fonts.sh`: the Toybox
cuts are Jersey 25 **subset to U+0020-007E**, so the division sign, the
multiplication sign and the minus sign draw as NOTHING -- a glyph the face lacks
is a hole, not a box. Jersey has had all three all along.

`tools_local/toybox/gen_calc_fonts.sh` cuts the calculator's own faces with the
math block included. They are NEW files, never wider versions of the Toybox
cuts, because `gen_toybox_fonts.sh` spells out at length that regenerating
`toybox_20` or `_30` today moves every glyph a pixel and silently shifts text in
every app in the fork. A cut nothing else uses cannot do that to anybody.

Five cuts per face, not one: **a calculator sets its word keys smaller than its
digits.** Look at any of them. Here it is also the only way DEL fits a key at
all -- Ubuntu Bold draws it 103px wide at 26px, in a 103px key. `labelFontFor`
picks by label length, structurally, so the host gate can resolve the same face
the panel will.

Jersey has no U+00B1, so the sign key prints **+/-**, which is the spelling a
keyboard-era calculator uses. An invisible key is not a convention.

## The gate that catches a label before a person does

`host-tests/calculator/label_fit.py` measures every key label, in the cut its
skin resolves, against the cell that skin produces. A host test cannot parse a
font header and a screenshot only shows the skin somebody photographed, so this
is the only thing that can see the failure. It found Ubuntu Bold's DEL within a pixel and a half of its border on both sides,
and Noto Serif's **fifteen pixels wider than its key** -- in a look whose render
nobody had looked at yet.

It checks the invisible half too: a codepoint the face has no glyph for is
reported rather than silently costing zero width.

## The pad is hit-tested against geometry, not registered

`toybox::kMaxInteractions` is 24. Twenty keys fits, but a screen that goes past
it loses the **last** ones registered, on the device only, silently -- they
draw, they look live, and they answer nothing. That is how the Connections
archive shipped with every date from the 20th onward dead. So `calc::keyRect` is
the one geometry function, `drawPad` draws from it and `loop()` hit-tests
against it.

The suite probes every key's centre **and its four corners**, and requires one
pixel past each edge to belong to something else. Centres alone are not enough
and this suite said they were until a mutation run proved otherwise: shifting
every hit rect sideways by one gap left each key's own centre inside its own
wrong rect, and the whole pad passed.

## What is settled about the KEYS

The arithmetic is the next section. This is the other half, and it is the half
with no library in it: every case below is a place calculators are commonly
wrong, and all of them are pinned.

- **Percent reads the pending operator.** `200 + 10 %` is 220; `200 x 10 %` is
  20. Not one operation, a convention.
- **`2 + 3 = = =` is 5, 8, 11.** Equals repeats the operator and the operand.
- **Two operators in a row replace**, they do not stack.
- **An error is a wall.** Divide by zero says so and then refuses every key but
  clear, rather than letting a digit land on top of the message.
- **The eleventh typed digit is refused**, not accepted and silently rounded --
  which is also what keeps an operand from ever being longer than the working
  precision, where decNumber has a published erratum.
- **There is no negative zero**, typed or computed. Pressing `0` then `+/-`
  holds the sign without drawing it; a digit after that is negative.
- **Every sequence is legal input.** A pad has twenty keys and no grammar, so an
  operator as the first key, `=` with nothing pending, a digit after `=`,
  backspace on a result and `...` all have to leave a number on the panel.
  Nineteen of them are pinned; writing them down is what found the negative
  zero.

## The arithmetic, and why it is a vendored library

Settled 2026-09-15, by Mario: *"I NEEDS to work as a real calc, no
0.1 + 0.2 = 0.300000001."*

Twelve-digit display rounding over a `double` fixes every case where the error is
small against the result and **none** where the result itself is near zero:

    0.1 + 0.2        ->  0.3                   fixed by rounding
    0.1 + 0.2 - 0.3  ->  5.55111512313e-17     NOT fixed, the error IS the answer

Casio and TI show `0` there because their arithmetic is decimal, not because
their displays are cleverer. So is ours now: `lib/decNumber`, IBM's decNumber
under the ICU licence, vendored the way `lib/miniz` is. About 187KB of flash for
the app, the library and five font cuts together; x4pro sits at 81.5% of its
slot.

Four details that are easy to get wrong and are all written down beside the code
that depends on them:

- **Sixteen working digits, ten shown.** The six hidden guard digits are why
  `(1 / 3) x 3` reads `1`. Every physical calculator carries them.
- **`ctx.traps = 0`**, or `decContextSetStatus` calls `raise(SIGFPE)` and the
  first divide by zero aborts the firmware.
- **`DECNUMDIGITS` 20, not 34**: `decDivideOp` falls back to `malloc` as the
  precision approaches `DECBUFFER`. `no_malloc_probe.cpp` proves zero calls.
- **`0/0` is `DEC_Division_undefined`**, a different bit from
  `DEC_Division_by_zero`.

## What is NOT settled

### Percent on x and /

`500 x 5 %` is **25** on iOS and Casio and **12500** on Windows. The engine does
the iOS thing, and `testPercentReadsThePendingOperator` is the one line that
changes.

## What no library does, and what one does

The ARITHMETIC is vendored, above. No PARSER is: `tinyexpr` (zlib, ~1000 lines,
6.5KB of flash measured on an ESP32-S3) is the right one if a typed-expression
mode is ever wanted -- built as `.c`, not C++, and with `-DTE_POW_FROM_RIGHT
-DTE_NAT_LOG`, because its defaults make `-2^2` be 4. This pad does not need it:
it is an immediate-execution machine and there is no expression to parse.

What no library anywhere supplies is the input state machine, the percent
convention, repeated equals and the display formatting -- and in particular
nothing formats to a CHARACTER BUDGET, which is the half that stops overflow. That is this app's own
~350 lines and its suites. Rejected with reasons: tinyexpr++ (C++20, 76 `throw`
sites, and exceptions are off here), muParser (exceptions are its only error
channel), ExprTk (RTTI and a 1.66MB header), Windows Calculator's RatPack
(genuinely exact, but throws raw ints and returns `std::wstring`).

## Still to measure

**Every keypress is a whole-screen refresh, and nobody knows what one costs on
an X4 Pro.** `docs/open-items.md` has this open: the SDK says "0.3-2 s" and that
is the whole of our knowledge. A calculator is the app that cares most. The
existing evidence that it is tolerable is the on-screen QWERTY keyboard people
already type Wi-Fi passwords on at 46px keys; these are twice that. One thing
works in our favour, and it is NOT the reveal gate, whatever this page said
before: that rule governs the interaction TABLE, and this screen registers
nothing in it -- the pad is hit-tested against geometry. What actually keeps
rapid taps alive is that the geometry cannot change between frames, so a tap
landing during a repaint lands on the key it would have landed on before it.
