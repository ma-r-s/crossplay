// Reveal sweep: does the screen that REPLACES another put a different action
// under the same pixels?
//
// The failure this measures is the one Wavelength shipped: a control produces a
// new screen, the new screen's control sits in the same rectangle, and the
// finger already resting there (or a second tap arriving during the panel's
// own 0.3-2s refresh) dismisses the thing it just asked for. The pixels are
// correct in both frames; nobody ever reads the second one.
//
// Method: build screen A and screen B against a fake draw target, route a tap
// at every point on a 4px lattice through each screen's own interaction table,
// and report the points that are live on BOTH with DIFFERENT actions. No
// device, no renderer: same freestanding build the ui suite uses.
//
// The fake text metrics (10px/char) are not the device's. Every probe is
// therefore run twice, at two different metrics; a region whose verdict changes
// between the two is reported as METRIC-DEPENDENT and must not be quoted as a
// number.
//
// THIS IS A GATE, not a report. It was a report until 2026-09-05, and that is
// the whole reason it is worth saying twice: main() printed what it found and
// returned 0, there was no failure counter in the file, and it never emitted
// the "N checks, M failed" line check.sh counts -- so check.sh printed
// "revealsweep ok (0 sub-suite(s))" whatever the sweep discovered. A NEW
// same-pixel collision, of exactly the kind Wavelength shipped, would have
// scrolled past in a log nobody opens while the gate stayed green.
//
// What it fails on now, and why it is shaped this way. Every collision the
// sweep can see today is already known: they are written down here, one line
// each, next to what is known about them. The gate fails when reality stops
// matching that list -- a collision that is not on it (a regression, or a new
// screen pair), or a line on the list that no longer happens (a fix landed and
// the waiver outlived it). Both directions matter: a stale waiver is a hole
// held open for the next regression.
//
// It also fails when a probe stops measuring what it says it measures. Each
// probe names the control on the FROM screen that PRODUCES the TO screen --
// the one rect a finger is provably on at the moment the panel changes. Three
// of the ten named an action their own fixture never registered (minesweeper
// and sudoku built an unfinished board and asked for the verdict capsule;
// study named ActionSync where the SYNC door carries ActionStudy/2), so the
// producer check silently did not run on any of them and printed nothing at
// all. A probe whose producer is absent is now a failure, not a blank line.

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../src/apps_local/battleship/BattleshipScreens.h"
#include "../../src/apps_local/forehead/ForeheadScreens.h"
#include "../../src/apps_local/insider/InsiderScreens.h"
#include "../../src/apps_local/instapaper/InstapaperScreens.h"
#include "../../src/apps_local/link/LinkScreens.h"
#include "../../src/apps_local/minesweeper/MinesweeperScreens.h"
#include "../../src/apps_local/study/StudyScreens.h"
#include "../../src/apps_local/sudoku/SudokuScreens.h"
#include "../../src/apps_local/trivia/TriviaScreens.h"
#include "../../src/apps_local/ui/ToyboxScreen.h"
#include "../../src/apps_local/wavelength/WavelengthScreens.h"

namespace fui = freeink::ui;

namespace {

int gChecks = 0;
int gFailed = 0;

// Every assertion this file makes goes through here, so the count in the
// summary line is the count of things actually asked -- not a number typed
// beside them.
bool check(const bool ok, const char* what) {
  ++gChecks;
  if (!ok) {
    ++gFailed;
    std::printf("   FAIL: %s\n", what);
  }
  return ok;
}

int16_t gCharWidth = 10;
int16_t gLineHeight = 20;

class NullTarget final : public fui::DrawTarget {
 public:
  fui::Size measureText(const fui::FontId, const char* text, const fui::TextStyle) const override {
    return fui::Size{static_cast<int16_t>(text ? std::strlen(text) * gCharWidth : 0), gLineHeight};
  }
  int16_t lineHeight(const fui::FontId) const override { return gLineHeight; }
  void fill(fui::Rect, fui::Paint, uint8_t = 0, uint8_t = 0xFF) override {}
  void stroke(fui::Rect, fui::Paint, uint8_t, uint8_t = 0, uint8_t = 0xFF) override {}
  void line(fui::Point, fui::Point, uint8_t, fui::Paint) override {}
  void triangle(fui::Point, fui::Point, fui::Point, fui::Paint) override {}
  void text(fui::Rect, const char*, fui::TextStyle) override {}
  void bitmap(fui::Rect, fui::BitmapRef, fui::BitmapMode, fui::Paint = {},
              fui::Rotation = fui::Rotation::None) override {}
};

fui::DeviceContext device(const bool landscape) {
  fui::DeviceContext ctx;
  ctx.width = landscape ? 800 : 480;
  ctx.height = landscape ? 480 : 800;
  ctx.hasTouch = true;
  ctx.hasButtons = true;
  return ctx;
}

// One built screen and the table it registered.
struct Built {
  NullTarget target;
  toybox::Interactions interactions;

