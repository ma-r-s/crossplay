# Translators

Below is a list of translator credits for languages with known contributors.
Official UI language support is determined by the YAML files in
`lib/I18n/translations/`; see [i18n.md](./i18n.md) for the current supported
language list.

## Contributing

If you'd like to add your name to this list, please open a PR adding yourself and your Github link. Thank you!

## French
- [Spigaw](https://github.com/Spigaw)
- [CaptainFrito](https://github.com/CaptainFrito)

## German
- [DavidOrtmann](https://github.com/DavidOrtmann)

## Czech
- [brbla](https://github.com/brbla)

## Portuguese (Brazil)
- [yagofarias](https://github.com/yagofarias)

## Portuguese (Portugal)
- [victordomingos](https://github.com/victordomingos)

## Italian
- [andreaturchet](https://github.com/andreaturchet)
- [fragolinux](https://github.com/fragolinux)
- [alan0ford](https://github.com/alan0ford)

## Russian
- [madebyKir](https://github.com/madebyKir)
- [mrtnvgr](https://github.com/mrtnvgr)

## Spanish
- [yeyeto2788](https://github.com/yeyeto2788)
- [Skrzakk](https://github.com/Skrzakk)
- [pablohc](https://github.com/pablohc)
- [DaniPhii](https://github.com/DaniPhii)
- [lpla](https://github.com/lpla)

## Swedish
- [dawiik](https://github.com/dawiik)
- [steka](https://github.com/steka)

## Romanian
- [ariel-lindemann](https://github.com/ariel-lindemann)

## Catalan
- [angeldenom](https://github.com/angeldenom)
- [lpla](https://github.com/lpla)

## Finnish
- [plahteenlahti](https://github.com/plahteenlahti)

## Ukrainian
- [mirus-ua](https://github.com/mirus-ua)
- [KymAndriy](https://github.com/KymAndriy)

## Belarusian
- [Dexif](https://github.com/dexif)

## Danish
- [hajisan](https://github.com/hajisan)

## Some strings cannot wrap, and have a hard pixel budget

A number of screens draw a string through `renderer.drawCenteredText`, which
renders ONE line and does not wrap. On those keys a translation that is longer
than the panel is wide does not spill onto a second line -- it runs off the
edge and the reader never sees the end of it.

**The panel is 480px.** The budget is pixels, not characters: the face is
proportional, so "iiiii" and "WWWWW" differ by about threefold, and the bold
face is roughly 5% wider again.

Measure before you commit a translation:

```bash
tools_local/i18n/measure_string.py "your translated string"
host-tests/i18nwidth/run.sh --all-languages
```

`host-tests/i18nwidth/run.sh` gates ENGLISH only. It is not a statement about
your language. When this was written, **25 of the 32 translations had at least
one overflowing unwrapped string** -- worst cases on the recovery and sync
screens, some more than 250px past the edge. English happens to be one of the
few that fit, which is exactly why its green must not be read as "it fits
everywhere".

The keys most affected are the hints: `STR_RECOVERY_MODE_HINT`,
`STR_POWER_ON_HINT`, `STR_SYNC_READY`, `STR_CLEAR_CACHE_WARNING_1`. If yours is
over, prefer a shorter sentence that keeps the ACTION over a faithful
translation that keeps the explanation. The reader needs to know what to do.
