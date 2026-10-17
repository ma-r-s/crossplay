// A title must show its whole string. Asked of every real string there is.
//
// Mario's rule, given on 2026-09-05 about Connections tiles: "a tile needs to
// show the whole text, no exceptions." ToyboxFonts.h has stated the mechanism
// for it since the reading cuts were added -- "pick the largest cut it fits in,
// walking the available cuts down, and only break a word when the smallest
// still overflows" -- and until this suite there was no toybox:: function that
// did it. Six apps had written their own ladder and the rest of the fork had
// none, so a title handed to a header band was cut by the renderer instead.
//
// WHAT THIS SUITE IS FOR, and why it is not a list of examples: every corpus
// below is COMPLETE. Every dungeon guide page, every Forehead category, every
// linkplay game, every Toy Battle map, every date the Connections header can
// format, and every comic title in the pack on the card. A sampled suite would
// pass on the day somebody adds the one long name -- which is precisely how
// "CURSED CEMETERY" reached a panel as "CURSED CEMETER".
//
// The instrument is real_target.h: real cuts, real EpdFont measurement, and the
// renderer's own truncation reproduced, so the question it answers is "what
// would the panel show", not "what did the builder pass".

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ConnectionsScreens.h"
#include "DungeonPuzzles.h"
#include "DungeonScreens.h"
#include "ForeheadScreens.h"
#include "ForeheadWords.h"
#include "HackerNewsScreens.h"
#include "HeartsScreens.h"
#include "LinkScreens.h"
#include "ToyBattleCore.h"
#include "ToyBattleMenus.h"
#include "ToyBattleScreens.h"
#include "ToyboxScreen.h"
#include "ToyboxText.h"
#include "Utf8.h"
#include "XkcdScreens.h"
#include "corpus.generated.h"
#include "fonts/instrument_10.h"
#include "fonts/instrument_13.h"
#include "fonts/instrument_24.h"
#include "fonts/reading_serif_11.h"
#include "fonts/reading_serif_14.h"
#include "fonts/reading_serif_bold_12.h"
#include "fonts/reading_serif_bold_16.h"
#include "fonts/toybox_10.h"
#include "fonts/toybox_14.h"
#include "fonts/toybox_20.h"
#include "fonts/toybox_30.h"
#include "fonts/toybox_44.h"
#include "fonts/toybox_64.h"
#include "real_target.h"

