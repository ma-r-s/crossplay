#include "SeaSaltActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdio>
#include <cstring>

#include "../Shelf.h"
#include "../ui/ToyboxFormat.h"
#include "../ui/ToyboxSeed.h"
#include "../ui/ToyboxTheme.h"
#include "SeaSaltCards.h"

namespace {

namespace fui = freeink::ui;

constexpr char kSavePath[] = "/.crosspoint/seasalt.sav";
constexpr char kStatsPath[] = "/.crosspoint/seasalt.stats";
constexpr int kSaveVersion = 1;

char hexDigit(const int v) { return static_cast<char>(v < 10 ? '0' + v : 'a' + (v - 10)); }
int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return -1;
}

bool isPair(const uint8_t a, const uint8_t b) {
  const seasalt::Kind ka = seasalt::kindOf(a);
  const seasalt::Kind kb = seasalt::kindOf(b);
  if (ka == kb) return ka == seasalt::Kind::Crab || ka == seasalt::Kind::Boat || ka == seasalt::Kind::Fish;
  return (ka == seasalt::Kind::Swimmer && kb == seasalt::Kind::Shark) ||
         (ka == seasalt::Kind::Shark && kb == seasalt::Kind::Swimmer);
}

}  // namespace

std::unique_ptr<Activity> SeaSaltActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<SeaSaltActivity>(renderer, mappedInput);
}

void SeaSaltActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  // Mixed from the clock, so two games in a row are not the same game.
  seed = toybox::seed();
  loadStats();
  hasSavedGame = loadGame();
  if (!hasSavedGame) {
    game.newGame(nextSeed(), 0);
  }
  refreshContinueDetail();
  view = View::Menu;
  requestUpdate();
}

void SeaSaltActivity::onExit() {
  saveGame();
  saveStats();
  Activity::onExit();
}

// --- link ------------------------------------------------------------------

const char* SeaSaltActivity::linkHeadline() const {
  const int mine = game.score[seat];
  const int theirs = game.score[seat ^ 1];
  if (game.mermaidsHeld(seat) == seasalt::kMermaidsToWin) {
    std::snprintf(headline, sizeof(headline), "FOUR MERMAIDS");
  } else if (game.mermaidsHeld(seat ^ 1) == seasalt::kMermaidsToWin) {
    std::snprintf(headline, sizeof(headline), "THEIR MERMAIDS");
  } else {
    std::snprintf(headline, sizeof(headline), mine >= theirs ? "YOU WIN %d - %d" : "THEY WIN %d - %d", mine, theirs);
  }
  return headline;
}

void SeaSaltActivity::onMatchStart(const bool goesFirst) {
  // The dealer deals and sends; the other device adopts the state whole, so
  // both play the same shuffle without a seed ever crossing on its own.
  seat = goesFirst ? 0 : 1;
  // BOTH sides clear the table; only the dealer's deal is the one that counts,
  // and it arrives in the first packet. The follower used to keep whatever it
  // was holding, so a match begun after a finished game started with
  // matchGameOver() already true -- which counted that old game a second time
  // and put its score screen up as the new match's opening view.
  game.newGame(nextSeed(), 0);
  if (goesFirst) link.play(game);
  statsCounted = false;
  clearSelection();
  report[0] = '\0';
  tab = 0;
  page = 0;
  view = View::Board;
  requestUpdate();
}

bool SeaSaltActivity::takeOpponentState() {
  seasalt::Game incoming;
  if (!link.takeOpponent(incoming)) return false;
  static const char* kNames[seasalt::kKindCount] = {
      "CRAB", "BOAT",   "FISH",    "SWIMMER",    "SHARK", "SHELL", "TURTLE",
      "GULL", "SAILOR", "MERMAID", "LIGHTHOUSE", "SHOAL", "NEST",  "CAPTAIN",
  };
  seasalt::describeTheirTurn(game, incoming, seat, kNames, report, sizeof(report));
  game = incoming;
  clearSelection();
  if (game.currentPhase() == seasalt::Phase::GameOver) countMatchEnd();
  view = viewForStep();
  // The rules may have nothing for this device even though the transport just
  // handed it the turn: LAST CHANCE moves the game turn without a send of its
  // own, and the loser deals a round the winner banked. Hand it straight back
  // rather than sitting on it. See SeaSaltLink.h.
  if (seasalt::linkAction(game, seat, linkYourTurn()) == seasalt::LinkAction::Pass) link.play(game);
  requestUpdate();
  return true;
}

void SeaSaltActivity::onRematch() { onMatchStart(linkYourTurn()); }

void SeaSaltActivity::onLinkEnded() {
  seat = 0;
  hasSavedGame = loadGame();
  if (!hasSavedGame) game.newGame(nextSeed(), 0);
  goToMenu();
}

void SeaSaltActivity::drawLinkArt(const Rect& slot) {
  // A fan of the game's own card faces, the menu ornament reused.
  namespace fui = freeink::ui;
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const int16_t size = 48;
  const int kinds[4] = {1, 0, 9, 4};  // boat, crab, mermaid, shark
  const int16_t step = static_cast<int16_t>((slot.width - size) / 3);
  for (int i = 0; i < 4; ++i) {
    target.bitmap(fui::makeRect(static_cast<int16_t>(slot.x + i * step),
                                static_cast<int16_t>(slot.y + (slot.height - size) / 2), size, size),
                  fui::bitmapFromIcon(seasaltui::kindIcon48(kinds[i])), fui::BitmapMode::Contain,
                  fui::Paint::solid(fui::Color::Black));
  }
}

