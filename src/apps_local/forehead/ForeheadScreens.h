#pragma once

// FOREHEAD on screen. Freestanding builders over plain models.
//
// Two orientations, and the split is physical rather than aesthetic. The front
// door, the picker and the rules are PORTRAIT, because you are holding the
// device the way you held the shelf you just came from. The ready card, the
// round and the results are LANDSCAPE, because the word has to be read from
// across a room and 800px of width is nearly twice 480.
//
// The turn between them is not signposted with an icon. The ready card is drawn
// in landscape while the device is still being held in portrait, so it arrives
// sideways -- and a sideways screenful of type is the only rotation instruction
// anybody has ever needed.

#include "../ui/ToyboxScreen.h"
#include "ForeheadCore.h"

namespace foreheadui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  // The headline opens the READY card; only the READY card starts the clock.
  // Two actions rather than one that means different things in two views: a
  // proxy is how a feature breaks a neighbour that was written before it.
  ActionReady = 1,
  // The three doors are one list, so they are one action told apart by
  // MenuRow -- ListProps carries the action and ListItem carries the value.
  ActionMenuRow = 2,
  ActionCategoryRow = 5,
  ActionPage = 6,
  ActionGot = 7,
  ActionMissed = 8,
  ActionAgain = 9,
  ActionDone = 10,
  ActionHowToNext = 11,
  ActionResultsPage = 12,
  ActionStart = 13,
  ActionSettingsRow = 14,
};

// The settings screen's rows. RESET is last and separated, because it is the
// only irreversible thing in the app.
enum class SettingRow : int { Length = 0, Reset, Count };

// The front door. Every band here is the pattern in docs/design-language.md:
// headline, state, rule, record, ornament, then the lesser doors at the bottom
// where a thumb rests.
enum class MenuRow : int { Category = 0, Settings, HowTo, Count };

struct MenuModel {
  int category = 0;
  int roundSeconds = forehead::kDefaultRoundSeconds;
  // Read, never owned. The activity holds the record and the save file.
  const forehead::Record* record = nullptr;
  int selected = -1;
};

// The picker. Seventeen categories over two pages, with the same pip row the
// shelf folder uses -- one paging idiom in the fork, not two.
struct PickerModel {
  int page = 0;
  int current = 0;
  const forehead::Record* record = nullptr;
};

struct ReadyModel {
  int category = 0;
  int roundSeconds = forehead::kDefaultRoundSeconds;
};

// The round. Nothing here is a pointer into the Round object: the activity
// hands over exactly what the card shows, so a screen test can build a state
// that no sequence of taps would reach.
struct PlayModel {
  const char* word = "";
  int score = 0;
  int secondsLeft = 0;
  int lengthSeconds = forehead::kDefaultRoundSeconds;
  int category = 0;
};

struct ResultModel {
  int category = 0;
  int score = 0;
  int page = 0;
  // The round itself, read-only. Round is freestanding, so handing it over
  // costs the screens nothing and saves copying a hundred and twenty-eight
  // cards into a model that would only be a worse copy of it.
  const forehead::Round* round = nullptr;
};

struct HowToModel {
  int page = 0;
};

struct SettingsModel {
  int roundSeconds = forehead::kDefaultRoundSeconds;
  // A reset asks once, in place, by relabelling its own row. A dialog would be
  // a second screen for a question that fits on the row you already tapped --
  // and the design language's rule is that a destructive setting confirms with
  // a label you were going to read anyway.
  bool confirmingReset = false;
  // Whether there is anything to clear -- scores OR dealt cards. Handed in
  // rather than derived from a Record here, because a round abandoned halfway
  // burns words without ever touching the record, and a screen that asked the
  // record would offer nothing to clear while a category quietly emptied.
  bool anythingToClear = false;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildPicker(toybox::Screen& screen, const PickerModel& model);
void buildReady(toybox::Screen& screen, const ReadyModel& model);
void buildPlay(toybox::Screen& screen, const PlayModel& model);
void buildResult(toybox::Screen& screen, const ResultModel& model);
void buildHowTo(toybox::Screen& screen, const HowToModel& model);
void buildSettings(toybox::Screen& screen, const SettingsModel& model);

// Where a page key lands. Wraps in both directions, so the last page's forward
// key returns to the first and the first page's back key reaches the last.
//
// Freestanding and here rather than inline in the activity because the activity
// is the one layer no host test can construct: left there, the arithmetic that
// decides whether paging dead-ends is exactly as testable as the device is.
int pageAfter(int page, int step, int pages);

int howToPages();
int pickerPages();
int pickerRowsPerPage();
int resultPages(int cards);
int resultsPerPage();

// The card's type ladder, exposed because it is the one piece of layout worth
// asserting on directly: it is what stands between a 22-character entry and a
// word running off the panel.
//
// Picks the largest of the three cuts bound to the three slots (title = 64,
// small = 44, body = 30) at which `text` fits `box` in at most kCardMaxLines
// lines, breaking on spaces only. Never hyphenates: a word split in half is
// unreadable in a way that a smaller word never is.
inline constexpr int kCardMaxLines = 3;

struct CardLayout {
  fui::FontId font = toybox::kDisplayFont;
  int lines = 1;
  // Byte offsets into the source string, one per line. The builder draws
  // [start, end) for each, so nothing is copied and no buffer is needed.
  int16_t start[kCardMaxLines] = {};
  int16_t end[kCardMaxLines] = {};
  int16_t width[kCardMaxLines] = {};
  int16_t lineHeight = 0;
};

CardLayout layOutCard(toybox::Screen& screen, const fui::Rect& box, const char* text);

}  // namespace foreheadui