namespace {

int checks = 0;
int failed = 0;

// What the suite actually walked, counted rather than described. A total
// written into a commit message is a total that was right once.
int walked = 0;
int residual = 0;

void ok(const bool condition, const std::string& what) {
  ++checks;
  if (!condition) {
    ++failed;
    std::printf("FAIL fittedtitle  %s\n", what.c_str());
  }
}

// The panel, and the bezel the X4 Pro's glass really hides (docs/bezel-insets).
// Not an empty safe area: an empty one is what lets a doubled margin pass
// unnoticed, and the header band's ink top is derived from it.
fui::DeviceContext panel() {
  fui::DeviceContext device;
  device.width = 480;
  device.height = 800;
  device.hasTouch = true;
  device.safeArea = fui::Insets{10, 1, 0, 1};
  return device;
}

fitted::Faces facesNamed(const char* name) {
  for (int i = 0; i < fitted::kFaceSetCount; ++i) {
    const fitted::FaceSet& set = fitted::kFaceSets[i];
    if (std::strcmp(set.name, name) != 0) continue;
    return fitted::Faces{fitted::dataForId(set.small), fitted::dataForId(set.body), fitted::dataForId(set.title)};
  }
  std::printf("FAIL fittedtitle  no Faces set named %s in ToyboxTheme.h\n", name);
  ++failed;
  return fitted::Faces{&toybox_10, &toybox_20, &toybox_30};
}

// One screen, drawn for real.
struct Paint {
  explicit Paint(const char* faces) : target(facesNamed(faces)) {}
  fitted::RealTarget target;
  toybox::Interactions interactions;
};

// The run that carries `source`, wherever on the screen it ended up.
//
// A title can lose its tail two ways and they look identical on the panel: the
// app shortens it before handing it over (Hacker News and the xkcd bar both
// do), or the renderer truncates what it was handed. Matching on "the drawn
// text is a prefix of the source, ignoring any ellipsis" catches both with one
// rule, which is the point -- from a reader's side they are one defect.
const fitted::TextRun* carrier(const std::vector<fitted::TextRun>& runs, const std::string& source) {
  const fitted::TextRun* best = nullptr;
  size_t bestLength = 0;
  for (const fitted::TextRun& run : runs) {
    std::string body = run.drawn;
    for (const char* mark : {"\xe2\x80\xa6", "..."}) {
      const size_t len = std::strlen(mark);
      if (body.size() >= len && body.compare(body.size() - len, len, mark) == 0) {
        body.resize(body.size() - len);
        break;
      }
    }
    while (!body.empty() && body.back() == ' ') body.pop_back();
    if (body.empty() || source.compare(0, body.size(), body) != 0) continue;
    if (best == nullptr || body.size() > bestLength) {
      best = &run;
      bestLength = body.size();
    }
  }
  return best;
}

// The assertion, for one string on one screen.
//
// TWO NUMBERS, and only one of them is gated, because only one of them is a
// property this change can construct.
//
//   avoidable  a string reached the panel short WHILE ONE OF THE CUTS THIS
//              SCREEN BOUND WOULD HAVE SHOWN IT WHOLE. That is the rule
//              ToyboxFonts.h states and toybox::fittedTitle now keeps, and it
//              is what fails the suite. Zero, everywhere, no exceptions.
//
//   residual   a string that no bound cut can show whole on one line. The bar
//              at the foot of the xkcd reader is 336px and some comic titles
//              are two hundred pixels of type past that at the smallest face
//              the reader binds. A ladder cannot fix those; a second line or a
//              smaller cut in the app's Faces set could. REPORTED, not gated,
//              and reported with a count so it cannot quietly grow -- the same
//              shape host-tests/i18nwidth's all-language survey uses, and for
//              the same reason: a gap published as a number is a gap somebody
//              can act on, and a gap folded into a pass is one nobody sees.
struct Tally {
  const char* what;
  int walked = 0;
  int missing = 0;    // never reached the panel at all
  int avoidable = 0;  // cut, and a bound cut would have shown it whole
  int residual = 0;   // cut, and no bound cut could have shown it
  std::string worst;
  std::string worstDrawn;
};

// Would ANY of the three cuts this screen bound have taken the whole string in
// the width it was drawn into? Asked of the target, so it is the real face.
bool aCutWouldHaveFitted(const fitted::RealTarget& target, const fitted::TextRun& run, const std::string& source) {
  const fui::FontId slots[3] = {fui::FONT_SLOT_SMALL, fui::FONT_SLOT_BODY, fui::FONT_SLOT_TITLE};
  for (const fui::FontId slot : slots) {
    if (target.measureText(slot, source.c_str(), fui::TextStyle{}).width <= run.rect.width) return true;
  }
  return false;
}

// WHOLE IS NOT THE SAME QUESTION AS FULL SIZE, and this suite only ever asked
// the first one.
//
// expectWhole passes when the panel would show the entire string. A string that
// dropped a cut to fit IS entire -- so "PICK THREE TO PASS ACROSS" came back
// green while rendering at half the cap height of "PICK THREE TO PASS LEFT",
// beside it, one word different, in a box it then left 190px of empty. The
// corpus was right and the assertion was the wrong one.
//
// Everything Hearts draws is at a chosen cut, so a downgrade is a defect rather
// than the ladder working. This asks the run which font really drew it.
void expectAtCut(Tally& tally, const fitted::RealTarget& target, const std::string& source, const fui::FontId wanted) {
  ++tally.walked;
  const fitted::TextRun* run = carrier(target.texts, source);
  if (run == nullptr) {
    ++tally.missing;
    if (tally.worst.empty()) {
      tally.worst = source;
      tally.worstDrawn = "(never drawn)";
    }
    return;
  }
  if (run->drawn != source) {
    ++tally.avoidable;
    if (tally.worst.size() < source.size()) {
      tally.worst = source;
      tally.worstDrawn = run->drawn;
    }
    return;
  }
  if (run->font != wanted) {
    ++tally.avoidable;
    if (tally.worst.size() <= source.size()) {
      tally.worst = source;
      tally.worstDrawn = "(shrunk a cut to fit)";
    }
  }
}

// EVERY RUN ON THIS SCREEN, rather than a list of strings somebody remembered.
//
// The tally below used to name seven strings in its comment and assert two of
// them, which is a clean list hiding an absence one level down from the one it
// was written to close. Listing them is also the wrong shape: the list has to
// be maintained beside the app, and the string that gets missed is the one
// somebody adds next.
//
// Nothing in Hearts steps its own cut any more -- fittedLabel is gone -- so the
// only way a string can come up short is the renderer truncating it, and that
// is a property of the run rather than of a corpus. Asking every run closes the
// whole screen and keeps closing it.
void expectNothingCut(Tally& tally, const fitted::RealTarget& target, const char* screen) {
  for (const fitted::TextRun& run : target.texts) {
    if (run.asked.empty()) continue;
    ++tally.walked;
    if (!run.cut()) continue;
    ++tally.avoidable;
    if (tally.worst.size() < run.asked.size()) {
      tally.worst = std::string(screen) + ": " + run.asked;
      tally.worstDrawn = run.drawn;
    }
  }
}

void expectWhole(Tally& tally, const fitted::RealTarget& target, const std::string& source) {
  ++tally.walked;
  const fitted::TextRun* run = carrier(target.texts, source);
  if (run == nullptr) {
    ++tally.missing;
    if (tally.worst.empty()) {
      tally.worst = source;
      tally.worstDrawn = "(never drawn)";
    }
    return;
  }
  if (run->drawn == source) return;
  const bool avoidable = aCutWouldHaveFitted(target, *run, source);
  if (avoidable) {
    ++tally.avoidable;
  } else {
    ++tally.residual;
  }
  if (tally.worstDrawn.empty() || source.size() > tally.worst.size()) {
    tally.worst = source;
    tally.worstDrawn = run->drawn;
  }
}

// `allowed` is the number of avoidable cuts this corpus is KNOWN to have and a
// person has decided to keep. It is a pin, not an excuse: the check is
// equality, so the count failing UPWARD and failing DOWNWARD both go red, and
// the reason has to be written at the call site. Every corpus but one passes 0.
void report(const Tally& tally, const int allowed = 0, const char* why = nullptr) {
  walked += tally.walked;
  residual += tally.residual;
  // A corpus that walked NOTHING is not a corpus that passed. Every one below
  // is either a table in this repository or a range this suite generates, so an
  // empty one means the generator stopped matching, not that the app lost its
  // screens -- and `0 walked 0 avoidable` reads exactly like a clean run.
  ok(tally.walked > 0, std::string(tally.what) + ": walked no strings at all");
  const int bad = tally.missing + tally.avoidable;
  std::printf("%-34s %5d walked  %4d avoidable  %4d residual  %3d never drawn\n", tally.what, tally.walked,
              tally.avoidable, tally.residual, tally.missing);
  if (tally.avoidable != 0 || tally.residual != 0 || tally.missing != 0) {
    std::printf("      longest short of it: %s\n                      -> %s\n", tally.worst.c_str(),
                tally.worstDrawn.c_str());
  }
  if (allowed != 0) {
    std::printf("      %d of those are a KEPT exception: %s\n", allowed, why == nullptr ? "(no reason given)" : why);
  }
  ok(bad == allowed, std::string(tally.what) + ": " + std::to_string(bad) + " of " + std::to_string(tally.walked) +
                         " strings were cut while a bound cut would have shown them whole, and " +
                         std::to_string(allowed) + " is the number this corpus is pinned at");
}

// --- The corpora ---------------------------------------------------------

// The fitted style has to SURVIVE Screen::header().
//
// headerBand() rewrites the title's font and hands the props on; Screen::header
// then replaces any title style that is "entirely default-constructed" with the
// theme's own. FONT_SLOT_SMALL is 0, so a title fitted all the way down to the
// small slot, with every other field left alone, is one field away from reading
// as a style nobody set -- and the substitution would put the display cut back
// and undo the fitting, silently, on exactly the longest strings.
//
// What stands between those two today is one token: Toybox's titleText is
// White, because the band is solid black. That is a real guarantee and an
// invisible one, so it is asserted here rather than trusted.
// THE LADDER ITSELF, which every corpus below is blind to.
//
// A cold review reintroduced the exact bug the shared function exists to fix --
// walking the cuts by slot NAME instead of by measured size, and dropping the
// "never above the cut the caller asked for" guard -- and every corpus below
// printed byte-identical output. They can only see a string arriving SHORT, and
// a ladder that steps the wrong way makes strings arrive BIG. So the two
// properties that only this code can break are asserted directly, against a
// target whose slots are deliberately out of name order.
//
// bigNumberFaces() and cardFaces() are not hypothetical shapes: they are two of
// the nine sets in ToyboxTheme.h, they put a 64px cut in a slot named BODY and
// a 44px one in a slot named SMALL, and Forehead binds both. Eleven of its
// seventeen category titles fit at the 64px cut, so without the ceiling eleven
// result screens would set their title at a 133px line height inside a 76px
// band -- and no corpus here would have said a word.
void theLadderItself() {
  const fitted::Faces bigNumber = facesNamed("bigNumberFaces");  // SMALL 29, BODY 133, TITLE 63
  fitted::RealTarget target(bigNumber);

  const int16_t small = target.lineHeight(fui::FONT_SLOT_SMALL);
  const int16_t body = target.lineHeight(fui::FONT_SLOT_BODY);
  const int16_t titleSlot = target.lineHeight(fui::FONT_SLOT_TITLE);
  ok(body > titleSlot && titleSlot > small,
     "bigNumberFaces no longer binds a BODY cut taller than its TITLE cut, so this test proves nothing");

  // Asked for the TITLE cut with room to spare: it must come back unchanged,
  // and must NOT be promoted to the taller cut sitting in BODY.
  {
    fui::TextStyle style;
    style.font = fui::FONT_SLOT_TITLE;
    style.color = fui::Color::White;
    const std::string out = toybox::fittedTitle(target, "MUSIC", 448, style);
    ok(out == "MUSIC", "a title that fits was altered");
    ok(style.font == fui::FONT_SLOT_TITLE, "fitting stepped UP into the taller cut bound to the BODY slot");
  }

  // Asked for the TITLE cut with too little room: it must step DOWN to the
  // small slot, never up, and never sideways into BODY.
  {
    fui::TextStyle style;
    style.font = fui::FONT_SLOT_TITLE;
    style.color = fui::Color::White;
    const std::string out = toybox::fittedTitle(target, "FAMOUS PEOPLE", 200, style);
    ok(out == "FAMOUS PEOPLE", "a title that fits a smaller cut was elided instead of shrunk");
    ok(style.font == fui::FONT_SLOT_SMALL, "fitting did not walk down to the smallest cut this screen bound");
  }

  // The LARGEST that fits, not merely one that fits. A ladder that jumped
  // straight to the smallest would pass every "was it cut" check in this file.
  {
    fui::TextStyle style;
    style.font = fui::FONT_SLOT_TITLE;
    style.color = fui::Color::White;
    const fui::Size atTitle = target.measureText(fui::FONT_SLOT_TITLE, "SCIENCE", style);
    const std::string out = toybox::fittedTitle(target, "SCIENCE", atTitle.width, style);
    ok(out == "SCIENCE", "a title measured to fit exactly was still shortened");
    ok(style.font == fui::FONT_SLOT_TITLE, "fitting stepped down from a cut the string fitted in exactly");
  }

  // cardFaces puts the 44px cut in SMALL and the 30px one in BODY, so "walk
  // TITLE, BODY, SMALL" would end on the LARGER of the two. Asked at BODY, the
  // only legal step is to stay or to shrink -- and SMALL is bigger here, so the
  // only legal answer is BODY itself.
  {
    const fitted::Faces card = facesNamed("cardFaces");  // SMALL 92, BODY 63, TITLE 133
    fitted::RealTarget cardTarget(card);
    ok(cardTarget.lineHeight(fui::FONT_SLOT_SMALL) > cardTarget.lineHeight(fui::FONT_SLOT_BODY),
       "cardFaces no longer binds a SMALL cut taller than its BODY cut, so this test proves nothing");
    fui::TextStyle style;
    style.font = fui::FONT_SLOT_BODY;
    style.color = fui::Color::White;
    toybox::fittedTitle(cardTarget, "LITTLE RED RIDING HOOD", 60, style);
    ok(style.font == fui::FONT_SLOT_BODY,
       "with nothing smaller bound, fitting moved to a LARGER cut rather than staying and marking");

    // And the case that separates MEASURED order from NAME order outright.
    // Under cardFaces the two smaller cuts are SMALL (92) and BODY (63), in
    // that order by size and the other way round by name. Given a width both
    // fit in, "largest that fits" is SMALL; a walk by name reaches BODY first
    // and stops, one rung smaller than it had to be. Nothing arrives cut either
    // way, which is why every corpus in this file is blind to it.
    fui::TextStyle wide;
    wide.font = fui::FONT_SLOT_TITLE;
    wide.color = fui::Color::White;
    const int16_t atSmall = cardTarget.measureText(fui::FONT_SLOT_SMALL, "FOX", wide).width;
    const int16_t atTitleCut = cardTarget.measureText(fui::FONT_SLOT_TITLE, "FOX", wide).width;
    ok(atTitleCut > atSmall, "cardFaces' TITLE cut is no longer wider than its SMALL cut");
    toybox::fittedTitle(cardTarget, "FOX", atSmall, wide);
    ok(wide.font == fui::FONT_SLOT_SMALL,
       "fitting took a SMALLER cut than it had to: the rungs were walked by slot name, not by measured size");
  }
}

void themeKeepsTheFittedCut() {
  fui::TextStyle title = toybox::themeTokens().titleText;
  title.align = toybox::themeTokens().headerTitleAlign;
  title.font = fui::FONT_SLOT_SMALL;
  ok(!fui::textStyleUnset(title),
     "a title fitted down to the small slot reads as unset, so Screen::header would put the display cut back");
}

void dungeonGuide() {
  Tally tally{"dungeon: guide page titles"};
  // The corpus comes from the TABLE, never from the panel. Read off the panel
  // it was circular: headerBand rewrites props.title to the fitted string
  // before the component draws it, so "what was drawn" and "what was expected"
  // were the same object and the check could not fail. A cold review proved it
  // by truncating every title to five characters and watching this stay green.
  //
  // Which makes the count load-bearing: a table row the generator's pattern
  // misses is a page that silently stops being walked.
  ok(fitted::kDungeonGuideTitleCount == dungeonui::guidePageCount(),
     "corpus.py found a different number of dungeon guide pages than the app has");
  for (int page = 0; page < dungeonui::guidePageCount(); ++page) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    toybox::Screen screen(frame);
    dungeonui::GuideModel model;
    model.page = page;
    model.pageCount = dungeonui::guidePageCount();
    dungeonui::buildGuide(screen, model);
    expectWhole(tally, paint.target, fitted::kDungeonGuideTitles[page]);
  }
  report(tally);
}

