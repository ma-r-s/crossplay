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

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../../src/apps_local/battleship/BattleshipScreens.h"
#include "../../src/apps_local/forehead/ForeheadScreens.h"
#include "../../src/apps_local/insider/InsiderScreens.h"
#include "../../src/apps_local/instapaper/InstapaperScreens.h"
#include "../../src/apps_local/trivia/TriviaScreens.h"
#include "../../src/apps_local/link/LinkScreens.h"
#include "../../src/apps_local/study/StudyScreens.h"
#include "../../src/apps_local/minesweeper/MinesweeperScreens.h"
#include "../../src/apps_local/sudoku/SudokuScreens.h"
#include "../../src/apps_local/ui/ToyboxScreen.h"
#include "../../src/apps_local/wavelength/WavelengthScreens.h"

namespace fui = freeink::ui;

namespace {

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

struct Probe {
  const char* name;
  bool landscape;
  void (*buildFrom)(Built&);
  void (*buildTo)(Built&);
  // The action on the FROM screen that produces the TO screen, or 0 if the
  // transition is not a touch at all. This is the one whose rect a finger is
  // provably on at the moment the new screen is drawn.
  int producer = 0;
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
  {
    Built from;
    Built to;
    probe.buildFrom(from);
    probe.buildTo(to);
    dump("FROM", from);
    dump("TO  ", to);
    if (probe.producer != 0) {
      const fui::Interaction* data = from.interactions.data();
      for (size_t i = 0; i < from.interactions.count(); ++i) {
        if (static_cast<int>(data[i].action) != probe.producer) continue;
        const fui::Rect r = data[i].rect;
        // What the NEW screen does at the four corners and the centre of the
        // rect the finger is provably on.
        const int xs[3] = {r.x + 2, r.x + r.width / 2, r.x + r.width - 3};
        const int ys[3] = {r.y + 2, r.y + r.height / 2, r.y + r.height - 3};
        int live = 0;
        int total = 0;
        int seen = 0;
        for (int yi = 0; yi < 3; ++yi) {
          for (int xi = 0; xi < 3; ++xi) {
            ++total;
            const fui::ActionEvent e = to.tap(xs[xi], ys[yi]);
            if (e.action != 0) {
              ++live;
              seen = static_cast<int>(e.action);
            }
          }
        }
        std::printf("   PRODUCER action %d rect x=%d y=%d w=%d h=%d -> new screen answers at %d of %d probe points",
                    probe.producer, r.x, r.y, r.width, r.height, live, total);
        if (live > 0) std::printf(" (e.g. action %d)", seen);
        std::printf("\n");
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
    return;
  }
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
    std::printf("   action %d/%d  ->  action %d/%d   %d%% of the source control (%d of %d lattice pts)\n",
                o.fromAction, o.fromValue, o.toAction, o.toValue, pct, o.points, fromArea);
    std::printf("      overlap box x[%d..%d] y[%d..%d]   %s\n", o.minX, o.maxX + kStep - 1, o.minY,
                o.maxY + kStep - 1, stable ? "stable at both text metrics" : "METRIC-DEPENDENT -- do not quote");
  }
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

const Probe kProbes[] = {
    {"wavelength: DIAL (hold to lock) -> REVEAL (the score)", false, wavelengthDial, wavelengthReveal,
     wavelengthui::ActionLock},
    {"minesweeper: settled BOARD (verdict capsule) -> RESULT", false, minesweeperBoardSettled, minesweeperResult,
     mineui::ActionSeeResult},
    {"sudoku: BOARD -> RESULT", false, sudokuBoard, sudokuResult, sudokuui::ActionSeeResult},
    {"battleship: game-over BOARD (PLAY AGAIN capsule) -> shared LINK screen", false, battleshipGameOverBoard,
     linkRematchScreen, bshipui::ActionPlayAgain},
    {"insider: QUESTIONS (WE SAID THE WORD) -> REVEAL, on the 300s clock expiring", false, insiderQuestions,
     insiderReveal, insiderui::ActionFoundWord},
    {"insider: VOTE (confirm the accusation) -> REVEAL", false, insiderVote, insiderReveal,
     insiderui::ActionConfirmVote},
    {"forehead: READY (tap to start) -> PLAY (GOT / MISSED bands), landscape", true, foreheadReady, foreheadPlay,
     foreheadui::ActionStart},
    {"trivia: CLUE (REVEAL) -> ANSWER (NEXT / HIDE)", false, triviaClue, triviaAnswer, triviaui::ActionReveal},
    {"study: DECK (SYNC door) -> SYNC VERDICT", false, studyDeck, studySyncVerdict, studyui::ActionSync},
    {"instapaper: QUEUE (SYNC) -> NOTICE (the sync verdict)", false, instapaperQueue, instapaperNotice,
     instapaperui::ActionSync},
};

}  // namespace

int main() {
  std::printf("reveal sweep: same pixel, different action across a screen change\n");
  std::printf("lattice step %dpx; action 0 = nothing tappable there\n", kStep);
  for (const auto& probe : kProbes) report(probe);
  dumpOnly("link screen, rematch state (every multiplayer game shares it)", linkRematch);
  std::printf("\ndone\n");
  return 0;
}