  fui::ActionEvent tap(const int x, const int y) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(x);
    input.touchY = static_cast<int16_t>(y);
    return interactions.route(input);
  }
};

// Build helper: hands the caller a toybox::Screen over the fake target.
template <typename Fn>
void build(Built& out, const bool landscape, Fn fn) {
  const fui::DeviceContext ctx = device(landscape);
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(out.target, ctx, noInput, out.interactions);
  toybox::Screen screen(frame, toybox::themeTokens());
  fn(screen);
}

struct Cell {
  int action = 0;
  int value = 0;
};

// Every lattice point's action on one screen.
std::vector<Cell> latticeOf(Built& built, const int w, const int h, const int step) {
  std::vector<Cell> out;
  for (int y = 0; y < h; y += step) {
    for (int x = 0; x < w; x += step) {
      const fui::ActionEvent e = built.tap(x, y);
      out.push_back(Cell{static_cast<int>(e.action), static_cast<int>(e.value)});
    }
  }
  return out;
}

struct Overlap {
  int fromAction = 0;
  int fromValue = 0;
  int toAction = 0;
  int toValue = 0;
  int points = 0;
  int minX = 1 << 20, minY = 1 << 20, maxX = -1, maxY = -1;
};

std::string key(const Overlap& o) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%d/%d->%d/%d", o.fromAction, o.fromValue, o.toAction, o.toValue);
  return buf;
}

// Points live on A and also live on B with a different action.
std::vector<Overlap> collide(std::vector<Cell>& a, std::vector<Cell>& b, const int w, const int h, const int step) {
  std::map<std::string, Overlap> acc;
  size_t i = 0;
  for (int y = 0; y < h; y += step) {
    for (int x = 0; x < w; x += step, ++i) {
      const Cell& ca = a[i];
      const Cell& cb = b[i];
      if (ca.action == 0 || cb.action == 0) continue;
      if (ca.action == cb.action && ca.value == cb.value) continue;
      Overlap probe{ca.action, ca.value, cb.action, cb.value, 0, 0, 0, 0, 0};
      const std::string k = key(probe);
      auto it = acc.find(k);
      if (it == acc.end()) {
        Overlap fresh;
        fresh.fromAction = ca.action;
        fresh.fromValue = ca.value;
        fresh.toAction = cb.action;
        fresh.toValue = cb.value;
        it = acc.emplace(k, fresh).first;
      }
      Overlap& o = it->second;
      ++o.points;
      if (x < o.minX) o.minX = x;
      if (y < o.minY) o.minY = y;
      if (x > o.maxX) o.maxX = x;
      if (y > o.maxY) o.maxY = y;
    }
  }
  std::vector<Overlap> out;
  for (const auto& entry : acc) out.push_back(entry.second);
  return out;
}

// How many lattice points a given action owns on one screen.
int areaOf(const std::vector<Cell>& cells, const int action, const int value) {
  int n = 0;
  for (const auto& c : cells) {
    if (c.action == action && c.value == value) ++n;
  }
  return n;
}

constexpr int kStep = 4;

// A producer whose action is unique on its screen does not need a value; a
// producer that is one row of a list does, because the row is what the finger
// is on and its siblings are elsewhere.
constexpr int kAnyValue = 1 << 30;