// Every dungeon's name, on both screens that show it. The names are the app's
// longest data -- thirty-three characters at the top -- and the two screens
// treat them differently on purpose: the menu's foot is one line beside an icon
// and a TUTORIAL button, and the cleared screen is two centred lines with the
// map underneath.
// --- Hearts ---------------------------------------------------------------
//
// THE FIRST LANDSCAPE SCREEN IN THIS SUITE, and it is here because a cold
// play-tester found three separate strings that had been shrunk a cut, cut
// short, or both, on screens I had looked at. Measuring in the real face is the
// only way to see that: host-tests/ui answers a flat ten pixels a character and
// is blind to it entirely.
//
// Every string walked here is one the app draws at a FIXED cut, so a failure is
// a string that needs rewriting rather than a ladder that needs another rung.
fui::DeviceContext landscapePanel() {
  fui::DeviceContext device;
  device.width = 800;
  device.height = 480;
  device.hasTouch = true;
  device.safeArea = fui::Insets{1, 0, 1, 10};
  return device;
}

toybox::Screen heartsScreen(toybox::Frame& frame, fui::ThemeTokens& tokens) {
  tokens = toybox::themeTokens();
  tokens.headerHeight = heartsui::kHeaderBand;
  return toybox::Screen(frame, tokens);
}