// --- models ----------------------------------------------------------------

seasaltui::StartModel SeaSaltActivity::startModel() const {
  seasaltui::StartModel model;
  model.hasSavedGame = hasSavedGame;
  model.continueDetail = continueDetail;
  model.played = played;
  model.won = won;
  model.selected = menuSelected;
  return model;
}

int SeaSaltActivity::groupPoints(const uint8_t card, const int mySeat) const {
  using namespace seasalt;
  const Kind kind = kindOf(card);
  const int held = game.countHeld(mySeat, kind);
  switch (kind) {
    case Kind::Crab:
    case Kind::Boat:
    case Kind::Fish:
      return held / 2;
    case Kind::Swimmer:
    case Kind::Shark: {
      const int sw = game.countHeld(mySeat, Kind::Swimmer);
      const int sh = game.countHeld(mySeat, Kind::Shark);
      return sw < sh ? sw : sh;
    }
    case Kind::Shell:
      return kShellScore[held];
    case Kind::Octopus:
      return kOctopusScore[held];
    case Kind::Penguin:
      return kPenguinScore[held];
    case Kind::Sailor:
      return kSailorScore[held];
    case Kind::Lighthouse:
      return game.countHeld(mySeat, Kind::Boat);
    case Kind::ShoalOfFish:
      return game.countHeld(mySeat, Kind::Fish);
    case Kind::PenguinColony:
      return 2 * game.countHeld(mySeat, Kind::Penguin);
    case Kind::Captain:
      return 3 * game.countHeld(mySeat, Kind::Sailor);
    case Kind::Mermaid: {
      // The mermaids' whole share: score with them minus score without them.
      seasalt::Game bare = game;
      for (int c = 0; c < kCards; ++c) {
        if (kindOf(static_cast<uint8_t>(c)) == Kind::Mermaid &&
            (bare.place[c] == static_cast<uint8_t>(handOf(mySeat)) ||
             bare.place[c] == static_cast<uint8_t>(tableOf(mySeat)))) {
          bare.place[c] = static_cast<uint8_t>(Place::Deck);
        }
      }
      return game.cardPoints(mySeat) - bare.cardPoints(mySeat);
    }
  }
  return 0;
}

seasaltui::CardTile SeaSaltActivity::tileFor(const uint8_t card, const int forSeat, const bool selected) const {
  seasaltui::CardTile tile;
  tile.kind = static_cast<uint8_t>(seasalt::kindOf(card));
  tile.colour = static_cast<uint8_t>(seasalt::colourOf(card));
  tile.groupPoints = static_cast<int8_t>(groupPoints(card, forSeat));
  tile.supply = seasalt::kKindSupply[tile.kind];
  tile.selected = selected;
  return tile;
}

seasaltui::PileTile SeaSaltActivity::pileTileFor(const int pile) const {
  seasaltui::PileTile tile;
  const uint8_t top = game.pileTop(pile);
  tile.size = static_cast<uint8_t>(game.pileSize(pile));
  if (top != seasalt::kNoCard) {
    tile.kind = static_cast<uint8_t>(seasalt::kindOf(top));
    tile.colour = static_cast<uint8_t>(seasalt::colourOf(top));
  }
  return tile;
}