// The nine points inside the producer's own rect that the new screen is asked
// about: four corners, four edge midpoints, the centre.
constexpr int kProducerProbePoints = 9;

struct Probe {
  const char* name;
  bool landscape;
  void (*buildFrom)(Built&);
  void (*buildTo)(Built&);
  // The action on the FROM screen that produces the TO screen, or 0 if the
  // transition is not a touch at all. This is the one whose rect a finger is
  // provably on at the moment the new screen is drawn. It must exist on the
  // FROM screen the fixture builds: a producer that is not there means the
  // fixture is not in the state the probe's name claims, and the sharpest
  // check in the file quietly does not run.
  int producer = 0;
  // Which instance of it, when the action is a list row.
  int producerValue = kAnyValue;
  // How many of the nine points inside the producer's rect the NEW screen
  // answers at. Zero is the only good answer; anything else is a control the
  // finger already resting there can fire. The number is here rather than
  // assumed so that a collision getting WIDER is a failure and not a shrug.
  int producerLive = 0;
  // The collisions this transition is known to have, "fromAction/fromValue->
  // toAction/toValue", terminated by nullptr. Anything else the sweep finds
  // fails; anything here it no longer finds fails too.
  const char* const* known = nullptr;
  // One line saying why the list above is what it is. Printed with the
  // findings so a reader of the log is told, not left to infer.
  const char* note = "";
};

void dump(const char* label, Built& built) {
  std::printf("   %s registers %d interactions:\n", label, static_cast<int>(built.interactions.count()));
  const fui::Interaction* data = built.interactions.data();
  for (size_t i = 0; i < built.interactions.count(); ++i) {
    std::printf("      action %d/%d  rect x=%d y=%d w=%d h=%d  mask=0x%x\n", static_cast<int>(data[i].action),
                static_cast<int>(data[i].value), data[i].rect.x, data[i].rect.y, data[i].rect.width,
                data[i].rect.height, static_cast<unsigned>(data[i].inputMask));
  }
}