void heartsRules() {
  Tally rules{"hearts: every line of the rules screen"};
  for (int page = 0; page < heartsui::howToPages(); ++page) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, landscapePanel(), fui::InputSnapshot{}, paint.interactions);
    fui::ThemeTokens tokens;
    toybox::Screen screen = heartsScreen(frame, tokens);
    heartsui::HowToModel model;
    model.page = page;
    heartsui::buildHowTo(screen, model);
    expectNothingCut(rules, paint.target, "rules");
    expectAtCut(rules, paint.target, heartsui::howToTitle(page), toybox::kDisplayFont);
    for (int line = 0; line < heartsui::howToLines(page); ++line) {
      expectAtCut(rules, paint.target, heartsui::howToLine(page, line), toybox::kUiFont);
    }
  }
  report(rules);
}

// The board's status row carries two strings side by side in one row, and the
// right-hand one is assembled from two halves. Every combination is walked
// rather than a sample: four states of the pair, plus all six refusals, which
// take the same box.
void heartsStatusLines() {
  Tally status{"hearts: board status and refusal lines"};
  uint32_t seed = 909;
  hearts::Game game;
  hearts::newGame(game, seed);
  for (int s = 0; s < hearts::kSeats; ++s) {
    uint8_t three[hearts::kPassCount] = {game.hands[s].at(0), game.hands[s].at(1), game.hands[s].at(2)};
    hearts::setPass(game, static_cast<hearts::Seat>(s), three, hearts::kPassCount);
  }
  hearts::commitPass(game);

  static const char* kNames[hearts::kSeats] = {"YOU", "WEST", "NORTH", "EAST"};
  // Every status template at its LONGEST instantiation, every refusal, and all
  // four states of the pair -- READ OUT OF THE APP rather than retyped here. A
  // corpus written into a test stops matching the app the day somebody rewords
  // a string, which is the failure this suite exists to catch.
  const int kStatuses = static_cast<int>(heartsui::Status::Count);
  const int kRefusals = static_cast<int>(heartsui::Refusal::Count);
  for (int variant = 0; variant < 4 + kStatuses + kRefusals; ++variant) {
    char sub[64];
    char longest[80];
    const char* main = nullptr;
    std::snprintf(sub, sizeof(sub), "%s   %s", heartsui::heartsStateText((variant & 1) != 0),
                  heartsui::queenStateText((variant & 2) != 0));
    if (variant < 4) {
      main = heartsui::statusTemplate(heartsui::Status::YourLead);
    } else if (variant < 4 + kStatuses) {
      heartsui::longestStatus(static_cast<heartsui::Status>(variant - 4), longest, sizeof(longest));
      main = longest;
    } else {
      main = heartsui::refusalText(static_cast<heartsui::Refusal>(variant - 4 - kStatuses));
    }
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, landscapePanel(), fui::InputSnapshot{}, paint.interactions);
    fui::ThemeTokens tokens;
    toybox::Screen screen = heartsScreen(frame, tokens);
    heartsui::BoardModel model;
    model.game = &game;
    for (int s = 0; s < hearts::kSeats; ++s) {
      model.seats[s].name = kNames[s];
      model.seats[s].initial = kNames[s][0];
      // THE WIDEST NUMBER THE PLAQUE CAN EVER SHOW. It draws total + taken, so
      // a seat on 99 that takes all 26 projects 125: three digits, which is
      // what the column has to hold and what it did not.
      model.seats[s].total = 99;
      model.seats[s].taken = hearts::kMoonPoints;
      model.legal[s] = true;
    }
    model.status = main;
    model.subStatus = sub;
    heartsui::Layout layout;
    heartsui::buildBoard(screen, model, layout);
    expectAtCut(status, paint.target, main, toybox::kUiFont);
    expectAtCut(status, paint.target, sub, toybox::kSmallFont);
    // And the four seat names, which must all survive at the shared cut: the
    // rail rendered three at cap 50 and NORTH at 26 when they were fitted
    // independently.
    for (int s = 0; s < hearts::kSeats; ++s) expectAtCut(status, paint.target, kNames[s], toybox::kUiFont);
    // AND EVERYTHING ELSE THE BOARD DREW. This tally was an explicit list of
    // six strings while the score and menu had been generalised, so the seat
    // plaques' running totals were in no corpus anywhere -- and that is exactly
    // where the next defect landed: 114 rendered as "1" on the largest number
    // on the board, for the whole last hand of every game. The same complement
    // that put the status-line defect and the three fittedLabel strings outside
    // their own suites, one screen over.
    expectNothingCut(status, paint.target, "board");
  }
  report(status);
}