seasaltui::BoardModel SeaSaltActivity::boardModel() {
  using namespace seasalt;
  seasaltui::BoardModel model;
  model.yourTotal = game.score[seat];
  model.theirTotal = game.score[seat ^ 1];
  model.theyHold = game.handSize(seat ^ 1);
  model.theirTableCount = game.tableSize(seat ^ 1);
  model.yourTableCount = game.tableSize(seat);
  // Your biggest colour group, mark and count.
  int best = 0, bestAt = 0;
  for (int i = 0; i < kColourCount; ++i) {
    const int n = game.countHeldColour(seat, static_cast<Colour>(i));
    if (n > best) {
      best = n;
      bestAt = i;
    }
  }
  model.bestColourCount = best;
  model.bestColour = static_cast<uint8_t>(bestAt);
  model.deckCount = game.deckRemaining();
  model.piles[0] = pileTileFor(0);
  model.piles[1] = pileTileFor(1);
  model.tab = tab;
  model.handCount = game.handSize(seat);
  model.yoursCount = game.tableSize(seat);
  model.theirsCount = game.tableSize(seat ^ 1);

  // What the grid shows. The hand tab carries the current DECISION when one
  // is pending: the two drawn cards, the rejected one awaiting a pile, or the
  // pile being dug. Cards appear at grid size in grid positions, always --
  // the modal chooser screens that resized and re-homed them are gone.
  using seasalt::Step;
  const Step step = game.currentStep();
  if (tab == 0 && myTurn()) {
    switch (step) {
      case Step::ChooseKeep:
        model.handTabLabel = "KEEP";
        break;
      case Step::ChoosePile:
        model.handTabLabel = "PLACE";
        break;
      case Step::CrabPick:
        model.handTabLabel = "DIG";
        break;
      default:
        break;
    }
  }
  if (tab == 0 && myTurn() && step == Step::ChooseKeep) {
    for (int i = 0; i < 2; ++i) {
      if (game.drawn[i] == seasalt::kNoCard) continue;
      model.tiles[model.tileCount++] = tileFor(game.drawn[i], seat, false);
    }
    model.pages = 1;
    model.page = 0;
  } else if (tab == 0 && myTurn() && step == Step::ChoosePile && game.pendingDiscard != seasalt::kNoCard) {
    model.tiles[model.tileCount++] = tileFor(game.pendingDiscard, seat, false);
    model.pages = 1;
    model.page = 0;
  } else if (tab == 0 && myTurn() && step == Step::CrabPick) {
    const int total = game.pileSize(game.crabPile);
    model.pages =
        total > 0 ? (total + seasaltui::BoardModel::kMaxBoardTiles - 1) / seasaltui::BoardModel::kMaxBoardTiles : 1;
    if (page >= model.pages) page = model.pages - 1;
    model.page = page;
    int skip = page * seasaltui::BoardModel::kMaxBoardTiles;
    for (int c = 0; c < seasalt::kCards && model.tileCount < seasaltui::BoardModel::kMaxBoardTiles; ++c) {
      if (game.place[c] != static_cast<uint8_t>(seasalt::pileAt(game.crabPile))) continue;
      if (skip-- > 0) continue;
      model.tiles[model.tileCount++] = tileFor(static_cast<uint8_t>(c), seat, false);
    }
  } else {
    const seasalt::Place where =
        tab == 0 ? seasalt::handOf(seat) : (tab == 1 ? seasalt::tableOf(seat) : seasalt::tableOf(seat ^ 1));
    const int forSeat = tab == 2 ? seat ^ 1 : seat;
    const int total = tab == 0 ? model.handCount : (tab == 1 ? model.yoursCount : model.theirsCount);
    model.pages =
        total > 0 ? (total + seasaltui::BoardModel::kMaxBoardTiles - 1) / seasaltui::BoardModel::kMaxBoardTiles : 1;
    if (page >= model.pages) page = model.pages - 1;
    model.page = page;
    int skip = page * seasaltui::BoardModel::kMaxBoardTiles;
    for (int c = 0; c < seasalt::kCards && model.tileCount < seasaltui::BoardModel::kMaxBoardTiles; ++c) {
      if (game.place[c] != static_cast<uint8_t>(where)) continue;
      if (skip-- > 0) continue;
      const bool selected = tab == 0 && (sel[0] == static_cast<uint8_t>(c) || sel[1] == static_cast<uint8_t>(c));
      model.tiles[model.tileCount++] = tileFor(static_cast<uint8_t>(c), forSeat, selected);
    }
  }

  composeHint();
  model.hint = hint;
  model.primaryLabel = primaryLabel;
  model.primaryEnabled = primaryEnabled;
  model.callPoints = game.cardPoints(seat);
  model.canCall = mayAct() && game.currentStep() == Step::Play && game.currentPhase() == seasalt::Phase::Playing &&
                  game.mayEndRound(seat);
  return model;
}

seasaltui::RoundModel SeaSaltActivity::roundModel() const {
  seasaltui::RoundModel model;
  model.round = game.round;
  model.deckOut = game.ender == seasalt::kNoSeat && game.mermaidsHeld(0) < seasalt::kMermaidsToWin &&
                  game.mermaidsHeld(1) < seasalt::kMermaidsToWin;
  model.mermaidWin = game.mermaidsHeld(0) == seasalt::kMermaidsToWin || game.mermaidsHeld(1) == seasalt::kMermaidsToWin;
  model.youCalled = game.ender == seat;
  model.wasLastChance = game.betWasLastChance != 0;
  model.betWon = game.betWon();
  model.yourCards = game.cardPoints(seat);
  model.theirCards = game.cardPoints(seat ^ 1);
  model.yourBonus = game.colourBonus(seat);
  model.theirBonus = game.colourBonus(seat ^ 1);
  model.yourBanked = game.roundScore(seat);
  model.theirBanked = game.roundScore(seat ^ 1);
  model.yourTotal = game.score[seat];
  model.theirTotal = game.score[seat ^ 1];
  model.matchOver = game.currentPhase() == seasalt::Phase::GameOver;
  model.youWonMatch = game.matchWinner() == seat;
  model.theirName = inMatch() ? opponentName() : nullptr;
  model.waitingOnThem =
      inMatch() && !model.matchOver && seasalt::linkAction(game, seat, linkYourTurn()) != seasalt::LinkAction::Deal;
  return model;
}

// --- the hint system --------------------------------------------------------