void report(const Probe& probe) {
  const int w = probe.landscape ? 800 : 480;
  const int h = probe.landscape ? 480 : 800;

  std::printf("\n== %s\n", probe.name);
  if (probe.note[0] != '\0') std::printf("   known: %s\n", probe.note);
  {
    Built from;
    Built to;
    probe.buildFrom(from);
    probe.buildTo(to);
    dump("FROM", from);
    dump("TO  ", to);
    if (probe.producer != 0) {
      int found = 0;
      int live = 0;
      int seen = 0;
      const fui::Interaction* data = from.interactions.data();
      for (size_t i = 0; i < from.interactions.count(); ++i) {
        if (static_cast<int>(data[i].action) != probe.producer) continue;
        if (probe.producerValue != kAnyValue && static_cast<int>(data[i].value) != probe.producerValue) continue;
        ++found;
        const fui::Rect r = data[i].rect;
        // What the NEW screen does at the four corners and the centre of the
        // rect the finger is provably on.
        const int xs[3] = {r.x + 2, r.x + r.width / 2, r.x + r.width - 3};
        const int ys[3] = {r.y + 2, r.y + r.height / 2, r.y + r.height - 3};
        for (int yi = 0; yi < 3; ++yi) {
          for (int xi = 0; xi < 3; ++xi) {
            const fui::ActionEvent e = to.tap(xs[xi], ys[yi]);
            if (e.action != 0) {
              ++live;
              seen = static_cast<int>(e.action);
            }
          }
        }
        std::printf("   PRODUCER action %d/%d rect x=%d y=%d w=%d h=%d -> new screen answers at %d of %d probe points",
                    probe.producer, static_cast<int>(data[i].value), r.x, r.y, r.width, r.height, live,
                    kProducerProbePoints);
        if (live > 0) std::printf(" (e.g. action %d)", seen);
        std::printf("\n");
      }
      // The check that three probes needed and none of them got: a producer
      // the fixture never registered printed NOTHING, which reads exactly
      // like a producer that collides with nothing.
      char what[192];
      std::snprintf(what, sizeof(what),
                    "%s: the producing control (action %d) is on the FROM screen the fixture builds", probe.name,
                    probe.producer);
      if (check(found > 0, what)) {
        std::snprintf(what, sizeof(what),
                      "%s: the new screen answers at %d of %d points inside the producer (found %d)", probe.name,
                      probe.producerLive, kProducerProbePoints, live);
        check(live == probe.producerLive, what);
      }
    }
  }
  std::vector<std::vector<Overlap>> runs;
  std::vector<std::vector<Cell>> fromRuns;
  for (int pass = 0; pass < 2; ++pass) {
    gCharWidth = pass == 0 ? 10 : 18;
    gLineHeight = pass == 0 ? 20 : 45;
    Built from;
    Built to;
    probe.buildFrom(from);
    probe.buildTo(to);
    std::vector<Cell> a = latticeOf(from, w, h, kStep);
    std::vector<Cell> b = latticeOf(to, w, h, kStep);
    fromRuns.push_back(a);
    runs.push_back(collide(a, b, w, h, kStep));
  }
  gCharWidth = 10;
  gLineHeight = 20;

  if (runs[0].empty()) {
    std::printf("   no pixel carries a different action across this transition\n");
  }
  std::vector<std::string> seenKeys;
  for (const auto& o : runs[0]) {
    // Stable across both metrics?
    bool stable = false;
    for (const auto& other : runs[1]) {
      if (other.fromAction == o.fromAction && other.fromValue == o.fromValue && other.toAction == o.toAction &&
          other.toValue == o.toValue) {
        stable = true;
        break;
      }
    }
    const int fromArea = areaOf(fromRuns[0], o.fromAction, o.fromValue);
    const int pct = fromArea > 0 ? (o.points * 100) / fromArea : 0;
    seenKeys.push_back(key(o));
    std::printf("   action %d/%d  ->  action %d/%d   %d%% of the source control (%d of %d lattice pts)\n", o.fromAction,
                o.fromValue, o.toAction, o.toValue, pct, o.points, fromArea);
    std::printf("      overlap box x[%d..%d] y[%d..%d]   %s\n", o.minX, o.maxX + kStep - 1, o.minY, o.maxY + kStep - 1,
                stable ? "stable at both text metrics" : "METRIC-DEPENDENT -- do not quote");
  }

  // The list in the table against what the lattice just found, both ways.
  // One check, because a probe either matches its declaration or it does not;
  // every individual difference is printed above it either way.
  std::vector<std::string> knownKeys;
  for (const char* const* k = probe.known; k != nullptr && *k != nullptr; ++k) knownKeys.push_back(*k);

  int unexpected = 0;
  for (const auto& k : seenKeys) {
    if (std::find(knownKeys.begin(), knownKeys.end(), k) != knownKeys.end()) continue;
    ++unexpected;
    std::printf("   FAIL: %s: a collision that is not written down: %s. Two controls share pixels across this\n",
                probe.name, k.c_str());
    std::printf("         screen change and nobody decided that was acceptable. Fix it, or add it to `known`\n");
    std::printf("         with a line saying why it is survivable.\n");
  }
  int vanished = 0;
  for (const auto& k : knownKeys) {
    if (std::find(seenKeys.begin(), seenKeys.end(), k) != seenKeys.end()) continue;
    ++vanished;
    std::printf("   FAIL: %s: %s is written down as known and no longer happens. Delete the line: a waiver\n",
                probe.name, k.c_str());
    std::printf("         that outlives its finding is a door held open for the next one.\n");
  }
  char what[192];
  std::snprintf(what, sizeof(what), "%s: the collisions found are exactly the ones written down (%d new, %d stale)",
                probe.name, unexpected, vanished);
  check(unexpected == 0 && vanished == 0, what);
}

// --- probes -----------------------------------------------------------------

void wavelengthDial(Built& out) {
  wavelengthui::DialModel model;
  build(out, false, [&](toybox::Screen& s) { wavelengthui::renderDial(s, model); });
}

void wavelengthReveal(Built& out) {
  wavelengthui::RevealModel model;
  model.points = 3;
  model.roundNumber = 2;
  model.total = 7;
  build(out, false, [&](toybox::Screen& s) { wavelengthui::renderReveal(s, model); });
}