// THE OTHER TWO SCREENS. 136 strings, green, and neither buildScore nor
// buildMenu was in the suite at all -- so TIED ON, WIN/WINS ON, SHOT THE MOON,
// PLAY AGAIN, HAND %d SCORED, DISCARD IT? and the four place names were
// measured by nothing. A clean list hiding an absence.
void heartsScoreAndMenu() {
  Tally sheet{"hearts: score screen and menu"};
  static const char* kNames[hearts::kSeats] = {"YOU", "WEST", "NORTH", "EAST"};

  // Three shapes of finished hand: ordinary, a moon (which adds a banner and
  // shortens every row), and game over (which inverts the winner).
  for (int shape = 0; shape < 3; ++shape) {
    hearts::Game game;
    for (int s = 0; s < hearts::kSeats; ++s) game.total[s] = shape == 2 ? 88 : 20;
    if (shape == 1) {
      game.taken[seatIndex(hearts::Seat::North)] = hearts::kMoonPoints;
    } else {
      game.taken[0] = 5;
      game.taken[1] = 8;
      game.taken[2] = 13;
    }
    hearts::scoreHand(game);

    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, landscapePanel(), fui::InputSnapshot{}, paint.interactions);
    fui::ThemeTokens tokens;
    toybox::Screen screen = heartsScreen(frame, tokens);
    heartsui::ScoreModel model;
    model.game = &game;
    for (int s = 0; s < hearts::kSeats; ++s) {
      model.seats[s].name = kNames[s];
      model.seats[s].initial = kNames[s][0];
      model.seats[s].isMe = s == 0;
    }
    model.gameOver = shape == 2;
    heartsui::buildScore(screen, model);
    for (int s = 0; s < hearts::kSeats; ++s) expectAtCut(sheet, paint.target, kNames[s], toybox::kUiFont);
    expectAtCut(sheet, paint.target, model.gameOver ? "PLAY AGAIN" : "NEXT HAND", toybox::kUiFont);
    expectAtCut(sheet, paint.target, "MENU", toybox::kUiFont);
    // And everything else this screen drew: the standings note, TIED ON, the
    // moon banner, the places, the rule line, every number.
    expectNothingCut(sheet, paint.target, "score");
  }

  // Both menu states, and the armed discard.
  for (int variant = 0; variant < 3; ++variant) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, landscapePanel(), fui::InputSnapshot{}, paint.interactions);
    fui::ThemeTokens tokens;
    toybox::Screen screen = heartsScreen(frame, tokens);
    heartsui::MenuModel model;
    model.hasSave = variant > 0;
    model.savedHand = 12;
    model.savedHandDone = variant == 2;
    model.confirmingNew = variant == 2;
    model.gamesPlayed = 99;
    model.gamesWon = 42;
    model.bestPlace = 4;
    model.sharp = variant == 0;
    heartsui::buildMenu(screen, model);
    expectAtCut(sheet, paint.target, "HEARTS", toybox::kDisplayFont);
    expectAtCut(sheet, paint.target, "RULES", toybox::kUiFont);
    expectAtCut(sheet, paint.target, model.sharp ? "TABLE: SHARP" : "TABLE: ROOKIE", toybox::kUiFont);
    expectAtCut(sheet, paint.target, model.hasSave ? "TABLE WAITING" : "FOUR SEATS", toybox::kDisplayFont);
    if (model.hasSave)
      expectAtCut(sheet, paint.target, model.confirmingNew ? "DISCARD IT?" : "NEW GAME", toybox::kUiFont);
    static const char* kCells[3] = {"GAMES", "WON", "BEST"};
    for (const char* cell : kCells) expectAtCut(sheet, paint.target, cell, toybox::kSmallFont);
    expectNothingCut(sheet, paint.target, "menu");
  }
  report(sheet);
}