void SeaSaltActivity::composeHint() {
  using namespace seasalt;
  primaryEnabled = false;

  if (!myTurn()) {
    std::snprintf(hint, sizeof(hint), "%s", report[0] ? report : "THEIR TURN.");
    std::snprintf(primaryLabel, sizeof(primaryLabel), "THEIR TURN");
    return;
  }

  switch (game.currentStep()) {
    case Step::Take:
      if (report[0] != '\0') {
        std::snprintf(hint, sizeof(hint), "%s", report);
      } else {
        std::snprintf(hint, sizeof(hint), "TAKE A CARD. DECK DEALS TWO, PILES SHOW WHAT YOU GET.");
      }
      std::snprintf(primaryLabel, sizeof(primaryLabel), "TAKE A CARD");
      return;
    case Step::ChooseKeep:
      std::snprintf(hint, sizeof(hint), "TWO FROM THE DECK. TAP THE ONE YOU KEEP.");
      std::snprintf(primaryLabel, sizeof(primaryLabel), "KEEP ONE");
      return;
    case Step::ChoosePile:
      std::snprintf(
          hint, sizeof(hint), "THE %s GOES FACE UP. TAP A PILE ABOVE.",
          game.pendingDiscard != kNoCard ? seasaltui::kindName(static_cast<int>(kindOf(game.pendingDiscard))) : "CARD");
      std::snprintf(primaryLabel, sizeof(primaryLabel), "PLACE IT");
      return;
    case Step::CrabPile:
      std::snprintf(hint, sizeof(hint), "THE CRABS DIG. TAP A PILE ABOVE TO SEARCH IT.");
      std::snprintf(primaryLabel, sizeof(primaryLabel), "DIG");
      return;
    case Step::CrabPick:
      std::snprintf(hint, sizeof(hint), "TAKE ANY ONE CARD FROM THIS PILE.");
      std::snprintf(primaryLabel, sizeof(primaryLabel), "TAKE ONE");
      return;
    case Step::Play:
      break;
  }

  // Step::Play. What is selected decides everything.
  const int selected = (sel[0] != kNoCard) + (sel[1] != kNoCard);
  if (selected == 2) {
    if (isPair(sel[0], sel[1])) {
      const int k = static_cast<int>(kindOf(sel[0]));
      std::snprintf(hint, sizeof(hint), "%s", seasaltui::pairHint(k <= 4 ? k : 0));
      std::snprintf(primaryLabel, sizeof(primaryLabel), "PLAY THE PAIR");
      primaryEnabled = true;
    } else {
      std::snprintf(hint, sizeof(hint), "THESE TWO DO NOT PAIR. A PAIR IS TWO ALIKE, OR SWIMMER AND SHARK.");
      std::snprintf(primaryLabel, sizeof(primaryLabel), "END TURN");
      primaryEnabled = true;
    }
    return;
  }
  if (selected == 1) {
    std::snprintf(hint, sizeof(hint), "%s", seasaltui::kindHint(static_cast<int>(kindOf(sel[0]))));
    std::snprintf(primaryLabel, sizeof(primaryLabel), "END TURN");
    primaryEnabled = true;
    return;
  }
  if (tab != 0) {
    std::snprintf(hint, sizeof(hint), "PAIRS LAID HERE ALREADY DID THEIR WORK. POINTS COUNT EITHER WAY.");
  } else if (report[0] != '\0') {
    std::snprintf(hint, sizeof(hint), "%s", report);
  } else {
    std::snprintf(hint, sizeof(hint), "TAP A CARD TO SEE WHAT IT DOES. END THE TURN WHEN DONE.");
  }
  std::snprintf(primaryLabel, sizeof(primaryLabel), "END TURN");
  primaryEnabled = true;
}

// --- drawing ---------------------------------------------------------------

namespace {
template <typename Model>
freeink::ui::Rect paint(GfxRenderer& renderer, toybox::Interactions& interactions, bool& interactionsReady,
                        freeink::ui::Rect (*build)(toybox::Screen&, const Model&), const Model& model,
                        const char* name) {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = build(screen, model);
  interactionsReady = true;
  toybox::reportOverflow(interactions, name);
  return slot;
}
}  // namespace

void SeaSaltActivity::drawStartMenu() {
  const fui::Rect slot =
      paint(renderer, interactions, interactionsReady, seasaltui::buildStartMenu, startModel(), "SeaSalt menu");
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);

  // THE DECK, Mario's pick of three rendered ornaments: the distribution card
  // that ships in the real box -- every face and its count. The menu's dead
  // middle becomes the one reference a player actually consults between
  // rounds, and nothing else on the device shows it.
  const int16_t rowH = 44;
  const int16_t colW = static_cast<int16_t>(slot.width / 2);
  const int16_t top = static_cast<int16_t>(slot.y + (slot.height - 7 * rowH) / 2);
  fui::TextStyle name;
  name.font = toybox::kSmallFont;
  name.align = fui::TextAlign::Left;
  fui::TextStyle count;
  count.font = toybox::kSmallFont;
  count.align = fui::TextAlign::Right;
  for (int k = 0; k < seasalt::kKindCount; ++k) {
    const int col = k / 7;
    const int row = k % 7;
    const int16_t x = static_cast<int16_t>(slot.x + col * colW + (col ? 14 : 0));
    const int16_t y = static_cast<int16_t>(top + row * rowH);
    target.bitmap(fui::makeRect(x, y + (rowH - 24) / 2, 24, 24), fui::bitmapFromIcon(seasaltui::kindIcon24(k)),
                  fui::BitmapMode::Contain, fui::Paint::solid(fui::Color::Black));
    target.text(fui::makeRect(x + 32, y, colW - 96, rowH), seasaltui::kindName(k), name);
    // "X%d"
    constexpr int kNChars = toybox::kIntChars + toybox::literalChars("X") + 1;
    char n[kNChars];
    std::snprintf(n, sizeof(n), "X%d", seasalt::kKindSupply[k]);
    target.text(fui::makeRect(x + colW - 64, y, 50 - (col ? 14 : 0), rowH), n, count);
  }
}