void minesweeperBoardSettled(Built& out) {
  mineui::BoardModel model;
  minesweeper::start(model.game, 12345u);
  model.showMines = true;
  // SETTLED, which is the state this probe is named for. buildBoard draws the
  // verdict capsule only under ms::over(game); with a Fresh game it drew the
  // DIG/FLAG strip, so the probe measured the wrong screen and its producer
  // (ActionSeeResult) was not on it.
  model.game.status = minesweeper::Status::Won;
  build(out, false, [&](toybox::Screen& s) { mineui::buildBoard(s, model); });
}

void minesweeperResult(Built& out) {
  mineui::ResultModel model;
  model.won = true;
  model.revealed = 60;
  model.flagsRight = 10;
  build(out, false, [&](toybox::Screen& s) { mineui::buildResult(s, model); });
}

void sudokuBoard(Built& out) {
  sudokuui::BoardModel model;
  // Finished, for the same reason as minesweeper above: drawRail registers
  // ActionSeeResult on the status capsule only once solvedFlag is set. An
  // unsolved grid made this "BOARD -> RESULT" probe a transition that cannot
  // happen, measured against the two controls that are not the door.
  model.game.solvedFlag = 1;
  build(out, false, [&](toybox::Screen& s) { sudokuui::buildBoard(s, model); });
}

void sudokuResult(Built& out) {
  sudokuui::ResultModel model;
  model.elapsedMs = 605000;
  model.clues = 30;
  build(out, false, [&](toybox::Screen& s) { sudokuui::buildResult(s, model); });
}

void battleshipGameOverBoard(Built& out) {
  bshipui::BoardModel model;
  model.report = "MARIO SANK YOUR CRUISER";
  model.status = "PLAY AGAIN";
  model.gameOver = true;
  model.theirName = "CALM BLUE OWL";
  build(out, false, [&](toybox::Screen& s) { bshipui::buildBoardChrome(s, model); });
}

void linkRematchScreen(Built& out) {
  linkui::LinkModel model;
  model.gameTitle = "BATTLESHIP";
  model.headline = "THEY WIN IN 41 SHOTS";
  model.yourName = "YOU";
  model.yourFaceName = "BRAVE RED FOX";
  model.theirName = "CALM BLUE OWL";
  model.you = linkui::SeatState::Deciding;
  model.them = linkui::SeatState::Deciding;
  model.linked = true;
  model.offerPlayAgain = true;
  build(out, false, [&](toybox::Screen& s) { linkui::buildLink(s, model); });
}

void studyDeck(Built& out) {
  studyui::DeckModel model;
  model.name = "SPANISH";
  model.due = 12;
  model.total = 400;
  std::snprintf(model.syncSubtitle, sizeof(model.syncSubtitle), "NOT PAIRED YET");
  build(out, false, [&](toybox::Screen& s) { studyui::buildDeck(s, model); });
}

void studySyncVerdict(Built& out) {
  studyui::SyncFlowModel model;
  model.verdict = studyui::SyncVerdictKind::Error;
  std::snprintf(model.title, sizeof(model.title), "NO ANSWER");
  std::snprintf(model.body, sizeof(model.body), "The bridge did not answer on this network.");
  std::snprintf(model.whatNow, sizeof(model.whatNow), "TRY AGAIN");
  build(out, false, [&](toybox::Screen& s) { studyui::buildSyncFlow(s, model); });
}

void instapaperQueue(Built& out) {
  instapaperui::QueueModel model;
  model.count = 0;
  build(out, false, [&](toybox::Screen& s) { instapaperui::buildQueue(s, model); });
}

void instapaperNotice(Built& out) {
  instapaperui::NoticeModel model;
  model.headline = "SYNCED";
  model.message = "3 did not arrive; sync again.";
  model.actionLabel = "BACK TO THE LIST";
  build(out, false, [&](toybox::Screen& s) { instapaperui::buildNotice(s, model); });
}