void dungeonNames() {
  Tally menu{"dungeon: next-dungeon name"};
  Tally win{"dungeon: cleared-dungeon name"};
  for (int i = 0; i < dungeon::kPuzzleCount; ++i) {
    const char* name = dungeon::kPuzzles[i].name;
    {
      Paint paint("toyboxFaces");
      toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
      toybox::Screen screen(frame);
      dungeonui::MenuModel model;
      model.dungeonName = name;
      model.selectedIndex = i;
      model.total = dungeon::kPuzzleCount;
      dungeonui::PickerLayout layout;
      dungeonui::buildMenu(screen, model, layout);
      expectWhole(menu, paint.target, name);
    }
    {
      Paint paint("toyboxFaces");
      toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
      toybox::Screen screen(frame);
      dungeonui::WinModel model;
      model.dungeonName = name;
      model.cleared = &dungeon::kPuzzles[i];
      model.solvedCount = i + 1;
      model.total = dungeon::kPuzzleCount;
      dungeonui::buildWin(screen, model);
      expectWhole(win, paint.target, name);
    }
  }
  report(menu);
  report(win);
}

void foreheadCategories() {
  Tally tally{"forehead: category titles"};
  for (int i = 0; i < forehead::kCategoryCount; ++i) {
    Paint paint("bigNumberFaces");
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    toybox::Screen screen(frame);
    foreheadui::ResultModel model;
    model.category = i;
    model.score = 7;
    foreheadui::buildResult(screen, model);
    expectWhole(tally, paint.target, forehead::kCategories[i].title);
  }
  report(tally);
}