void SeaSaltActivity::drawBoard() {
  const fui::Rect grid =
      paint(renderer, interactions, interactionsReady, seasaltui::buildBoard, boardModel(), "SeaSalt board");
  gridSlot = Rect{grid.x, grid.y, grid.width, grid.height};
}

void SeaSaltActivity::drawCall() {
  seasaltui::CallModel model;
  model.yourPoints = game.cardPoints(seat);
  paint(renderer, interactions, interactionsReady, seasaltui::buildCallChoice, model, "SeaSalt call");
}

void SeaSaltActivity::drawRoundOver() {
  paint(renderer, interactions, interactionsReady, seasaltui::buildRoundOver, roundModel(), "SeaSalt round");
}

void SeaSaltActivity::drawTutorial() {
  seasaltui::TutorialModel model;
  model.page = tutorialPage;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  seasaltui::buildTutorial(screen, model);
  interactionsReady = true;
  toybox::reportOverflow(interactions, "SeaSalt rules");
}

void SeaSaltActivity::gameRender() {
  switch (view) {
    case View::Menu:
      drawStartMenu();
      break;
    case View::Board:
      drawBoard();
      break;
    case View::Call:
      drawCall();
      break;
    case View::RoundOver:
      drawRoundOver();
      break;
    case View::Rules:
      drawTutorial();
      break;
  }
  renderer.displayBuffer();
}

// --- flow ------------------------------------------------------------------

bool SeaSaltActivity::myTurn() const { return game.turn == static_cast<uint8_t>(seat); }

bool SeaSaltActivity::mayAct() const {
  if (!myTurn()) return false;
  if (!inMatch()) return true;
  // Both must say yes, not merely agree: battleship's subtle version of this
  // guard let a tap through on the opponent's turn because the two nos agreed.
  return linkYourTurn();
}

void SeaSaltActivity::countMatchEnd() {
  if (statsCounted) return;
  statsCounted = true;
  ++played;
  if (game.matchWinner() == seat) ++won;
  saveStats();
}

SeaSaltActivity::View SeaSaltActivity::viewForStep() const {
  switch (game.currentPhase()) {
    case seasalt::Phase::RoundOver:
    case seasalt::Phase::GameOver:
      return View::RoundOver;
    default:
      // Every step of a turn lives on the board: the grid itself shows the
      // pending decision. See boardModel().
      return View::Board;
  }
}

void SeaSaltActivity::clearSelection() {
  sel[0] = seasalt::kNoCard;
  sel[1] = seasalt::kNoCard;
}

void SeaSaltActivity::goToMenu() {
  hasSavedGame = game.currentPhase() != seasalt::Phase::GameOver;
  refreshContinueDetail();
  saveGame();
  clearSelection();
  view = View::Menu;
  requestUpdate();
}

void SeaSaltActivity::startNewGame() {
  game.newGame(nextSeed(), 0);
  statsCounted = false;
  hasSavedGame = true;
  clearSelection();
  report[0] = '\0';
  tab = 0;
  page = 0;
  view = View::Board;
  requestUpdate();
}

void SeaSaltActivity::afterHumanAction() {
  clearSelection();
  const seasalt::Phase phase = game.currentPhase();

  // Over a link, my span ends when the game turn leaves me or the round does:
  // that state is the packet. A boat pair keeps the turn, so nothing sends and
  // I keep playing, which is exactly the rulebook's extra turn.
  if (inMatch() && (game.turn != static_cast<uint8_t>(seat) || phase == seasalt::Phase::RoundOver ||
                    phase == seasalt::Phase::GameOver)) {
    link.play(game);
  }

  if (phase == seasalt::Phase::RoundOver || phase == seasalt::Phase::GameOver) {
    if (phase == seasalt::Phase::GameOver) countMatchEnd();
    view = View::RoundOver;
  } else if (!myTurn() && !inMatch()) {
    view = View::Board;
    opponentPending = true;
  } else {
    view = viewForStep();
  }
  requestUpdate();
}

