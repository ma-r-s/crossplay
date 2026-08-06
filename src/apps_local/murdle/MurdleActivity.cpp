#include "MurdleActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

// GUI, for drawButtonHints. Included directly rather than inherited from
// Toybox.h: a header pulled in only for a macro reads as unused to clangd.
#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "MurdleCast.h"
#include "MurdleText.h"

namespace {

namespace fui = freeink::ui;
namespace ui = murdleui;

constexpr char kSavePath[] = "/.crosspoint/murdle.sav";

// Bumped whenever the layout below changes, or whenever the generator changes
// in a way that would make an old seed build a different case. The fingerprint
// catches the second of those on its own, but the version is what makes the
// first one cheap.
constexpr uint8_t kSaveVersion = 1;

// The case is not stored, it is regenerated from its seed. The fingerprint is
// what makes that safe: a generator that has changed since the save was written
// builds a different case from the same seed, the fingerprints disagree, and
// the save is dropped rather than restored on top of somebody's marks.
struct SaveState {
  uint8_t tier;      // the setting, which outlives any case
  uint8_t caseTier;  // the open case's own shape
  uint8_t hasCase;
  uint8_t solved;
  uint32_t seed;
  uint32_t fingerprint;
  uint32_t struck;
  uint32_t record;
  uint16_t caseNumber;
  uint16_t solvedCount;
  uint16_t wrongCount;
  uint8_t wrongAccusations;
  uint8_t face;
  uint8_t page;
  // Two bits a cell, six blocks of four by four.
  uint8_t marks[murdle::kMaxBlocks * murdle::kMaxItems * murdle::kMaxItems / 4];
} __attribute__((packed));

void packMarks(const murdle::Grid& grid, const murdle::Shape shape, uint8_t* out, const size_t cap) {
  std::memset(out, 0, cap);
  int bit = 0;
  for (int a = 0; a < shape.cats; ++a) {
    for (int b = a + 1; b < shape.cats; ++b) {
      for (int ia = 0; ia < shape.items; ++ia) {
        for (int ib = 0; ib < shape.items; ++ib, ++bit) {
          const size_t byte = static_cast<size_t>(bit) / 4;
          if (byte >= cap) return;
          const uint8_t value = static_cast<uint8_t>(grid.get(a, ia, b, ib));
          out[byte] = static_cast<uint8_t>(out[byte] | (value << ((bit % 4) * 2)));
        }
      }
    }
  }
}

void unpackMarks(const uint8_t* in, const size_t cap, const murdle::Shape shape, murdle::Grid& grid) {
  grid.reset(shape);
  int bit = 0;
  for (int a = 0; a < shape.cats; ++a) {
    for (int b = a + 1; b < shape.cats; ++b) {
      for (int ia = 0; ia < shape.items; ++ia) {
        for (int ib = 0; ib < shape.items; ++ib, ++bit) {
          const size_t byte = static_cast<size_t>(bit) / 4;
          if (byte >= cap) return;
          const uint8_t value = static_cast<uint8_t>((in[byte] >> ((bit % 4) * 2)) & 3u);
          if (value != 0) grid.set(a, ia, b, ib, static_cast<murdle::Mark>(value));
        }
      }
    }
  }
}

}  // namespace

std::unique_ptr<Activity> MurdleActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<MurdleActivity>(renderer, mappedInput);
}

void MurdleActivity::onEnter() {
  toybox::ensureFonts(renderer);
  if (!loadState()) {
    hasCase = false;
    tier = murdle::Tier::Elementary;
  }
  view = View::Menu;
  dirty = false;
  requestUpdate();
}

void MurdleActivity::onExit() {
  flushSave();
  scratch.reset();
}

void MurdleActivity::goToMenu() {
  view = View::Menu;
  flushSave();
  requestUpdate();
}

// ---------------------------------------------------------------------------
// The new-case funnel

void MurdleActivity::requestNewCase() {
  if (hasCase && !solved) {
    view = View::ConfirmNew;
    requestUpdate();
    return;
  }
  generatePending = true;
  view = View::Case;
  face = ui::Face::Clues;
  page = 0;
  requestUpdate();
}