void linkGames() {
  Tally tally{"link: game titles"};
  for (int i = 0; i < fitted::kLinkGameTitleCount; ++i) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    toybox::Screen screen(frame);
    linkui::LinkModel model;
    model.gameTitle = fitted::kLinkGameTitles[i];
    model.headline = "LOOKING FOR A PLAYER";
    model.yourName = "YOU";
    model.yourFaceName = "BRAVE SILVER FOX";
    linkui::buildLink(screen, model);
    expectWhole(tally, paint.target, fitted::kLinkGameTitles[i]);
  }
  report(tally);
}

void connectionsDates() {
  Tally tally{"connections: header dates"};
  // Every date the header can format, not the dates one pack happens to hold.
  // The archive grows by one a day, so a suite pinned to today's newest is a
  // suite that stops covering the format the moment somebody imports more.
  for (int year = 2015; year <= 2035; ++year) {
    for (int month = 1; month <= 12; ++month) {
      static const int days[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
      for (int day = 1; day <= days[month - 1]; ++day) {
        const uint32_t date = static_cast<uint32_t>(year * 10000 + month * 100 + day);
        char text[16];
        connectionsui::formatDate(date, text, sizeof(text));
        Paint paint("serifBoardFaces");
        toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
        toybox::Screen screen(frame);
        connectionsui::BoardModel model;
        model.date = date;
        connectionsui::buildBoardChrome(screen, model);
        expectWhole(tally, paint.target, text);
      }
    }
  }
  report(tally);
}

void toyBattleMaps() {
  Tally tally{"toy battle: map names"};
  for (int i = 0; i < toybattle::kTerrainCount; ++i) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    toybox::Screen screen(frame);
    tbui::BriefModel model;
    model.board = &toybattle::terrainAt(i);
    model.specialBases = true;
    tbui::buildBrief(screen, model);
    expectWhole(tally, paint.target, toybattle::terrainAt(i).name);
  }
  report(tally);
}

void toyBattleHowTo() {
  Tally tally{"toy battle: how-to page titles"};
  ok(fitted::kToyBattleHowToTitleCount == tbui::howToPages(),
     "corpus.py found a different number of how-to pages than the app has");
  for (int page = 0; page < tbui::howToPages(); ++page) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    toybox::Screen screen(frame);
    tbui::HowToModel model;
    model.page = page;
    tbui::buildHowTo(screen, model);
    expectWhole(tally, paint.target, fitted::kToyBattleHowToTitles[page]);
  }
  report(tally);
}