void SeaSaltActivity::playOpponentTurn() {
  using namespace seasalt;
  const int them = seat ^ 1;
  uint32_t rng = nextSeed();
  report[0] = '\0';
  int laid[5] = {0, 0, 0, 0, 0};
  bool called = false, bet = false, stole = false;

  int guard = 0;
  while (game.currentPhase() != seasalt::Phase::RoundOver && game.currentPhase() != seasalt::Phase::GameOver &&
         game.turn == static_cast<uint8_t>(them) && ++guard < 200) {
    const Decision d = seasalt::decide(observe(game, them), Skill::Navigator, rng);
    bool ok = false;
    switch (d.act) {
      case Decision::Act::TakeDeck:
        ok = game.takeFromDeck();
        break;
      case Decision::Act::TakePile:
        ok = game.takeFromPile(d.a);
        break;
      case Decision::Act::Keep:
        ok = game.keepDrawn(d.a);
        break;
      case Decision::Act::DiscardTo:
        ok = game.discardTo(d.a);
        break;
      case Decision::Act::LayDuo: {
        uint8_t a = kNoCard, b = kNoCard;
        const Kind ka = d.kind == Kind::Swimmer ? Kind::Swimmer : d.kind;
        const Kind kb = d.kind == Kind::Swimmer ? Kind::Shark : d.kind;
        for (int c = 0; c < kCards; ++c) {
          if (game.place[c] != static_cast<uint8_t>(handOf(them))) continue;
          const Kind k = kindOf(static_cast<uint8_t>(c));
          if (k == ka && a == kNoCard) {
            a = static_cast<uint8_t>(c);
            continue;
          }
          if (k == kb && b == kNoCard && c != a) b = static_cast<uint8_t>(c);
        }
        ok = a != kNoCard && b != kNoCard && game.playDuo(a, b);
        if (ok) {
          ++laid[static_cast<int>(d.kind)];
          if (d.kind == Kind::Swimmer) stole = true;
        }
        break;
      }
      case Decision::Act::EndTurn:
        ok = game.endTurn();
        break;
      case Decision::Act::Stop:
        ok = game.endRound(false);
        called = true;
        break;
      case Decision::Act::LastChance:
        ok = game.endRound(true);
        called = true;
        bet = true;
        break;
      case Decision::Act::DigPile:
        ok = game.chooseCrabPile(d.a);
        break;
      case Decision::Act::DigCard:
        ok = game.takeCrabCard(d.a);
        break;
    }
    if (!ok) {
      LOG_ERR("SEASALT", "the brain offered a move the rules refused (act %d)", static_cast<int>(d.act));
      break;
    }
  }

  // Narrate the headline of what just happened, most important first.
  if (called) {
    std::snprintf(report, sizeof(report),
                  bet ? "THEY BET LAST CHANCE. ONE MORE TURN, MAKE IT COUNT." : "THEY CALLED STOP.");
  } else if (stole) {
    std::snprintf(report, sizeof(report), "SWIMMER AND SHARK. THEY STOLE FROM YOUR HAND.");
  } else if (laid[1] > 0) {
    std::snprintf(report, sizeof(report), "THEY PLAYED BOATS FOR EXTRA TURNS. YOUR MOVE NOW.");
  } else if (laid[0] > 0) {
    std::snprintf(report, sizeof(report), "THEY PLAYED TWO CRABS AND DUG THROUGH A PILE.");
  } else if (laid[2] > 0) {
    std::snprintf(report, sizeof(report), "THEY PLAYED TWO FISH AND DREW A CARD.");
  } else {
    std::snprintf(report, sizeof(report), "THEY TOOK A CARD. YOUR MOVE.");
  }

  const seasalt::Phase phase = game.currentPhase();
  if (phase == seasalt::Phase::RoundOver || phase == seasalt::Phase::GameOver) {
    if (phase == seasalt::Phase::GameOver && !statsCounted) {
      statsCounted = true;
      ++played;
      if (game.matchWinner() == seat) ++won;
      saveStats();
    }
    view = View::RoundOver;
  } else {
    view = viewForStep();
  }
  requestUpdate();
}

void SeaSaltActivity::gameLoop() {
  if (!interactionsReady) return;

  if (opponentPending) {
    opponentPending = false;
    playOpponentTurn();
    return;
  }

  switch (view) {
    case View::Menu:
      routeStartMenu();
      break;
    case View::Board:
      routeBoard();
      break;
    case View::Call:
      routeCall();
      break;
    case View::RoundOver:
      routeRoundOver();
      break;
    case View::Rules:
      routeTutorial();
      break;
  }
}

// --- routing ---------------------------------------------------------------

void SeaSaltActivity::activateStartRow(const seasaltui::StartRow row) {
  switch (row) {
    case seasaltui::StartRow::Continue:
      report[0] = '\0';
      clearSelection();
      view = viewForStep();
      requestUpdate();
      break;
    case seasaltui::StartRow::NewGame:
      startNewGame();
      break;
    case seasaltui::StartRow::PlayNearby:
      enterLink(linkplay::GameId::SeaSalt);
      break;
    case seasaltui::StartRow::HowToPlay:
      tutorialPage = 0;
      view = View::Rules;
      requestUpdate();
      break;
    case seasaltui::StartRow::Count:
      break;
  }
}

void SeaSaltActivity::routeStartMenu() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // See src/apps_local/Shelf.h: no app names its own destination.
    shelf::leave(renderer, mappedInput);
    return;
  }
  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);
  if (event.action == seasaltui::ActionStartRow) {
    menuSelected = event.value;
    activateStartRow(seasaltui::startRowAt(startModel(), event.value));
  }
}