void MurdleActivity::generateCase() {
  if (!scratch) {
    scratch = makeUniqueNoThrow<murdle::Scratch>();
    if (!scratch) {
      LOG_ERR("MRDL", "OOM: generator scratch (%u bytes)", static_cast<unsigned>(sizeof(murdle::Scratch)));
      goToMenu();
      return;
    }
  }

  // millis() is the only entropy on this device that differs between two boots.
  // Mixed rather than used raw: the timer advances in even steps and the low
  // bit of a raw reading is not random.
  seed = static_cast<uint32_t>(millis()) * 2654435761u + 0x9E3779B9u;
  const murdle::Shape shape = murdle::shapeOf(tier);
  uint8_t cast[murdle::kMaxCats][murdle::kMaxItems];
  if (!murdle::drawCast(seed, shape, cast)) {
    LOG_ERR("MRDL", "Cast draw ran out of letters for tier %d", static_cast<int>(tier));
    goToMenu();
    return;
  }

  if (!murdle::generate(tier, seed, cast, murdle::attrMasksFor(cast, shape), *scratch, puzzle)) {
    LOG_ERR("MRDL", "No fair case for tier %d after the attempt budget", static_cast<int>(tier));
    goToMenu();
    return;
  }
  scratch.reset();

  marks.reset(puzzle.shape);
  hasCase = true;
  solved = false;
  struck = 0;
  wrongAccusations = 0;
  page = 0;
  face = ui::Face::Clues;
  ++caseNumber;
  for (int c = 0; c < murdle::kMaxCats; ++c) picks[c] = ui::AccuseModel::kNothingPicked;
  dirty = true;
  flashOnNextPaint = true;
  LOG_INF("MRDL", "Case %d: tier %d, %d clues, %d rounds", caseNumber, static_cast<int>(tier), puzzle.clueCount,
          puzzle.rounds);
}

void MurdleActivity::openCase() {
  if (!hasCase) {
    requestNewCase();
    return;
  }
  view = View::Case;
  requestUpdate();
}

// ---------------------------------------------------------------------------

void MurdleActivity::loop() {
  mappedInput.update();

  if (generatePending) {
    // One pass later than the tap, so the frame saying so is already on the
    // panel before the work starts.
    generatePending = false;
    generateCase();
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    switch (view) {
      case View::Menu:
        flushSave();
        shelf::leave(renderer, mappedInput);
        return;
      case View::Case:
        goToMenu();
        return;
      case View::Accuse:
        view = View::Case;
        requestUpdate();
        return;
      case View::Verdict:
      case View::Settings:
      case View::HowTo:
      case View::ConfirmNew:
        goToMenu();
        return;
    }
  }

  fui::InputSnapshot input{};
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent hit = interactions.route(input);
  if (hit.action == ui::ActionGrid) {
    handleGridTap(tapX, tapY);
    return;
  }
  // A tap that hit nothing, on a page of clues, is a tap on a clue. The list is
  // longer than the interaction buffer, so it hands back its geometry instead
  // of registering a rect a row; see MurdleScreens.h.
  if (hit.action == fui::NO_ACTION && view == View::Case && face == ui::Face::Clues && page > 0) {
    handleClueTap(tapY);
    return;
  }
  routeAction(static_cast<int>(hit.action), static_cast<int>(hit.value));
}

// A tap inside the grid, resolved against the layout that drew it. Never a
// second piece of arithmetic: the rule that has caught the most bugs in this
// fork is that a tappable region must be derived from the same values that
// placed the pixels.
void MurdleActivity::handleGridTap(const int x, const int y) {
  if (!hasCase || solved) return;
  ui::GridCell cell;
  if (!ui::cellAt(gridLayout, x, y, cell)) return;
  const int catA = cell.catA;
  const int catB = cell.catB;
  const int itemA = cell.itemA;
  const int itemB = cell.itemB;

  switch (marks.get(catA, itemA, catB, itemB)) {
    case murdle::Mark::Unknown:
      marks.set(catA, itemA, catB, itemB, murdle::Mark::No);
      break;
    case murdle::Mark::No:
      // Clearing the cell first, because setYes refuses to write over a No and
      // a player cycling through the three states means to overrule themselves.
      marks.clear(catA, itemA, catB, itemB);
      marks.setYes(catA, itemA, catB, itemB);
      break;
    case murdle::Mark::Yes:
      marks.clear(catA, itemA, catB, itemB);
      break;
  }
  dirty = true;
  requestUpdate();
}

void MurdleActivity::handleClueTap(const int y) {
  const ui::ClueLayout layout = ui::lastClueLayout();
  for (int i = 0; i < layout.count; ++i) {
    if (y < layout.top[i] || y >= layout.top[i + 1]) continue;
    struck ^= (1u << layout.index[i]);
    dirty = true;
    requestUpdate();
    return;
  }
}

void MurdleActivity::submitAccusation() {
  bool right = true;
  for (int c = 0; c < puzzle.shape.cats; ++c) {
    if (picks[c] != puzzle.assign[c][puzzle.murderRow]) right = false;
  }
  if (right) {
    solved = true;
    ++solvedCount;
    recordVerdict(wrongAccusations == 0 ? 1 : 2);
  } else {
    ++wrongAccusations;
    ++wrongCount;
  }
  view = View::Verdict;
  dirty = true;
  flashOnNextPaint = true;
  requestUpdate();
}