// The Hacker News reader's band, which is the one title in this fork that is
// always somebody else's sentence.
//
// It is here because this change touched it: the band carries a SAVE control
// whose width was in neither term of the room the headline was fitted to, so a
// long headline was shortened by the app to a room about ninety pixels too
// wide and then shortened AGAIN by the component. Two ellipses, one headline.
// Walking it with the control both present and absent is what holds that shut.
//
// The headlines are folded with utf8FoldTypography first, exactly as
// HackerNewsActivity does on the way in -- unfolded, a curly quote would draw
// as a hole and this suite would be measuring a different defect.
void hackerNewsReader() {
  Tally tally{"hacker news: story headlines"};
  ok(fitted::kHnHeadlineCount > 0, "no captured Hacker News front page to walk");
  for (int i = 0; i < fitted::kHnHeadlineCount; ++i) {
    const std::string headline = utf8FoldTypography(fitted::kHnHeadlines[i]);
    for (int variant = 0; variant < 3; ++variant) {
      Paint paint("readerFaces");
      toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
      toybox::Screen screen(frame);
      toybox::WrappedText wrap;
      hnui::ReaderBody body;
      body.text = "One line of article, which this screen is not measuring.";
      body.style = screen.theme().bodyText;
      body.wrap = &wrap;
      hnui::ReaderModel model;
      model.title = headline.c_str();
      model.pageLabel = "3 / 12";
      // No control, the outlined SAVE, and the filled SAVED: three different
      // widths taken out of the same band, and the widest is the one the old
      // arithmetic left out entirely.
      model.canSave = variant > 0;
      model.saved = variant == 2;
      hnui::buildReader(screen, model, body);
      expectWhole(tally, paint.target, headline);
    }
  }
  // ZERO avoidable, now that the reader binds readerFaces (card #268). The band
  // steps its headline down through real reading cuts -- bold 16 -> 14 -> 11 --
  // instead of falling straight to an ellipsis with nothing worth shrinking to.
  // The ten that used to be a KEPT exception here fit only toybox_10, the Jersey
  // tile cut readingFaces bound in the SMALL slot, which has no business setting
  // prose; readerFaces puts reading_serif_11 there instead, so four of the ten
  // now fit whole and the rest join the residual, showing more of themselves
  // before the ellipsis than reading_serif_14 did.
  //
  // The residual stays high because the band is ONE line and a Hacker News
  // headline is a whole sentence: no single-line cut this fork owns holds "New
  // Mexico court orders Meta to pay $567m over harms to children's mental
  // health". That is the same class as the xkcd bar's residual below -- a second
  // line, not a smaller cut, is what would take it -- and like it, it is
  // reported, not gated. Gating avoidable at 0 is the fix this card asked for.
  report(tally);
}

void xkcdReaderBar() {
  Tally tally{"xkcd: comic titles in the bar"};
  std::printf("xkcd pack: %s\n", fitted::kXkcdPackLabel);
  // The ONE corpus that is not in this repository: the comics live in a pack on
  // the card. With no pack this walked zero titles and reported `0 walked 0
  // avoidable`, which is indistinguishable from a clean run -- and it is the
  // corpus that catches 588 avoidable cuts, on the runner nobody can skip.
  //
  // So it says SKIP, on its own line, at the start of the line, because that is
  // what scripts_local/check.sh greps for and prints beside an otherwise-green
  // suite. A check that did not run must not scroll past looking like one that
  // passed.
  if (fitted::kXkcdTitleCount == 0) {
    std::printf(
        "SKIP fittedtitle: no xkcd pack reachable, so NOT ONE comic title was walked.\n"
        "     That is the corpus that found 588 avoidable cuts; everything else here still ran.\n"
        "     Put a pack on the card or point XKCD_PACK at one.\n");
    return;
  }
  for (int i = 0; i < fitted::kXkcdTitleCount; ++i) {
    Paint paint("readingChromeFaces");
    // The panel NEVER rotates for this reader: a wide comic is stored already
    // turned on its side and the player turns the device, so the bar keeps the
    // 480px width every other screen has (XkcdActivity::onEnter). Handing this
    // an 800px landscape context -- the obvious guess -- makes the bar 320px
    // wider than it is and hides all but eight of the titles it cuts.
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    // The shared palette, which is what XkcdActivity::render takes: its old
    // headerHeight override was assigning the default over itself.
    toybox::Screen screen(frame);
    xkcdui::ReaderModel model;
    model.num = static_cast<uint32_t>(i + 1);
    model.title = fitted::kXkcdTitles[i];
    model.imageW = 600;
    model.imageH = 900;
    model.viewW = 600;
    model.viewH = 480;
    model.hasOverview = true;
    model.hasAlt = true;
    xkcdui::buildReaderBar(screen, model);
    char line[128];
    std::snprintf(line, sizeof(line), "#%u  %s", static_cast<unsigned>(model.num), fitted::kXkcdTitles[i]);
    expectWhole(tally, paint.target, line);
  }
  report(tally);
}

}  // namespace

int main() {
  std::printf("--- every title, every real string ---\n");
  theLadderItself();
  themeKeepsTheFittedCut();
  dungeonGuide();
  heartsRules();
  heartsStatusLines();
  heartsScoreAndMenu();
  dungeonNames();
  foreheadCategories();
  linkGames();
  connectionsDates();
  toyBattleMaps();
  toyBattleHowTo();
  hackerNewsReader();
  xkcdReaderBar();
  std::printf("\n%d real strings walked, %d of them shown short because no bound cut could take them\n", walked,
              residual);
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