void SeaSaltActivity::handleCardTap(const int tileIndex) {
  using namespace seasalt;
  if (tab != 0 || !mayAct()) return;

  switch (game.currentStep()) {
    case Step::ChooseKeep:
      if (tileIndex <= 1 && game.keepDrawn(tileIndex)) afterHumanAction();
      return;
    case Step::CrabPick: {
      int skip = page * seasaltui::BoardModel::kMaxBoardTiles + tileIndex;
      for (int c = 0; c < kCards; ++c) {
        if (game.place[c] != static_cast<uint8_t>(pileAt(game.crabPile))) continue;
        if (skip-- == 0) {
          if (game.takeCrabCard(static_cast<uint8_t>(c))) afterHumanAction();
          return;
        }
      }
      return;
    }
    case Step::Play:
      break;
    default:
      return;  // ChoosePile wants a pile, not a card
  }

  // Step::Play: selection. The tile index is a position in card-id order on
  // this page; find the card.
  int skip = page * seasaltui::BoardModel::kMaxBoardTiles + tileIndex;
  uint8_t card = kNoCard;
  for (int c = 0; c < kCards; ++c) {
    if (game.place[c] != static_cast<uint8_t>(handOf(seat))) continue;
    if (skip-- == 0) {
      card = static_cast<uint8_t>(c);
      break;
    }
  }
  if (card == kNoCard) return;

  // Tap a selected card to unselect it; a third card replaces the older pick.
  if (sel[0] == card) {
    sel[0] = sel[1];
    sel[1] = kNoCard;
  } else if (sel[1] == card) {
    sel[1] = kNoCard;
  } else if (sel[0] == kNoCard) {
    sel[0] = card;
  } else if (sel[1] == kNoCard) {
    sel[1] = card;
  } else {
    sel[0] = sel[1];
    sel[1] = card;
  }
  requestUpdate();
}

void SeaSaltActivity::routeBoard() {
  using namespace seasalt;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (linkRequested()) {
      leaveLink();
      return;
    }
    goToMenu();
    return;
  }

  // The side keys page the card grid.
  const seasaltui::BoardModel probe = boardModel();
  if (probe.pages > 1) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) && page > 0) {
      --page;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down) && page < probe.pages - 1) {
      ++page;
      requestUpdate();
      return;
    }
  }

  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);
  switch (event.action) {
    case seasaltui::ActionTabHand:
      tab = 0;
      page = 0;
      requestUpdate();
      return;
    case seasaltui::ActionTabYours:
      tab = 1;
      page = 0;
      clearSelection();
      requestUpdate();
      return;
    case seasaltui::ActionTabTheirs:
      tab = 2;
      page = 0;
      clearSelection();
      requestUpdate();
      return;
    case seasaltui::ActionDeck:
      if (mayAct() && game.currentStep() == Step::Take && game.takeFromDeck()) {
        report[0] = '\0';
        afterHumanAction();
      }
      return;
    case seasaltui::ActionPileA:
    case seasaltui::ActionPileB: {
      const int pile = event.action == seasaltui::ActionPileA ? 0 : 1;
      if (!mayAct()) return;
      switch (game.currentStep()) {
        case Step::Take:
          if (game.takeFromPile(pile)) {
            report[0] = '\0';
            afterHumanAction();
          }
          return;
        case Step::ChoosePile:
          if (game.discardTo(pile)) afterHumanAction();
          return;
        case Step::CrabPile:
          if (game.chooseCrabPile(pile)) {
            page = 0;
            requestUpdate();
          }
          return;
        default:
          return;
      }
    }
    case seasaltui::ActionCall:
      if (!mayAct()) return;
      view = View::Call;
      requestUpdate();
      return;
    case seasaltui::ActionPrimary: {
      if (!mayAct()) return;
      const int selected = (sel[0] != kNoCard) + (sel[1] != kNoCard);
      if (selected == 2 && isPair(sel[0], sel[1])) {
        if (game.playDuo(sel[0], sel[1])) afterHumanAction();
        return;
      }
      if (game.endTurn()) afterHumanAction();
      return;
    }
    default:
      break;
  }

  // Not chrome: maybe a card.
  const fui::Rect grid = fui::makeRect(gridSlot.x, gridSlot.y, gridSlot.width, gridSlot.height);
  const int index =
      seasaltui::cardIndexAt(grid, probe.tileCount, static_cast<int16_t>(tapX), static_cast<int16_t>(tapY));
  if (index >= 0) handleCardTap(index);
}

void SeaSaltActivity::routeCall() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = View::Board;  // deciding not to call is allowed
    requestUpdate();
    return;
  }
  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);
  if (event.action != seasaltui::ActionStop && event.action != seasaltui::ActionLastChance) return;
  if (game.endRound(event.action == seasaltui::ActionLastChance)) {
    report[0] = '\0';
    afterHumanAction();
  }
}

void SeaSaltActivity::routeRoundOver() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // The same guard routeBoard() has twenty lines up. Without it Back here
    // walks to the front door with the radio still up and the opponent never
    // told, and the link screen arrives over the menu a moment later.
    if (linkRequested()) {
      leaveLink();
      return;
    }
    goToMenu();
    return;
  }
  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);
  if (event.action == seasaltui::ActionContinue) {
    if (inMatch() && seasalt::linkAction(game, seat, linkYourTurn()) != seasalt::LinkAction::Deal)
      return;  // drawn disabled; THEY DEAL
    // The player after the one who ended the round starts the next one.
    const uint8_t starter = game.ender != seasalt::kNoSeat ? static_cast<uint8_t>(game.ender ^ 1)
                                                           : static_cast<uint8_t>(game.roundStarter ^ 1);
    const uint8_t s0 = game.score[0];
    const uint8_t s1 = game.score[1];
    const uint8_t round = game.round;
    game.deal(nextSeed(), starter);
    game.score[0] = s0;
    game.score[1] = s1;
    game.round = static_cast<uint8_t>(round + 1);
    report[0] = '\0';
    clearSelection();
    tab = 0;
    page = 0;
    if (inMatch()) {
      link.play(game);  // the deal is a state like any other
    } else if (!myTurn()) {
      opponentPending = true;
    }
    view = View::Board;
    requestUpdate();
    return;
  }
  if (event.action == seasaltui::ActionPlayAgain) {
    // In a match this button is drawn live and reachable, and startNewGame()
    // would deal a fresh local game over a link the opponent is still on --
    // they would never see it. A rematch is asked, the same as everywhere else.
    if (inMatch()) {
      proposeRematch();
      return;
    }
    startNewGame();
  }
}