void MurdleActivity::recordVerdict(const int outcome) { record = (record << 2) | static_cast<uint32_t>(outcome & 3); }

void MurdleActivity::routeAction(const int action, const int value) {
  switch (action) {
    case ui::ActionPlay:
      openCase();
      break;
    case ui::ActionNewCase:
      requestNewCase();
      break;
    case ui::ActionConfirmNew:
      // The only path that skips the question, and it is the answer to it.
      solved = true;
      requestNewCase();
      break;
    case ui::ActionCancel:
      view = hasCase ? View::Case : View::Menu;
      requestUpdate();
      break;
    case ui::ActionSettings:
      view = View::Settings;
      requestUpdate();
      break;
    case ui::ActionHowTo:
      if (view == View::HowTo) {
        if (howToPage + 1 < ui::howToPages()) {
          ++howToPage;
          requestUpdate();
        } else {
          goToMenu();
        }
      } else {
        view = View::HowTo;
        howToPage = 0;
        requestUpdate();
      }
      break;
    case ui::ActionFace:
      face = value == 1 ? ui::Face::Grid : ui::Face::Clues;
      page = 0;
      dirty = true;
      requestUpdate();
      break;
    case ui::ActionPage: {
      // The top is clamped by the builder, which is the only thing that knows
      // how many pages the measured text came to. Here we only refuse to go
      // below zero and let the render report the real ceiling back.
      int next = page + value;
      if (next < 0) next = 0;
      if (next == page) break;
      page = next;
      dirty = true;
      requestUpdate();
      break;
    }
    case ui::ActionAccuse:
      for (int c = 0; c < murdle::kMaxCats; ++c) picks[c] = ui::AccuseModel::kNothingPicked;
      view = View::Accuse;
      requestUpdate();
      break;
    case ui::ActionPick: {
      const int cat = value / 8;
      const int item = value % 8;
      if (cat < 0 || cat >= puzzle.shape.cats) break;
      picks[cat] = static_cast<uint8_t>(item);
      requestUpdate();
      break;
    }
    case ui::ActionConfirm:
      submitAccusation();
      break;
    case ui::ActionKeepLooking:
      view = View::Case;
      requestUpdate();
      break;
    case ui::ActionDone:
      goToMenu();
      break;
    case ui::ActionTier: {
      // An absolute tier, not a step. The settings screen shows all four, so
      // there is nothing to walk.
      if (value < 0 || value >= murdle::kTierCount) break;
      if (static_cast<murdle::Tier>(value) == tier) break;
      tier = static_cast<murdle::Tier>(value);
      dirty = true;
      requestUpdate();
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------

void MurdleActivity::render(RenderLock&&) {
  renderer.clearScreen();
  // Jersey throughout, like chess and insider. A clue is a sentence or two, not
  // a screenful of prose, so this game does not need the reading cut -- and the
  // first attempt at it put the *header* and the *buttons* in a serif, which is
  // the device's voice wearing the app's.
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  gridLayout = ui::GridLayout{};

  switch (view) {
    case View::Case: {
      if (generatePending || !hasCase) {
        // The frame that has to be on the panel before generation starts.
        const fui::Rect body = screen.body();
        fui::TextStyle style;
        style.font = toybox::kDisplayFont;
        style.align = fui::TextAlign::Center;
        style.color = fui::Color::Black;
        target.text(fui::makeRect(body.x, static_cast<int16_t>(body.y + body.height / 2 - 30), body.width, 60),
                    "A NEW CASE", style);
        break;
      }
      ui::CaseModel model;
      model.puzzle = &puzzle;
      model.marks = &marks;
      model.face = face;
      model.page = page;
      model.struck = struck;
      model.caseNumber = caseNumber;
      model.solved = solved;
      const ui::CaseReport report = ui::buildCase(screen, model);
      gridLayout = report.grid;
      // The builder clamps the page against a count only it can compute, so
      // take its word back rather than keeping a second opinion around.
      page = report.page;
      break;
    }
    case View::Accuse: {
      ui::AccuseModel model;
      model.puzzle = &puzzle;
      for (int c = 0; c < murdle::kMaxCats; ++c) model.picks[c] = picks[c];
      ui::buildAccuse(screen, model);
      break;
    }
    case View::Verdict: {
      ui::VerdictModel model;
      model.puzzle = &puzzle;
      model.right = solved;
      model.wrongAccusations = wrongAccusations;
      for (int c = 0; c < murdle::kMaxCats; ++c) model.picks[c] = picks[c];
      ui::buildVerdict(screen, model);
      break;
    }
    case View::Settings: {
      ui::SettingsModel model;
      model.tier = tier;
      model.caseOpen = hasCase && !solved;
      ui::buildSettings(screen, model);
      break;
    }
    case View::HowTo: {
      ui::HowToModel model;
      model.page = howToPage;
      ui::buildHowTo(screen, model);
      break;
    }
    case View::ConfirmNew:
      ui::buildConfirmNew(screen);
      break;
    case View::Menu:
    default: {
      ui::MenuModel model;
      model.hasCase = hasCase;
      model.caseSolved = solved;
      model.tier = tier;
      model.caseNumber = caseNumber;
      model.solvedCount = solvedCount;
      model.wrongCount = wrongCount;
      model.record = record;
      if (hasCase) {
        model.puzzle = &puzzle;
        model.marks = &marks;
        for (int i = 0; i < puzzle.clueCount; ++i) {
          if (struck & (1u << i)) ++model.cluesTicked;
        }
      }
      ui::buildMenu(screen, model);
      break;
    }
  }

  interactionsReady = true;
  toybox::reportOverflow(interactions, "Murdle");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // A new case and a verdict are page turns; a mark is not. Spend the full
  // refresh on meaning rather than on frames.
  renderer.displayBuffer(flashOnNextPaint ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint = false;
}

// ---------------------------------------------------------------------------

void MurdleActivity::flushSave() {
  if (!dirty) return;
  saveState();
  dirty = false;
}

void MurdleActivity::saveState() {
  SaveState state{};
  state.tier = static_cast<uint8_t>(tier);
  state.caseTier = static_cast<uint8_t>(puzzle.tier);
  state.hasCase = hasCase ? 1 : 0;
  state.solved = solved ? 1 : 0;
  state.seed = seed;
  state.fingerprint = hasCase ? murdle::fingerprint(puzzle) : 0;
  state.struck = struck;
  state.record = record;
  state.caseNumber = static_cast<uint16_t>(caseNumber);
  state.solvedCount = static_cast<uint16_t>(solvedCount);
  state.wrongCount = static_cast<uint16_t>(wrongCount);
  state.wrongAccusations = static_cast<uint8_t>(wrongAccusations);
  state.face = static_cast<uint8_t>(face);
  state.page = static_cast<uint8_t>(page);
  if (hasCase) packMarks(marks, puzzle.shape, state.marks, sizeof(state.marks));

  HalFile file;
  if (!Storage.openFileForWrite("MRDL", kSavePath, file)) return;
  const uint8_t version = kSaveVersion;
  file.write(&version, 1);
  file.write(reinterpret_cast<const uint8_t*>(&state), sizeof(state));
  file.flush();
}

bool MurdleActivity::loadState() {
  if (!Storage.exists(kSavePath)) return false;
  HalFile file;
  if (!Storage.openFileForRead("MRDL", kSavePath, file)) return false;
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != kSaveVersion) return false;
  SaveState state{};
  if (file.read(reinterpret_cast<uint8_t*>(&state), sizeof(state)) != sizeof(state)) return false;

  tier = state.tier < murdle::kTierCount ? static_cast<murdle::Tier>(state.tier) : murdle::Tier::Elementary;
  record = state.record;
  caseNumber = state.caseNumber;
  solvedCount = state.solvedCount;
  wrongCount = state.wrongCount;
  hasCase = false;
  if (state.hasCase == 0) return true;

  if (!scratch) scratch = makeUniqueNoThrow<murdle::Scratch>();
  if (!scratch) {
    LOG_ERR("MRDL", "OOM restoring a case; starting without one");
    return true;
  }
  const murdle::Tier caseTier = state.caseTier < murdle::kTierCount ? static_cast<murdle::Tier>(state.caseTier) : tier;
  const murdle::Shape shape = murdle::shapeOf(caseTier);
  uint8_t cast[murdle::kMaxCats][murdle::kMaxItems];
  const bool drawn = murdle::drawCast(state.seed, shape, cast);
  const bool built =
      drawn && murdle::generate(caseTier, state.seed, cast, murdle::attrMasksFor(cast, shape), *scratch, puzzle);
  scratch.reset();

  // The fingerprint is the whole point of storing a seed instead of a puzzle.
  // A generator that has changed since this save was written builds a different
  // case from the same seed, and restoring somebody's marks onto it would be
  // silently wrong in a way nothing else could catch.
  if (!built || murdle::fingerprint(puzzle) != state.fingerprint) {
    LOG_INF("MRDL", "Saved case no longer rebuilds; dropping it");
    return true;
  }

  unpackMarks(state.marks, sizeof(state.marks), puzzle.shape, marks);
  hasCase = true;
  solved = state.solved != 0;
  seed = state.seed;
  struck = state.struck;
  wrongAccusations = state.wrongAccusations;
  face = state.face == 1 ? ui::Face::Grid : ui::Face::Clues;
  page = state.page;
  return true;
}