void insiderQuestions(Built& out) {
  insiderui::QuestionsModel model;
  model.secondsLeft = 3;
  build(out, false, [&](toybox::Screen& s) { insiderui::buildQuestions(s, model); });
}

void insiderReveal(Built& out) {
  insiderui::RevealModel model;
  model.outcome = insider::Outcome::OutOfTime;
  model.insiderSeat = 2;
  model.accused = insider::kNoInsider;
  model.word = "LIGHTHOUSE";
  build(out, false, [&](toybox::Screen& s) { insiderui::buildReveal(s, model); });
}

void insiderVote(Built& out) {
  insiderui::VoteModel model;
  model.chosen = 2;
  build(out, false, [&](toybox::Screen& s) { insiderui::buildVote(s, model); });
}

void triviaClue(Built& out) {
  triviaui::QuestionModel model;
  model.clue = "THIS CITY HOSTED THE 1992 SUMMER OLYMPICS";
  model.answer = nullptr;
  model.difficulty = 3;
  model.asked = 4;
  build(out, false, [&](toybox::Screen& s) { triviaui::buildQuestion(s, model); });
}

void triviaAnswer(Built& out) {
  triviaui::QuestionModel model;
  model.clue = "THIS CITY HOSTED THE 1992 SUMMER OLYMPICS";
  model.answer = "BARCELONA";
  model.difficulty = 3;
  model.asked = 4;
  build(out, false, [&](toybox::Screen& s) { triviaui::buildQuestion(s, model); });
}

void foreheadReady(Built& out) {
  foreheadui::ReadyModel model;
  build(out, true, [&](toybox::Screen& s) { foreheadui::buildReady(s, model); });
}

void foreheadPlay(Built& out) {
  foreheadui::PlayModel model;
  model.word = "LIGHTHOUSE";
  model.secondsLeft = 58;
  build(out, true, [&](toybox::Screen& s) { foreheadui::buildPlay(s, model); });
}

void linkRematch(Built& out) {
  linkui::LinkModel model;
  model.gameTitle = "TOY BATTLE";
  model.headline = "YOU WIN";
  model.yourName = "YOU";
  model.yourFaceName = "BRAVE RED FOX";
  model.theirName = "CALM BLUE OWL";
  model.you = linkui::SeatState::Deciding;
  model.them = linkui::SeatState::Deciding;
  model.linked = true;
  model.offerPlayAgain = true;
  build(out, false, [&](toybox::Screen& s) { linkui::buildLink(s, model); });
}

void dumpOnly(const char* label, void (*builder)(Built&)) {
  Built built;
  builder(built);
  std::printf("\n== %s\n", label);
  dump("    ", built);
}

// The collisions each transition is known to have. One array per probe so
// each carries its own reasoning; REVEAL-FINDINGS.md at the workspace root is
// the longer version, and says which of these are owned and which are not.
//
// A key is "fromAction/fromValue->toAction/toValue". Adding one is a decision:
// it says a person looked at this pair of controls and judged the collision
// survivable, or tracked elsewhere. Removing one is what happens when the
// collision is fixed.
const char* const kNone[] = {nullptr};
const char* const kMinesweeperKnown[] = {"9/0->7/0", nullptr};
// Measured, not assumed. With the grid solved the SOLVED capsule itself is
// clear of RESULT (0 of 9 points answer inside it), and the two controls
// that do collide are UNDO and HINT, which are not the door the player
// takes. Both were in every run this sweep ever printed.
const char* const kSudokuKnown[] = {"4/0->8/0", "5/0->7/0", nullptr};
const char* const kBattleshipKnown[] = {"5/0->201/0", nullptr};
const char* const kInsiderQuestionsKnown[] = {"5/0->9/0", nullptr};
const char* const kInsiderVoteKnown[] = {"6/-1->8/0", "7/0->9/0", nullptr};
const char* const kForeheadKnown[] = {"13/0->7/0", "13/0->8/0", nullptr};
const char* const kTriviaKnown[] = {"2/0->3/0", "2/0->4/0", nullptr};
const char* const kStudyKnown[] = {"1/2->7/1", nullptr};
const char* const kInstapaperKnown[] = {"321/0->325/0", nullptr};