void SeaSaltActivity::routeTutorial() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    view = View::Menu;
    requestUpdate();
    return;
  }
  int tapX = 0, tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  if (interactions.route(input).action != seasaltui::ActionAdvance) return;
  ++tutorialPage;
  if (tutorialPage >= seasaltui::tutorialPages()) {
    tutorialPage = 0;
    view = View::Menu;
  }
  requestUpdate();
}

// --- persistence -----------------------------------------------------------

uint32_t SeaSaltActivity::nextSeed() {
  seed = seed * 1664525u + 1013904223u + static_cast<uint32_t>(millis());
  return seed | 1u;
}

void SeaSaltActivity::refreshContinueDetail() {
  std::snprintf(continueDetail, sizeof(continueDetail), "ROUND %d, %d-%d", game.round, game.score[seat],
                game.score[seat ^ 1]);
}

void SeaSaltActivity::saveGame() const {
  if (linkRequested()) return;
  if (!hasSavedGame || game.currentPhase() == seasalt::Phase::GameOver) {
    if (Storage.exists(kSavePath)) Storage.remove(kSavePath);
    return;
  }
  char buffer[16 + 2 * sizeof(seasalt::Game) + 4] = {};
  int at = std::snprintf(buffer, sizeof(buffer), "%d %d ", kSaveVersion, seat);
  const auto* bytes = reinterpret_cast<const uint8_t*>(&game);
  for (size_t i = 0; i < sizeof(seasalt::Game); ++i) {
    buffer[at++] = hexDigit((bytes[i] >> 4) & 0xF);
    buffer[at++] = hexDigit(bytes[i] & 0xF);
  }
  buffer[at] = '\0';
  Storage.writeFile(kSavePath, String(buffer));
}

bool SeaSaltActivity::loadGame() {
  if (!Storage.exists(kSavePath)) return false;
  char buffer[16 + 2 * sizeof(seasalt::Game) + 4] = {};
  if (Storage.readFileToBuffer(kSavePath, buffer, sizeof(buffer)) == 0) return false;

  int version = 0;
  int savedSeat = 0;
  int consumed = 0;
  if (std::sscanf(buffer, "%d %d %n", &version, &savedSeat, &consumed) < 2) return false;
  if (version != kSaveVersion) {
    LOG_INF("SEASALT", "ignoring a save from another build");
    return false;
  }
  const char* hex = buffer + consumed;
  if (std::strlen(hex) < 2 * sizeof(seasalt::Game)) {
    LOG_ERR("SEASALT", "corrupt save, starting fresh");
    return false;
  }
  seasalt::Game restored;
  auto* bytes = reinterpret_cast<uint8_t*>(&restored);
  for (size_t i = 0; i < sizeof(seasalt::Game); ++i) {
    const int high = hexValue(hex[2 * i]);
    const int low = hexValue(hex[2 * i + 1]);
    if (high < 0 || low < 0) {
      LOG_ERR("SEASALT", "corrupt save, starting fresh");
      return false;
    }
    bytes[i] = static_cast<uint8_t>((high << 4) | low);
  }

  // A save that breaks conservation came from another build. Every card must
  // be in a real place, and the deck must match the deal cursor.
  int inDeck = 0;
  for (int c = 0; c < seasalt::kCards; ++c) {
    if (restored.place[c] >= seasalt::kPlaceCount) {
      LOG_ERR("SEASALT", "save holds an impossible position, starting fresh");
      return false;
    }
    if (restored.place[c] == static_cast<uint8_t>(seasalt::Place::Deck)) ++inDeck;
  }
  if (inDeck != seasalt::kCards - restored.deckNext ||
      restored.phase > static_cast<uint8_t>(seasalt::Phase::GameOver) ||
      restored.step > static_cast<uint8_t>(seasalt::Step::CrabPick)) {
    LOG_ERR("SEASALT", "save holds an impossible position, starting fresh");
    return false;
  }

  game = restored;
  seat = savedSeat & 1;
  return true;
}

void SeaSaltActivity::loadStats() {
  char buffer[32] = {};
  if (Storage.readFileToBuffer(kStatsPath, buffer, sizeof(buffer)) == 0) return;
  int p = 0, w = 0;
  if (std::sscanf(buffer, "%d %d", &p, &w) == 2 && p >= 0 && w >= 0 && w <= p) {
    played = p;
    won = w;
  }
}

void SeaSaltActivity::saveStats() const {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d %d", played, won);
  Storage.writeFile(kStatsPath, String(buffer));
}
