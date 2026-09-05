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
#include "LinkScreens.h"
#include "ToyBattleCore.h"
#include "ToyBattleMenus.h"
#include "ToyBattleScreens.h"
#include "ToyboxScreen.h"
#include "ToyboxText.h"
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

void report(const Tally& tally) {
  walked += tally.walked;
  residual += tally.residual;
  const int bad = tally.missing + tally.avoidable;
  std::printf("%-34s %5d walked  %4d avoidable  %4d residual  %3d never drawn\n", tally.what, tally.walked,
              tally.avoidable, tally.residual, tally.missing);
  if (tally.avoidable != 0 || tally.residual != 0 || tally.missing != 0) {
    std::printf("      longest short of it: %s\n                      -> %s\n", tally.worst.c_str(),
                tally.worstDrawn.c_str());
  }
  ok(bad == 0, std::string(tally.what) + ": " + std::to_string(bad) + " of " + std::to_string(tally.walked) +
                   " strings were cut while a bound cut would have shown them whole");
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
void themeKeepsTheFittedCut() {
  fui::TextStyle title = toybox::themeTokens().titleText;
  title.align = toybox::themeTokens().headerTitleAlign;
  title.font = fui::FONT_SLOT_SMALL;
  ok(!fui::textStyleUnset(title),
     "a title fitted down to the small slot reads as unset, so Screen::header would put the display cut back");
}

void dungeonGuide() {
  Tally tally{"dungeon: guide page titles"};
  for (int page = 0; page < dungeonui::guidePageCount(); ++page) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    toybox::Screen screen(frame);
    dungeonui::GuideModel model;
    model.page = page;
    model.pageCount = dungeonui::guidePageCount();
    dungeonui::buildGuide(screen, model);
    // The title is data the screen owns, so it is read back off the panel
    // rather than named here: whatever the header drew IS the corpus entry.
    const std::vector<fitted::TextRun>& runs = paint.target.texts;
    ok(!runs.empty(), "dungeon guide drew nothing");
    if (runs.empty()) continue;
    // The band's title is the first white run in the header band.
    for (const fitted::TextRun& run : runs) {
      if (run.color != fui::Color::White || run.rect.y >= toybox::kHeaderHeight) continue;
      expectWhole(tally, paint.target, run.asked);
      break;
    }
  }
  report(tally);
}

// Every dungeon's name, on both screens that show it. The names are the app's
// longest data -- thirty-three characters at the top -- and the two screens
// treat them differently on purpose: the menu's foot is one line beside an icon
// and a TUTORIAL button, and the cleared screen is two centred lines with the
// map underneath.
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
  for (int page = 0; page < tbui::howToPages(); ++page) {
    Paint paint("toyboxFaces");
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    toybox::Screen screen(frame);
    tbui::HowToModel model;
    model.page = page;
    tbui::buildHowTo(screen, model);
    for (const fitted::TextRun& run : paint.target.texts) {
      if (run.color != fui::Color::White || run.rect.y >= toybox::kHeaderHeight) continue;
      expectWhole(tally, paint.target, run.asked);
      break;
    }
  }
  report(tally);
}

void xkcdReaderBar() {
  Tally tally{"xkcd: comic titles in the bar"};
  std::printf("xkcd pack: %s\n", fitted::kXkcdPackLabel);
  for (int i = 0; i < fitted::kXkcdTitleCount; ++i) {
    Paint paint("readingChromeFaces");
    // The panel NEVER rotates for this reader: a wide comic is stored already
    // turned on its side and the player turns the device, so the bar keeps the
    // 480px width every other screen has (XkcdActivity::onEnter). Handing this
    // an 800px landscape context -- the obvious guess -- makes the bar 320px
    // wider than it is and hides all but eight of the titles it cuts.
    toybox::Frame frame(paint.target, panel(), fui::InputSnapshot{}, paint.interactions);
    fui::ThemeTokens tokens = toybox::themeTokens();
    tokens.headerHeight = xkcdui::kHeaderBand;
    toybox::Screen screen(frame, tokens);
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
  themeKeepsTheFittedCut();
  dungeonGuide();
  dungeonNames();
  foreheadCategories();
  linkGames();
  connectionsDates();
  toyBattleMaps();
  toyBattleHowTo();
  xkcdReaderBar();
  std::printf("\n%d real strings walked, %d of them shown short because no bound cut could take them\n", walked,
              residual);
  std::printf("%d checks, %d failed\n", checks, failed);
  return failed == 0 ? 0 : 1;
}