const Probe kProbes[] = {
    {"wavelength: DIAL (hold to lock) -> REVEAL (the score)", false, wavelengthDial, wavelengthReveal,
     wavelengthui::ActionLock, kAnyValue, 0, kNone,
     "clean since v1.12.9 -- LOCK is a plain button guarded by geometry, and the reveal puts nothing under it"},
    {"minesweeper: settled BOARD (verdict capsule) -> RESULT", false, minesweeperBoardSettled, minesweeperResult,
     mineui::ActionSeeResult, kAnyValue, kProducerProbePoints, kMinesweeperKnown,
     "the verdict capsule and RESULT's own button are the same bottom pill; open, unowned"},
    {"sudoku: BOARD -> RESULT", false, sudokuBoard, sudokuResult, sudokuui::ActionSeeResult, kAnyValue, 0, kSudokuKnown,
     "the door itself is clear; UNDO and HINT land on RESULT's DONE and AGAIN. Open, unowned"},
    {"battleship: game-over BOARD (PLAY AGAIN capsule) -> shared LINK screen", false, battleshipGameOverBoard,
     linkRematchScreen, bshipui::ActionPlayAgain, kAnyValue, kProducerProbePoints, kBattleshipKnown,
     "closed by S1 (PaintClock/RevealedInteractions, v1.12.10): the rects still coincide, the timing window is gone"},
    {"insider: QUESTIONS (WE SAID THE WORD) -> REVEAL, on the 300s clock expiring", false, insiderQuestions,
     insiderReveal, insiderui::ActionFoundWord, kAnyValue, kProducerProbePoints, kInsiderQuestionsKnown,
     "genuine same-rect overlap, still open per REVEAL-FINDINGS.md"},
    {"insider: VOTE (confirm the accusation) -> REVEAL", false, insiderVote, insiderReveal,
     insiderui::ActionConfirmVote, kAnyValue, kProducerProbePoints, kInsiderVoteKnown,
     "genuine same-rect overlap, still open per REVEAL-FINDINGS.md"},
    {"forehead: READY (tap to start) -> PLAY (GOT / MISSED bands), landscape", true, foreheadReady, foreheadPlay,
     foreheadui::ActionStart, kAnyValue, 3, kForeheadKnown,
     "READY is the whole panel and PLAY splits it into two bands, so a third of it is unavoidably live"},
    {"trivia: CLUE (REVEAL) -> ANSWER (NEXT / HIDE)", false, triviaClue, triviaAnswer, triviaui::ActionReveal,
     kAnyValue, kProducerProbePoints, kTriviaKnown, "genuine same-rect overlap, still open per REVEAL-FINDINGS.md"},
    // The SYNC door is a list row: ActionStudy carrying value 2. The table
    // named ActionSync, which no screen registers, so this probe's producer
    // check printed nothing for its whole life.
    {"study: DECK (SYNC door) -> SYNC VERDICT", false, studyDeck, studySyncVerdict, studyui::ActionStudy, 2,
     kProducerProbePoints, kStudyKnown, "genuine same-rect overlap, still open per REVEAL-FINDINGS.md"},
    {"instapaper: QUEUE (SYNC) -> NOTICE (the sync verdict)", false, instapaperQueue, instapaperNotice,
     instapaperui::ActionSync, kAnyValue, kProducerProbePoints, kInstapaperKnown,
     "genuine same-rect overlap, still open per REVEAL-FINDINGS.md"},
};

}  // namespace

int main() {
  std::printf("reveal sweep: same pixel, different action across a screen change\n");
  std::printf("lattice step %dpx; action 0 = nothing tappable there\n", kStep);
  for (const auto& probe : kProbes) report(probe);
  dumpOnly("link screen, rematch state (every multiplayer game shares it)", linkRematch);

  // The line check.sh counts. Without it the suite reported
  // "revealsweep ok (0 sub-suite(s))" and the zero looked like a formatting
  // quirk rather than the whole truth about it.
  std::printf("\n%d checks, %d failed\n", gChecks, gFailed);
  return gFailed == 0 ? 0 : 1;
}
