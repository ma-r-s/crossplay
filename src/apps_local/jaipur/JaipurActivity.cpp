#include "JaipurActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../Shelf.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxIcons.h"
#include "../ui/ToyboxSeed.h"
#include "../ui/ToyboxTheme.h"
#include "JaipurArt.h"
#include "JaipurGoods.h"

namespace {

using jaipur::Good;
using jaipur::kBonusStacks;
using jaipur::kCamel;
using jaipur::kEmpty;
using jaipur::kGoodCount;
using jaipur::kMarketSlots;

// The goods, in the order the rulebook lists them, which is also descending
// value. Short enough to survive an 80px card at the tile cut.
constexpr const char* kGoodNames[kGoodCount] = {"DIAMOND", "GOLD", "SILVER", "CLOTH", "SPICE", "LEATHER"};

// The six goods, as marks. The only thing asked of them is that they read at
// 32px and are not mistaken for each other; see tools_local/jaipur/jaipur_goods.txt for
// what each rejection actually read as -- leather alone went through three
// before one stuck.
// One cut per row of the board, because each row leaves the mark a different
// amount of room: a market card carries nothing but the mark, a hand card puts
// it under a count, and a pile card has a price above it and the depth pips
// below.
enum class MarkSize : uint8_t { Pile = 0, Hand, Market };

const freeink::Icon& goodIcon(const int good, const MarkSize size) {
  static const freeink::Icon* kPile[kGoodCount] = {&icon_good_diamond_32, &icon_good_gold_32,  &icon_good_silver_32,
                                                   &icon_good_cloth_32,   &icon_good_spice_32, &icon_good_leather_32};
  static const freeink::Icon* kHand[kGoodCount] = {&icon_good_diamond_44, &icon_good_gold_44,  &icon_good_silver_44,
                                                   &icon_good_cloth_44,   &icon_good_spice_44, &icon_good_leather_44};
  static const freeink::Icon* kMarket[kGoodCount] = {&icon_good_diamond_56, &icon_good_gold_56,  &icon_good_silver_56,
                                                     &icon_good_cloth_56,   &icon_good_spice_56, &icon_good_leather_56};
  if (size == MarkSize::Market) return *kMarket[good];
  return size == MarkSize::Hand ? *kHand[good] : *kPile[good];
}

// Icons store 1 for "leave this pixel" and 0 for "draw it", which is the
// opposite of toybox::blit1bpp's own asset format. Feeding one to the other
// paints the negative of the icon, so this is its own blitter rather than a
// flag on that one.
void blitIcon(const GfxRenderer& renderer, const freeink::Icon& icon, const int x, const int y) {
  const int rowBytes = (icon.w + 7) / 8;
  for (int row = 0; row < icon.h; ++row) {
    for (int col = 0; col < icon.w; ++col) {
      if (((icon.bits[row * rowBytes + (col >> 3)] >> (7 - (col & 7))) & 1) == 0) {
        renderer.drawPixel(x + col, y + row, true);
      }
    }
  }
}

void blitIconCentered(const GfxRenderer& renderer, const freeink::Icon& icon, const Rect& box) {
  blitIcon(renderer, icon, box.x + (box.width - icon.w) / 2, box.y + (box.height - icon.h) / 2);
}

const char* cardName(const uint8_t card) {
  if (card == kCamel) return "CAMEL";
  if (card == kEmpty) return "";
  return kGoodNames[card];
}

int popcount8(uint8_t v) {
  int n = 0;
  while (v) {
    v &= static_cast<uint8_t>(v - 1);
    ++n;
  }
  return n;
}

// Pick the largest cut the text fits in, walking down. Never breaks a word:
// EpdFont is a bitmap format, so "fit this text" can only mean "choose a size".
int fittedFont(const GfxRenderer& renderer, const char* text, const int maxWidth) {
  static constexpr int kCuts[] = {toybox::kUiFontId, toybox::kButtonFontId, toybox::kTileFontId};
  for (const int cut : kCuts) {
    if (renderer.getTextWidth(cut, text) <= maxWidth) return cut;
  }
  return toybox::kTileFontId;
}

// Centres `text` horizontally in `box` at the largest cut that fits, with its
// capital ink vertically centred in [y, y + h).
void drawCentered(const GfxRenderer& renderer, const Rect& box, const int y, const int h, const char* text,
                  const bool black) {
  const int font = fittedFont(renderer, text, box.width);
  const int w = renderer.getTextWidth(font, text);
  toybox::drawCapsCentered(renderer, font, box.x + (box.width - w) / 2, y, h, text, black);
}

// Splits a band into `count` cells with `gap` between them, and hands back cell
// `index`. Both the drawing and the hit-testing call this, which is the whole
// point: a tappable region computed a second time is a bug waiting for a theme
// change.
Rect cellIn(const Rect& band, const int count, const int index, const int gap) {
  const int cell = (band.width - (count - 1) * gap) / count;
  Rect out;
  out.x = band.x + index * (cell + gap);
  out.y = band.y;
  out.width = cell;
  out.height = band.height;
  return out;
}

Rect rowOf(const Rect& body, int& cursor, const int height, const int gapAfter = 0) {
  Rect out;
  out.x = body.x;
  out.y = cursor;
  out.width = body.width;
  out.height = height;
  cursor += height + gapAfter;
  return out;
}

constexpr char kSavePath[] = "/.crosspoint/jaipur.sav";
// Bumped whenever jaipur::Game changes shape *or meaning*, so a save from an
// older build is ignored rather than reinterpreted. Same discipline as the cache
// formats and as LinkPlay's GameId. Version 2: the seal is awarded when the
// round ends rather than when the next one is dealt, so a v1 save sitting on
// Phase::RoundOver is one seal short and there is no way to tell it apart from a
// correct v2 one. Version 3: Game grew a record of the last move.
constexpr int kSaveVersion = 3;

char hexDigit(const int value) { return static_cast<char>(value < 10 ? '0' + value : 'A' + value - 10); }

int hexValue(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// The last move, in words, from `viewer`'s side of the table. Read out of the
// state rather than remembered by whoever made the move: over a link the mover
// is the other device, and the packet is all that arrives. One function, so
// "YOU SOLD 3 SPICE FOR 11" and "THEY SOLD 3 SPICE FOR 11" cannot drift apart.
void describeLastMove(const jaipur::Game& game, const int viewer, char* out, const size_t size) {
  if (!game.hasLastMove()) {
    out[0] = '\0';
    return;
  }
  const char* who = game.lastMover() == viewer ? "YOU" : "THEY";
  const int n = game.lastCount;
  switch (game.lastMoveKind()) {
    case jaipur::Move::Kind::TakeOne:
      std::snprintf(out, size, "%s TOOK A %s", who, cardName(game.lastCard));
      break;
    case jaipur::Move::Kind::TakeCamels:
      std::snprintf(out, size, n == 1 ? "%s TOOK %d CAMEL" : "%s TOOK %d CAMELS", who, n);
      break;
    case jaipur::Move::Kind::Exchange:
      std::snprintf(out, size, "%s TRADED %d CARDS", who, n);
      break;
    case jaipur::Move::Kind::Sell:
      std::snprintf(out, size, "%s SOLD %d %s FOR %d", who, n, kGoodNames[game.lastCard], game.lastSaleValue());
      break;
  }
  // A bonus token is the one thing a sale produces that no other readout can
  // show, because its value stays face down until the round is scored.
  if (game.lastTookBonus()) {
    const size_t used = std::strlen(out);
    std::snprintf(out + used, size - used, " + BONUS");
  }
}

bool hits(const Rect& box, const int x, const int y) {
  return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
}

}  // namespace

std::unique_ptr<Activity> JaipurActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<JaipurActivity>(renderer, mappedInput);
}

// --- lifecycle --------------------------------------------------------------

void JaipurActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  // Mixed from the clock, so two games in a row are not the same game.
  seed = toybox::seed();

  hasSavedGame = loadGame();
  goToMenu();
}

void JaipurActivity::onExit() {
  // Runs on sleep and on an app switch as well as on the way out, which is why
  // this is the door that matters: you press nothing to fall asleep.
  saveGame();
  Activity::onExit();
}

uint32_t JaipurActivity::nextSeed() {
  seed = seed * 1664525u + 1013904223u;
  return seed;
}

bool JaipurActivity::myTurn() const { return game.turn == static_cast<uint8_t>(seat); }

bool JaipurActivity::canAct() const {
  if (game.currentPhase() != jaipur::Phase::Playing) return false;
  if (!inMatch()) return myTurn();
  // In a match both turns have to agree. They always do -- the pass in
  // takeOpponentState() is what keeps them in step -- and this is the belt to
  // that braces: a move made against a transport that will refuse to send it
  // leaves two devices holding different games, and nothing on either screen
  // would say so.
  return jaipur::linkAction(game, seat, linkYourTurn()) == jaipur::LinkAction::Move;
}

// --- the link ---------------------------------------------------------------

const char* JaipurActivity::linkHeadline() const {
  // The link screen is the last thing a match shows: it comes up the instant
  // the game ends and asks for a rematch. So it has to carry the result, or the
  // final scores would be replaced by a question before anyone read them.
  if (game.currentPhase() != jaipur::Phase::GameOver) return "TRADE AGAINST SOMEONE IN THE ROOM";
  char rival[24];
  player::shortName(inMatch() ? opponentName() : nullptr, rival, sizeof(rival));
  if (game.roundWinner() == seat) {
    std::snprintf(headline, sizeof(headline), "YOU WIN %d-%d", game.seals[seat], game.seals[1 - seat]);
  } else if (rival[0] != '\0') {
    std::snprintf(headline, sizeof(headline), "%s WINS %d-%d", rival, game.seals[1 - seat], game.seals[seat]);
  } else {
    std::snprintf(headline, sizeof(headline), "THEY WIN %d-%d", game.seals[1 - seat], game.seals[seat]);
  }
  return headline;
}

void JaipurActivity::onMatchStart(const bool goesFirst) {
  seat = goesFirst ? 0 : 1;
  // Both sides clear the table; only the dealer's deal travels. The follower
  // used to keep the previous game, so a match begun after a finished one
  // opened on the old scoreline until the first packet landed.
  game.newGame(nextSeed(), 0);
  if (goesFirst) link.play(game);
  clearSelection();
  view = View::Board;
  requestUpdate();
}

bool JaipurActivity::takeOpponentState() {
  jaipur::Game incoming;
  if (!link.takeOpponent(incoming)) return false;
  game = incoming;
  clearSelection();
  // What they did, out of the packet. Their hand is hidden, so this line is the
  // only account of their turn a player ever gets.
  describeLastMove(game, seat, report, sizeof(report));
  view = viewForPhase();
  // The rules may have nothing for this device even though the transport just
  // handed it the turn: the loser starts a round, and that is not necessarily
  // the player who did not send. Hand it straight back rather than sitting on
  // it, which is the deadlock this replaces. See JaipurLink.h.
  if (jaipur::linkAction(game, seat, linkYourTurn()) == jaipur::LinkAction::Pass) link.play(game);
  requestUpdate();
  return true;
}

void JaipurActivity::onRematch() {
  // Same rule as the first match, and for the same reason: exactly one device
  // deals, and it is whichever one holds the turn the finished match left
  // behind. Both dealing would be two different games with the same name.
  onMatchStart(linkYourTurn());
}

void JaipurActivity::onLinkEnded() { goToMenu(); }

// --- selection --------------------------------------------------------------

void JaipurActivity::clearSelection() {
  selMarket = 0;
  selCamels = 0;
  selCamelTake = false;
  for (int g = 0; g < kGoodCount; ++g) selHand[g] = 0;
}

bool JaipurActivity::selectionEmpty() const {
  if (selMarket != 0 || selCamels != 0 || selCamelTake) return false;
  for (int g = 0; g < kGoodCount; ++g) {
    if (selHand[g] != 0) return false;
  }
  return true;
}

void JaipurActivity::tapMarket(const int slot) {
  const uint8_t card = game.market[slot];
  if (card == kEmpty) return;

  if (card == kCamel) {
    // Camels are all or nothing and can never be part of a trade, so tapping
    // one means the camel take and clears whatever was being built.
    const bool was = selCamelTake;
    clearSelection();
    selCamelTake = !was;
    return;
  }

  // A goods tap cancels a camel take: the two are different actions.
  if (selCamelTake) {
    clearSelection();
  }
  selMarket ^= static_cast<uint8_t>(1u << slot);
  // Selecting market goods and selling out of your hand are different actions,
  // so picking one drops the other.
  if (selMarket != 0) {
    for (int g = 0; g < kGoodCount; ++g) selHand[g] = 0;
  }
}

void JaipurActivity::tapHand(const int good) {
  if (selCamelTake) clearSelection();
  if (game.hand[seat][good] == 0) return;

  // Tapping the same good again takes one more of it and wraps at the top. One
  // control, one gesture, no stepper.
  //
  // What it wraps at depends on what the selection means. Selling, it is
  // everything you hold. Paying for an exchange, it is what the exchange needs:
  // climbing past that offers a trade the rules can never accept, and the
  // capsule would sit dead while you tapped.
  int most = game.hand[seat][good];
  if (selMarket != 0) {
    const int wanted = popcount8(selMarket);
    if (most > wanted) most = wanted;
  }
  selHand[good] = static_cast<uint8_t>(selHand[good] >= most ? 0 : selHand[good] + 1);

  if (selMarket == 0) {
    // Building a sale: only one goods type may be sold in a turn.
    for (int g = 0; g < kGoodCount; ++g) {
      if (g != good) selHand[g] = 0;
    }
    selCamels = 0;
  }
}

void JaipurActivity::tapHerd() {
  if (selCamelTake) clearSelection();
  // Camels are only ever given, and only in a trade.
  if (selMarket == 0) return;
  // Wraps at what the trade needs, not at the size of the herd: six camels
  // offered against two market cards is not a move, and letting the counter
  // climb there only produces a dead capsule.
  int most = game.herd[seat];
  const int wanted = popcount8(selMarket);
  if (most > wanted) most = wanted;
  selCamels = static_cast<uint8_t>(selCamels >= most ? 0 : selCamels + 1);
}

bool JaipurActivity::selectionMove(jaipur::Move& out) const {
  if (selCamelTake) {
    out = jaipur::Move::takeCamels();
    return game.isLegal(out);
  }

  const int marketPicked = popcount8(selMarket);
  int handPicked = 0;
  int onlyGood = -1;
  for (int g = 0; g < kGoodCount; ++g) {
    if (selHand[g] == 0) continue;
    handPicked += selHand[g];
    onlyGood = onlyGood < 0 ? g : -2;  // -2 marks "more than one type"
  }

  if (marketPicked == 1 && handPicked == 0 && selCamels == 0) {
    int slot = 0;
    while (((selMarket >> slot) & 1) == 0) ++slot;
    out = jaipur::Move::takeOne(slot);
    return game.isLegal(out);
  }

  if (marketPicked >= 1) {
    out = jaipur::Move();
    out.kind = jaipur::Move::Kind::Exchange;
    out.marketMask = selMarket;
    out.giveCamels = selCamels;
    for (int g = 0; g < kGoodCount; ++g) out.give[g] = selHand[g];
    return game.isLegal(out);
  }

  if (onlyGood >= 0) {
    out = jaipur::Move::sell(static_cast<Good>(onlyGood), selHand[onlyGood]);
    return game.isLegal(out);
  }

  return false;
}

void JaipurActivity::capsuleLabel(char* buffer, const size_t size) const {
  // The round has ended and the board is holding the position it ended on. The
  // scores are one tap away rather than already on screen.
  if (game.currentPhase() != jaipur::Phase::Playing) {
    std::snprintf(buffer, size, "SEE SCORES");
    return;
  }

  if (!myTurn()) {
    // Their first word only. A three-word name inside a sentence is what
    // pushed "MOVE" off the end of chess's capsule; the layout wanted a short
    // name, not the whole value.
    char them[24];
    player::shortName(inMatch() ? opponentName() : nullptr, them, sizeof(them));
    if (them[0] != '\0') {
      std::snprintf(buffer, size, "%s IS TRADING", them);
    } else {
      std::snprintf(buffer, size, "THEY ARE TRADING");
    }
    return;
  }

  if (selCamelTake) {
    const int n = game.marketCamels();
    std::snprintf(buffer, size, n == 1 ? "TAKE %d CAMEL" : "TAKE %d CAMELS", n);
    return;
  }

  const int marketPicked = popcount8(selMarket);
  int handPicked = 0;
  int onlyGood = -1;
  for (int g = 0; g < kGoodCount; ++g) {
    if (selHand[g] == 0) continue;
    handPicked += selHand[g];
    onlyGood = onlyGood < 0 ? g : -2;
  }

  if (marketPicked == 1 && handPicked == 0 && selCamels == 0) {
    int slot = 0;
    while (((selMarket >> slot) & 1) == 0) ++slot;
    std::snprintf(buffer, size, "TAKE %s", cardName(game.market[slot]));
    return;
  }

  if (marketPicked >= 1) {
    // Says what it would be, even while it is short, so the capsule teaches the
    // rule rather than going blank: an exchange is at least 2 for 2.
    std::snprintf(buffer, size, "EXCHANGE %d FOR %d", handPicked + selCamels, marketPicked);
    return;
  }

  if (onlyGood >= 0) {
    const Good good = static_cast<Good>(onlyGood);
    // Diamonds, gold and silver never sell alone. The capsule is dimmed either
    // way, but a dimmed control that still quotes a price reads as an offer
    // being refused rather than as a rule.
    if (jaipur::sellsInPairs(good) && selHand[onlyGood] < 2) {
      std::snprintf(buffer, size, "%s SELLS IN PAIRS", kGoodNames[onlyGood]);
      return;
    }
    const int value = game.saleValue(good, selHand[onlyGood]);
    std::snprintf(buffer, size, "SELL %d %s -> %d", selHand[onlyGood], kGoodNames[onlyGood], value);
    return;
  }

  std::snprintf(buffer, size, "YOUR MOVE");
}

void JaipurActivity::commitSelection() {
  jaipur::Move move;
  if (!selectionMove(move) || !game.isLegal(move)) return;

  game.apply(move);
  // Past tense, because the capsule states an intent and this states an
  // outcome. Read back out of the state, which is also where the other device
  // reads it from.
  describeLastMove(game, seat, report, sizeof(report));
  clearSelection();

  // The board stays up when the round ends, holding the position it ended on,
  // and the capsule turns into SEE SCORES. Jumping straight to the scoring grid
  // took the final board away before anyone had looked at it.
  if (inMatch()) link.play(game);
  // Deferred rather than played here, so the repaint showing your own move
  // reaches the panel before the reply is worked out.
  if (opponentIsBrain() && game.currentPhase() == jaipur::Phase::Playing && !myTurn()) opponentPending = true;
  requestUpdate();
}

void JaipurActivity::playOpponentTurn() {
  if (game.currentPhase() != jaipur::Phase::Playing) return;
  const int them = 1 - seat;
  if (game.turn != them) return;

  // The brain is handed an Observation, never the Game, so it cannot see your
  // hand. That is a property of the type, not of this call site.
  const jaipur::Observation obs = jaipur::observe(game, them);
  const jaipur::Move move = jaipur::chooseMove(obs, jaipur::Skill::Maharaja, seed);

  if (!game.apply(move)) {
    // chooseMove only ever returns something legalMoves produced, and that is
    // asserted move-for-move against Game::isLegal in the host tests. Reaching
    // here means those two have drifted, which is worth a log line rather than
    // a silently skipped turn.
    LOG_ERR("JAIPUR", "the opponent chose an illegal move (kind %d)", static_cast<int>(move.kind));
    return;
  }
  describeLastMove(game, seat, report, sizeof(report));

  // What they did goes in the report line and the turn comes straight back. A
  // tap-to-continue beat sat here for one build and was worse to play: it put a
  // press between you and every one of your own turns.
  requestUpdate();
}

// --- layout -----------------------------------------------------------------

JaipurActivity::Layout JaipurActivity::layoutBoard(const Rect& body) const {
  Layout out;
  out.body = body;
  int y = body.y;

  // Everything at once, in reading order down the page: what the game
  // stands at, what is for sale, what it is worth, what you hold.
  const int slack = body.height - (34 + 128 + 96 + 40 + 112 + 44);
  const int gap = std::max(6, slack / 5);

  const Rect score = rowOf(body, y, 34, gap);
  const Rect market = rowOf(body, y, 128, gap);
  const Rect piles = rowOf(body, y, 96, gap);
  const Rect bonus = rowOf(body, y, 40, gap);
  const Rect hand = rowOf(body, y, 112, gap);
  const Rect bottom = rowOf(body, y, 44, 0);

  for (int i = 0; i < kMarketSlots; ++i) out.market[i] = cellIn(market, kMarketSlots, i, 8);
  for (int g = 0; g < kGoodCount; ++g) out.piles[g] = cellIn(piles, kGoodCount, g, 5);
  for (int g = 0; g < kGoodCount; ++g) out.hand[g] = cellIn(hand, kGoodCount, g, 5);
  for (int b = 0; b < kBonusStacks; ++b) {
    Rect band = bonus;
    band.width = (body.width * 3) / 5;
    out.bonus[b] = cellIn(band, kBonusStacks, b, 8);
  }
  out.herd = cellIn(bottom, 2, 0, 12);
  out.theirs = cellIn(bottom, 2, 1, 12);
  out.score = score;

  return out;
}

// --- drawing ----------------------------------------------------------------

void JaipurActivity::drawMarketCard(const Rect& box, const uint8_t card, const bool selected, const bool dim) const {
  if (card == kEmpty) {
    // The deck could not refill this slot, so the round is ending. An outline
    // with nothing in it says so without a word.
    renderer.drawRect(box.x, box.y, box.width, box.height, toybox::kHairline, true);
    return;
  }

  // Knock out first, then stroke: an outline alone leaves the shape hollow and
  // whatever is under it shows through.
  renderer.fillRoundedRect(box.x, box.y, box.width, box.height, 8, White);
  if (dim) renderer.fillRectDither(box.x + 4, box.y + 4, box.width - 8, box.height - 8, LightGray);
  renderer.drawRect(box.x, box.y, box.width, box.height, selected ? toybox::kFrame : toybox::kHairline, true);

  // Nothing but the mark, centred, at the largest cut. The price the good
  // fetches used to sit under it, and it was the same number the pile card
  // below already carries: one figure printed twice, and it cost the market
  // card the room to breathe. A market card answers "what is for sale", the
  // pile row answers "what does it pay".
  const bool camel = card == kCamel;
  const bool bigMark = box.height >= 120;
  const freeink::Icon& mark =
      camel ? (bigMark ? icon_camel_56 : icon_camel_32) : goodIcon(card, bigMark ? MarkSize::Market : MarkSize::Pile);
  blitIconCentered(renderer, mark, box);
}

void JaipurActivity::drawHandCounter(const Rect& box, const int good, const int held, const int picked) const {
  const bool empty = held == 0;
  renderer.fillRoundedRect(box.x, box.y, box.width, box.height, 8, White);
  if (empty) renderer.fillRectDither(box.x + 3, box.y + 3, box.width - 6, box.height - 6, LightGray);
  renderer.drawRect(box.x, box.y, box.width, box.height, picked > 0 ? toybox::kFrame : toybox::kHairline, true);

  Rect inner = box;
  inner.x += 4;
  inner.width -= 8;

  char count[12];
  if (picked > 0) {
    std::snprintf(count, sizeof(count), "%d/%d", picked, held);
  } else {
    std::snprintf(count, sizeof(count), "%d", held);
  }
  const int countBand = 38;
  drawCentered(renderer, inner, box.y + 6, countBand, count, true);
  // Centred in what the count leaves, rather than hung off the bottom edge.
  const freeink::Icon& mark = goodIcon(good, MarkSize::Hand);
  const int zoneTop = box.y + 6 + countBand;
  blitIcon(renderer, mark, box.x + (box.width - mark.w) / 2, zoneTop + (box.y + box.height - zoneTop - mark.h) / 2);
}

void JaipurActivity::drawPile(const Rect& box, const int good) const {
  const int depth = game.goodsDepth[good];
  const int left = jaipur::kPileDepth[good] - depth;

  renderer.fillRoundedRect(box.x, box.y, box.width, box.height, 6, White);
  // An exhausted pile is one of the three that ends the round, so it reads as
  // spent rather than merely empty.
  if (left == 0) renderer.fillRectDither(box.x + 3, box.y + 3, box.width - 6, box.height - 6, DarkGray);
  renderer.drawRect(box.x, box.y, box.width, box.height, toybox::kHairline, true);

  Rect inner = box;
  inner.x += 3;
  inner.width -= 6;

  const int valueBand = 30;
  char value[8];
  std::snprintf(value, sizeof(value), "%d", game.nextTokenValue(static_cast<Good>(good), depth));
  drawCentered(renderer, inner, box.y + 4, valueBand, left > 0 ? value : "-", true);

  // What is still under the top token, as pips. Decoration made of the app's
  // own material: the row is different on every device because the game is.
  // Leather is nine deep and the card is 70px wide, so a fixed pip size does not
  // fit: at 6px on a 3px pitch that row was 78px and ran out over its
  // neighbours. Sized from the box, capped so the five-deep piles do not turn
  // into blobs.
  const int totalPips = jaipur::kPileDepth[good];
  const int pipGap = 2;
  const int cell = (box.width - 8) / totalPips;
  int pip = cell - pipGap;
  if (pip > 6) pip = 6;
  if (pip < 2) pip = 2;
  const int pipY = box.y + box.height - pip - 7;

  // Three bands down the card: what the next token pays, the mark, and the
  // stack. The mark is centred in the gap the other two leave rather than
  // anchored to either, so it belongs to neither and reads as its own band.
  const freeink::Icon& mark = goodIcon(good, MarkSize::Pile);
  const int zoneTop = box.y + 4 + valueBand;
  blitIcon(renderer, mark, box.x + (box.width - mark.w) / 2, zoneTop + (pipY - zoneTop - mark.h) / 2);

  const int span = totalPips * pip + (totalPips - 1) * pipGap;
  const int startX = box.x + (box.width - span) / 2;
  for (int i = 0; i < totalPips; ++i) {
    const int px = startX + i * (pip + pipGap);
    // Spent tokens hollow out; what is still on the pile stays solid, so the
    // row reads as a stack being eaten from the top.
    if (i < depth) {
      renderer.drawRect(px, pipY, pip, pip, toybox::kHairline, true);
    } else {
      renderer.fillRect(px, pipY, pip, pip, true);
    }
  }
}

void JaipurActivity::drawBonusStack(const Rect& box, const int stack) const {
  const int left = jaipur::kBonusDepth[stack] - game.bonusDepth[stack];
  renderer.fillRoundedRect(box.x, box.y, box.width, box.height, 6, White);
  renderer.drawRect(box.x, box.y, box.width, box.height, toybox::kHairline, true);

  char label[24];
  std::snprintf(label, sizeof(label), "%d+ x%d", stack + 3, left);
  Rect inner = box;
  inner.x += 4;
  inner.width -= 8;
  drawCentered(renderer, inner, box.y, box.height, label, true);
}

void JaipurActivity::drawHerdBox(const Rect& box, const int camels, const int picked, const bool tappable) const {
  renderer.fillRoundedRect(box.x, box.y, box.width, box.height, 6, White);
  if (!tappable) renderer.fillRectDither(box.x + 3, box.y + 3, box.width - 6, box.height - 6, LightGray);
  renderer.drawRect(box.x, box.y, box.width, box.height, picked > 0 ? toybox::kFrame : toybox::kHairline, true);

  char label[32];
  if (picked > 0) {
    std::snprintf(label, sizeof(label), "YOUR CAMELS %d/%d", picked, camels);
  } else {
    std::snprintf(label, sizeof(label), "YOUR %d %s", camels, camels == 1 ? "CAMEL" : "CAMELS");
  }
  Rect inner = box;
  inner.x += 6;
  inner.width -= 12;
  drawCentered(renderer, inner, box.y, box.height, label, true);
}

void JaipurActivity::drawTheirSide(const Rect& box) const {
  renderer.drawRect(box.x, box.y, box.width, box.height, toybox::kHairline, true);
  const int them = 1 - seat;
  char label[48];
  // Their hand is a number and never a composition. Mario chose to show the
  // camel count; the box only leaves it optional because the cards are
  // physically there to count.
  const int theirCamels = game.herd[them];
  std::snprintf(label, sizeof(label), "THEM %d CARDS  %d %s", game.handSize(them), theirCamels,
                theirCamels == 1 ? "CAMEL" : "CAMELS");
  Rect inner = box;
  inner.x += 6;
  inner.width -= 12;
  drawCentered(renderer, inner, box.y, box.height, label, true);
}

void JaipurActivity::drawScoreStrip(const Rect& box) const {
  const int them = 1 - seat;
  // Both columns are the same quantity read from the same side of the table:
  // everything you can honestly know about that seat. Your own score is exact,
  // because you drew your bonus tokens and you may look at them. Theirs is what
  // is face up plus a count of what is not, because a bonus token's value is
  // printed on the back. Camels are face up for both, so the 5 rupee token
  // counts wherever the herds say it sits.
  char line[80];
  const int yours = game.visibleScore(seat, seat);
  const int theirs = game.visibleScore(seat, them);
  const int theirBonusCount = game.bonusTokenCount(them);
  if (theirBonusCount > 0) {
    std::snprintf(line, sizeof(line), "SEALS %d-%d   YOU %d   THEM %d+%d?   DECK %d", game.seals[seat],
                  game.seals[them], yours, theirs, theirBonusCount, game.deckRemaining());
  } else {
    std::snprintf(line, sizeof(line), "SEALS %d-%d   YOU %d   THEM %d   DECK %d", game.seals[seat], game.seals[them],
                  yours, theirs, game.deckRemaining());
  }
  const int font = fittedFont(renderer, line, box.width);
  toybox::drawCapsCentered(renderer, font, box.x, box.y, box.height, line, true);
  renderer.fillRect(box.x, box.y + box.height - 3, box.width, toybox::kRule, true);
}

void JaipurActivity::drawBoardSurface(const Layout& layout) {
  drawScoreStrip(layout.score);

  const bool yours = myTurn() && game.currentPhase() == jaipur::Phase::Playing;
  const bool handFull = game.handSize(seat) >= jaipur::kHandLimit;

  for (int i = 0; i < kMarketSlots; ++i) {
    const uint8_t card = game.market[i];
    const bool selected = card == kCamel ? selCamelTake : (((selMarket >> i) & 1) != 0);
    // A control that cannot act dims rather than disappears, and the dither
    // goes in the fill: there is no grey text on this device.
    const bool dim = !yours || (card != kCamel && handFull && selMarket == 0 && !selCamelTake);
    drawMarketCard(layout.market[i], card, selected, dim);
  }

  for (int g = 0; g < kGoodCount; ++g) drawPile(layout.piles[g], g);
  for (int b = 0; b < kBonusStacks; ++b) drawBonusStack(layout.bonus[b], b);

  for (int g = 0; g < kGoodCount; ++g) {
    drawHandCounter(layout.hand[g], g, game.hand[seat][g], selHand[g]);
  }

  drawHerdBox(layout.herd, game.herd[seat], selCamels, yours && selMarket != 0);
  drawTheirSide(layout.theirs);
}

void JaipurActivity::drawRoundSurface(const Rect& body) const {
  const int them = 1 - seat;
  char rival[24];
  player::shortName(inMatch() ? opponentName() : nullptr, rival, sizeof(rival));
  const bool named = rival[0] != '\0';
  if (!named) std::snprintf(rival, sizeof(rival), "THEM");

  const int winner = game.roundWinner();
  const bool youWon = winner == seat;
  char verdict[64];
  // A label built by dropping a name into a sentence has to survive not having
  // one: "THEM TAKES THE SEAL" was the first cut. The nameless case gets its own
  // verb rather than a pronoun that will not conjugate.
  if (game.currentPhase() == jaipur::Phase::GameOver) {
    if (youWon) {
      std::snprintf(verdict, sizeof(verdict), "YOU WIN THE GAME");
    } else if (named) {
      std::snprintf(verdict, sizeof(verdict), "%s WINS THE GAME", rival);
    } else {
      std::snprintf(verdict, sizeof(verdict), "THEY WIN THE GAME");
    }
  } else if (winner < 0) {
    std::snprintf(verdict, sizeof(verdict), "A DEAD HEAT, NO SEAL");
  } else if (youWon) {
    std::snprintf(verdict, sizeof(verdict), "YOU TAKE THE SEAL");
  } else if (named) {
    std::snprintf(verdict, sizeof(verdict), "%s TAKES THE SEAL", rival);
  } else {
    std::snprintf(verdict, sizeof(verdict), "THEY TAKE THE SEAL");
  }

  // One row per thing that paid, the mark on the left and the two columns
  // beside it. The icon says which good without a word, so the label column
  // disappears and the whole round fits in half the height the ledger needed.
  const int camelSeat = game.camelTokenSeat();
  struct Row {
    const freeink::Icon* icon;
    int mine;
    int theirs;
  };
  Row rows[kGoodCount + 2];
  int rowCount = 0;
  for (int g = 0; g < kGoodCount; ++g) {
    rows[rowCount++] = {&goodIcon(g, MarkSize::Pile), game.goodsRupees(seat, static_cast<Good>(g)),
                        game.goodsRupees(them, static_cast<Good>(g))};
  }
  rows[rowCount++] = {&icon_bonus_token_32, game.bonusRupees(seat), game.bonusRupees(them)};
  rows[rowCount++] = {&icon_camel_32, camelSeat == seat ? jaipur::kCamelTokenValue : 0,
                      camelSeat == them ? jaipur::kCamelTokenValue : 0};

  const int markCol = 76;
  const int col = (body.width - markCol) / 2;
  const int colCx[2] = {body.x + markCol + col / 2, body.x + markCol + col + col / 2};

  int y = body.y;

  // The two column heads, and the only place either player is named.
  {
    Rect mineBox{body.x + markCol, y, col, 30};
    Rect theirsBox{body.x + markCol + col, y, col, 30};
    drawCentered(renderer, mineBox, y, 30, "YOU", true);
    drawCentered(renderer, theirsBox, y, 30, rival, true);
    y += 34;
    renderer.fillRect(body.x, y, body.width, toybox::kRule, true);
    y += toybox::kRule + 4;
  }

  const int rowH = 44;
  for (int i = 0; i < rowCount; ++i) {
    Rect markBox{body.x, y, markCol, rowH};
    blitIconCentered(renderer, *rows[i].icon, markBox);

    char mine[12];
    char rest[12];
    std::snprintf(mine, sizeof(mine), "%d", rows[i].mine);
    std::snprintf(rest, sizeof(rest), "%d", rows[i].theirs);
    Rect mineBox{body.x + markCol, y, col, rowH};
    Rect theirsBox{body.x + markCol + col, y, col, rowH};
    drawCentered(renderer, mineBox, y, rowH, mine, true);
    drawCentered(renderer, theirsBox, y, rowH, rest, true);

    // A hairline between rows, so eight numbers read as a table rather than as
    // a column of loose digits. Light, because it repaints with the screen.
    if (i + 1 < rowCount) {
      renderer.fillRect(body.x, y + rowH - 1, body.width, toybox::kHairline, true);
    }
    y += rowH;
  }

  y += 4;
  renderer.fillRect(body.x, y, body.width, toybox::kRule, true);
  y += toybox::kRule + 10;

  {
    char mine[12];
    char rest[12];
    std::snprintf(mine, sizeof(mine), "%d", game.score(seat));
    std::snprintf(rest, sizeof(rest), "%d", game.score(them));
    const int mw = renderer.getTextWidth(toybox::kDisplayFontId, mine);
    const int tw = renderer.getTextWidth(toybox::kDisplayFontId, rest);
    toybox::drawCapsCentered(renderer, toybox::kTileFontId, body.x, y, 60, "RUPEES", true);
    toybox::drawCapsCentered(renderer, toybox::kDisplayFontId, colCx[0] - mw / 2, y, 60, mine, true);
    toybox::drawCapsCentered(renderer, toybox::kDisplayFontId, colCx[1] - tw / 2, y, 60, rest, true);
    y += 70;
  }

  // The verdict, solid, because it is drawn once and the round is over: this is
  // the one thing on the screen that can afford the black a play surface cannot.
  const int bannerH = 56;
  renderer.fillRoundedRect(body.x, y, body.width, bannerH, toybox::kPillRadius, Black);
  const int font = fittedFont(renderer, verdict, body.width - 24);
  const int vw = renderer.getTextWidth(font, verdict);
  toybox::drawCapsCentered(renderer, font, body.x + (body.width - vw) / 2, y, bannerH, verdict, false);
  y += bannerH + 14;

  // Seals: two slots a side, filled as they are won, under their own column.
  const int pip = 22;
  const int span = jaipur::kSealsToWin * pip + (jaipur::kSealsToWin - 1) * 8;
  const int seats[2] = {seat, them};
  for (int r = 0; r < 2; ++r) {
    for (int i = 0; i < jaipur::kSealsToWin; ++i) {
      const int px = colCx[r] - span / 2 + i * (pip + 8);
      if (i < game.seals[seats[r]]) {
        renderer.fillRoundedRect(px, y, pip, pip, pip / 2, Black);
      } else {
        renderer.drawRect(px, y, pip, pip, toybox::kRule, true);
      }
    }
  }
}

// The menu's table: every good, what the market is offering of it, and what it
// pays right now.
//
// Those two facts exist on the board but never together -- the market row shows
// what is for sale and the pile row shows what it fetches, so answering "is
// that diamond worth taking" means reading across the screen. Here they are one
// line. It is also the only place the price list survives when there is no game
// running, which is why it is drawn whether or not there is one to continue.
// The price list: what every good pays, best first, all of it.
//
// Static on purpose. It is not the state of a game, it is the game's own
// economy, and it is the one thing a player cannot work out by looking at the
// board -- the pile row there shows the next token and how many are left, never
// the ladder. Read down a row and you know what a good is worth; read across
// the rows and you know the shape of the whole game: diamond is short and rich,
// leather is long and cheap, silver is flat.
//
// The numbers come out of kGoodsTokens rather than being typed again, so the
// table cannot disagree with the rules it is quoting.
void JaipurActivity::drawPriceList(const Rect& slot) const {
  const int headH = 24;
  const int rowH = std::min(52, (slot.height - headH) / kGoodCount);
  const int total = headH + kGoodCount * rowH;
  const int top = slot.y + (slot.height - total) / 2;

  const int markW = 44;
  // Every row is laid out on the same grid, whatever its depth, so the length
  // of a row IS how many of that good can ever be sold. That comparison is the
  // reason the table is worth the space.
  const int deepest = jaipur::kPileDepth[static_cast<int>(Good::Leather)];
  const int cell = (slot.width - markW) / deepest;

  toybox::drawCapsCentered(renderer, toybox::kTileFontId, slot.x, top, headH, "WHAT EACH GOOD PAYS, BEST FIRST", true);
  renderer.fillRect(slot.x, top + headH, slot.width, toybox::kRule, true);

  for (int g = 0; g < kGoodCount; ++g) {
    const int y = top + headH + g * rowH;
    const freeink::Icon& mark = goodIcon(g, MarkSize::Pile);
    blitIcon(renderer, mark, slot.x + (markW - mark.w) / 2, y + (rowH - mark.h) / 2);

    for (int i = 0; i < jaipur::kPileDepth[g]; ++i) {
      char value[8];
      std::snprintf(value, sizeof(value), "%d", jaipur::kGoodsTokens[g][i]);
      const Rect at{slot.x + markW + i * cell, y, cell, rowH};
      drawCentered(renderer, at, y, rowH, value, true);
    }

    if (g + 1 < kGoodCount) {
      renderer.fillRectDither(slot.x, y + rowH - toybox::kHairline, slot.width, toybox::kHairline, DarkGray);
    }
  }
}

void JaipurActivity::drawMarketStrip(const Rect& slot) const {
  // The market of the game you would be going back to. Ornament has to be made
  // of the app's own material and carry the app's own data; a picture of a
  // camel would be wallpaper by the third day.
  if (!hasSavedGame) return;
  Rect band = slot;
  band.height = std::min<int>(slot.height, 110);
  band.y = slot.y + (slot.height - band.height) / 2;
  for (int i = 0; i < kMarketSlots; ++i) {
    drawMarketCard(cellIn(band, kMarketSlots, i, 8), game.market[i], false, true);
  }
}

void JaipurActivity::drawLinkArt(const Rect& slot) {
  if (game.currentPhase() == jaipur::Phase::GameOver) {
    drawResultArt(slot);
    return;
  }
  drawMarketStrip(slot);
}

void JaipurActivity::drawResultArt(const Rect& slot) const {
  // Two columns, the same two the scoring screen uses, so the match ends on a
  // picture the player has already learned to read. The seals are the match;
  // the rupees are the round that settled it, and they are labelled as such.
  const int them = 1 - seat;
  const int col = slot.width / 2;
  const int colCx[2] = {slot.x + col / 2, slot.x + col + col / 2};

  char rival[24];
  player::shortName(inMatch() ? opponentName() : nullptr, rival, sizeof(rival));
  if (rival[0] == '\0') std::snprintf(rival, sizeof(rival), "THEM");
  const char* heads[2] = {"YOU", rival};

  int y = slot.y + 4;
  for (int c = 0; c < 2; ++c) {
    const int w = renderer.getTextWidth(toybox::kTileFontId, heads[c]);
    toybox::drawCapsCentered(renderer, toybox::kTileFontId, colCx[c] - w / 2, y, 24, heads[c], true);
  }
  y += 30;

  // The seals, as the round screen draws them: filled for won, outlined for the
  // ones nobody reached.
  const int pip = 20;
  const int span = jaipur::kSealsToWin * pip + (jaipur::kSealsToWin - 1) * 8;
  const int seats[2] = {seat, them};
  for (int c = 0; c < 2; ++c) {
    for (int i = 0; i < jaipur::kSealsToWin; ++i) {
      const int px = colCx[c] - span / 2 + i * (pip + 8);
      if (i < game.seals[seats[c]]) {
        renderer.fillRoundedRect(px, y, pip, pip, pip / 2, Black);
      } else {
        renderer.drawRect(px, y, pip, pip, toybox::kRule, true);
      }
    }
  }
  y += pip + 16;

  if (y + 40 > slot.y + slot.height) return;
  const int w = renderer.getTextWidth(toybox::kTileFontId, "LAST ROUND");
  toybox::drawCapsCentered(renderer, toybox::kTileFontId, slot.x + (slot.width - w) / 2, y, 22, "LAST ROUND", true);
  y += 26;
  for (int c = 0; c < 2; ++c) {
    char total[12];
    std::snprintf(total, sizeof(total), "%d", game.score(seats[c]));
    const int tw = renderer.getTextWidth(toybox::kDisplayFontId, total);
    toybox::drawCapsCentered(renderer, toybox::kDisplayFontId, colCx[c] - tw / 2, y, 44, total, true);
  }
}

// --- the game you left ------------------------------------------------------

void JaipurActivity::saveGame() const {
  // Never during a match. The board on screen is then the shared game, and this
  // file is the single-player slot -- the one onLinkEnded() comes back to.
  if (linkRequested()) return;

  // Nothing worth returning to: a finished match reopens on a fresh one.
  if (!hasSavedGame || game.currentPhase() == jaipur::Phase::GameOver) {
    if (Storage.exists(kSavePath)) Storage.remove(kSavePath);
    return;
  }

  char buffer[16 + 2 * sizeof(jaipur::Game) + 4] = {};
  int at = std::snprintf(buffer, sizeof(buffer), "%d %d ", kSaveVersion, seat);
  const auto* bytes = reinterpret_cast<const uint8_t*>(&game);
  for (size_t i = 0; i < sizeof(jaipur::Game); ++i) {
    buffer[at++] = hexDigit((bytes[i] >> 4) & 0xF);
    buffer[at++] = hexDigit(bytes[i] & 0xF);
  }
  buffer[at] = '\0';
  Storage.writeFile(kSavePath, String(buffer));
}

bool JaipurActivity::loadGame() {
  if (!Storage.exists(kSavePath)) return false;
  char buffer[16 + 2 * sizeof(jaipur::Game) + 4] = {};
  if (Storage.readFileToBuffer(kSavePath, buffer, sizeof(buffer)) == 0) return false;

  int version = 0;
  int savedSeat = 0;
  int consumed = 0;
  if (std::sscanf(buffer, "%d %d %n", &version, &savedSeat, &consumed) < 2) return false;
  if (version != kSaveVersion) {
    LOG_INF("JAIPUR", "ignoring a save from another build");
    return false;
  }
  const char* hex = buffer + consumed;
  if (std::strlen(hex) < 2 * sizeof(jaipur::Game)) {
    LOG_ERR("JAIPUR", "corrupt save, starting fresh");
    return false;
  }

  jaipur::Game restored;
  auto* bytes = reinterpret_cast<uint8_t*>(&restored);
  for (size_t i = 0; i < sizeof(jaipur::Game); ++i) {
    const int high = hexValue(hex[2 * i]);
    const int low = hexValue(hex[2 * i + 1]);
    if (high < 0 || low < 0) {
      LOG_ERR("JAIPUR", "corrupt save, starting fresh");
      return false;
    }
    bytes[i] = static_cast<uint8_t>((high << 4) | low);
  }

  // A save that does not obey the rules came from a different build, and
  // adopting it would put an impossible board on screen rather than fail. Every
  // card has to still be somewhere.
  int cards =
      restored.handSize(0) + restored.handSize(1) + restored.herd[0] + restored.herd[1] + restored.deckRemaining();
  for (int i = 0; i < kMarketSlots; ++i) {
    if (restored.market[i] != kEmpty) ++cards;
  }
  for (int g = 0; g < kGoodCount; ++g) cards += restored.sold[g];
  if (cards != 55 || restored.handSize(0) > jaipur::kHandLimit || restored.handSize(1) > jaipur::kHandLimit) {
    LOG_ERR("JAIPUR", "save holds an impossible position, starting fresh");
    return false;
  }

  game = restored;
  seat = savedSeat & 1;
  return true;
}

void JaipurActivity::refreshContinueDetail() {
  std::snprintf(continueDetail, sizeof(continueDetail), "ROUND %d", game.round);
}

// --- models -----------------------------------------------------------------

jaipurui::StartModel JaipurActivity::startModel() const {
  jaipurui::StartModel model;
  model.hasSavedGame = hasSavedGame;
  model.continueDetail = continueDetail;
  model.selected = menuSelected;
  return model;
}

jaipurui::BoardModel JaipurActivity::boardModel() {
  capsuleLabel(capsule, sizeof(capsule));
  jaipurui::BoardModel model;
  model.status = capsule;
  model.report = report;
  jaipur::Move move;
  model.canCommit = canAct() && selectionMove(move);
  model.canClear = !selectionEmpty();
  model.roundOver = game.currentPhase() != jaipur::Phase::Playing;
  model.gameOver = game.currentPhase() == jaipur::Phase::GameOver;
  model.theirName = inMatch() ? link.opponentName() : nullptr;
  return model;
}

jaipurui::RoundModel JaipurActivity::roundModel() const {
  const int them = 1 - seat;
  jaipurui::RoundModel model;
  model.round = game.round;
  model.yourScore = game.score(seat);
  model.theirScore = game.score(them);
  model.yourGoods = game.goodsRupees(seat);
  model.theirGoods = game.goodsRupees(them);
  model.yourBonus = game.bonusRupees(seat);
  model.theirBonus = game.bonusRupees(them);
  model.yourBonusCount = game.bonusTokenCount(seat);
  model.theirBonusCount = game.bonusTokenCount(them);
  model.yourCamels = game.herd[seat];
  model.theirCamels = game.herd[them];
  model.camelTokenSeat = game.camelTokenSeat();
  model.yourSeals = game.seals[seat];
  model.theirSeals = game.seals[them];
  const int winner = game.roundWinner();
  model.youWonRound = winner == seat;
  model.drawnRound = winner < 0;
  model.matchOver = game.currentPhase() == jaipur::Phase::GameOver;
  // Both devices show the scores; only the one holding the turn deals.
  model.waitingOnThem =
      inMatch() && !model.matchOver && jaipur::linkAction(game, seat, linkYourTurn()) != jaipur::LinkAction::Deal;
  model.theirName = inMatch() ? link.opponentName() : nullptr;
  player::shortName(model.theirName, rivalShort, sizeof(rivalShort));
  model.theirShortName = rivalShort;
  return model;
}

// --- painting ---------------------------------------------------------------

void JaipurActivity::drawStartMenu() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = jaipurui::buildStartMenu(screen, startModel());
  drawPriceList(Rect{slot.x, slot.y, slot.width, slot.height});
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Jaipur menu");
}

void JaipurActivity::drawBoard() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = jaipurui::buildBoardChrome(screen, boardModel());
  bodySlot = Rect{slot.x, slot.y, slot.width, slot.height};
  const Layout laid = layoutBoard(bodySlot);
#ifdef JAIPUR_TRACE_LAYOUT
  for (int i = 0; i < kMarketSlots; ++i) {
    LOG_INF("JAIPUR", "market[%d] = %d,%d %dx%d", i, laid.market[i].x, laid.market[i].y, laid.market[i].width,
            laid.market[i].height);
  }
  for (int g = 0; g < kGoodCount; ++g) {
    LOG_INF("JAIPUR", "hand[%d] = %d,%d %dx%d", g, laid.hand[g].x, laid.hand[g].y, laid.hand[g].width,
            laid.hand[g].height);
  }
  LOG_INF("JAIPUR", "herd = %d,%d %dx%d", laid.herd.x, laid.herd.y, laid.herd.width, laid.herd.height);
#endif
  drawBoardSurface(laid);
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Jaipur board");
}

void JaipurActivity::drawRoundOver() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = jaipurui::buildRoundOver(screen, roundModel());
  bodySlot = Rect{slot.x, slot.y, slot.width, slot.height};
  drawRoundSurface(bodySlot);
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Jaipur round");
}

void JaipurActivity::drawTutorial() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  jaipurui::TutorialModel model;
  model.page = tutorialPage;
  jaipurui::buildTutorial(screen, model);
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Jaipur rules");
}

void JaipurActivity::gameRender() {
  switch (view) {
    case View::Menu:
      drawStartMenu();
      break;
    case View::Board:
      drawBoard();
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

// --- routing ----------------------------------------------------------------

JaipurActivity::View JaipurActivity::viewForPhase() const {
  return game.currentPhase() == jaipur::Phase::Playing ? View::Board : View::RoundOver;
}

void JaipurActivity::goToMenu() {
  refreshContinueDetail();
  saveGame();
  view = View::Menu;
  clearSelection();
  menuSelected = 0;
  requestUpdate();
}

void JaipurActivity::startNewGame() {
  seat = 0;
  game.newGame(nextSeed(), 0);
  clearSelection();
  hasSavedGame = true;
  report[0] = '\0';

  view = View::Board;
  if (opponentIsBrain() && !myTurn()) opponentPending = true;
  requestUpdate();
}

void JaipurActivity::activateStartRow(const jaipurui::StartRow row) {
  switch (row) {
    case jaipurui::StartRow::Continue:
      // Not always the board: a game left on the scores comes back to them.
      view = viewForPhase();
      // The saved position may be waiting on the opponent, and nothing else
      // would ever nudge them.
      if (opponentIsBrain() && game.currentPhase() == jaipur::Phase::Playing && !myTurn()) opponentPending = true;
      requestUpdate();
      break;
    case jaipurui::StartRow::NewGame:
      startNewGame();
      break;
    case jaipurui::StartRow::PlayNearby:
      enterLink(linkplay::GameId::Jaipur);
      break;
    case jaipurui::StartRow::HowToPlay:
      view = View::Rules;
      tutorialPage = 0;
      requestUpdate();
      break;
    default:
      break;
  }
}

void JaipurActivity::routeStartMenu() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // See src/apps_local/Shelf.h: no app names its own destination.
    shelf::leave(renderer, mappedInput);
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  if (!interactionsReady) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionEvent event = interactions.route(input);
  if (event.action == jaipurui::ActionStartRow) {
    menuSelected = event.value;
    activateStartRow(jaipurui::startRowAt(startModel(), event.value));
    requestUpdate();
  }
}

void JaipurActivity::routeBoard() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (linkRequested()) {
      // Tells them and shuts the radio down. saveGame() refuses during a match
      // on its own, so there is nothing to remember here.
      leaveLink();
      return;
    }
    // A selection half built is not worth keeping, but the game is: goToMenu()
    // writes it.
    goToMenu();
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;

  // The chrome's own controls first: the capsule is in the hit table and the
  // play surface is not.
  if (interactionsReady) {
    fui::InputSnapshot input;
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
    const fui::ActionEvent event = interactions.route(input);
    if (event.action == jaipurui::ActionCommit) {
      commitSelection();
      return;
    }
    if (event.action == jaipurui::ActionScores) {
      view = View::RoundOver;
      requestUpdate();
      return;
    }
  }

  // Nothing on the board is tappable once the round is over: the only thing to
  // do is the capsule.
  if (game.currentPhase() != jaipur::Phase::Playing) return;

  if (!canAct()) return;

  // Laid out against exactly the rect the last paint drew into, never a second
  // computation of the same geometry.
  const Layout layout = layoutBoard(bodySlot);
  for (int i = 0; i < kMarketSlots; ++i) {
    if (hits(layout.market[i], tapX, tapY)) {
      tapMarket(i);
      requestUpdate();
      return;
    }
  }
  for (int g = 0; g < kGoodCount; ++g) {
    if (hits(layout.hand[g], tapX, tapY)) {
      tapHand(g);
      requestUpdate();
      return;
    }
  }
  if (hits(layout.herd, tapX, tapY)) {
    tapHerd();
    requestUpdate();
    return;
  }
  // A tap on nothing clears, which is the cheapest undo there is.
  if (!selectionEmpty()) {
    clearSelection();
    requestUpdate();
  }
}

void JaipurActivity::routeRoundOver() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (linkRequested()) {
      leaveLink();
      return;
    }
    goToMenu();
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  if (!interactionsReady) return;

  fui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  const fui::ActionId action = interactions.route(input).action;
  if (action == jaipurui::ActionContinue) {
    // The hit table is from the last paint, and their packet can land between
    // that paint and this tap. Asked again here, against the state as it is now.
    if (inMatch() && jaipur::linkAction(game, seat, linkYourTurn()) != jaipur::LinkAction::Deal) return;
    game.startNextRound(nextSeed());
    clearSelection();
    view = game.currentPhase() == jaipur::Phase::GameOver ? View::RoundOver : View::Board;
    if (inMatch()) link.play(game);
    if (opponentIsBrain() && game.currentPhase() == jaipur::Phase::Playing && !myTurn()) opponentPending = true;
    requestUpdate();
  } else if (action == jaipurui::ActionPlayAgain) {
    // It used to be true that both devices were already looking at the link
    // screen by the time this could be tapped, so the match case was excluded
    // and the button was never drawn. The finished board now stays up for a
    // couple of seconds first, and the button is drawn live on it -- a tap that
    // did nothing would read exactly like a crash. Asking is the same question
    // the link screen is about to ask, only sooner.
    if (inMatch()) {
      proposeRematch();
      return;
    }
    startNewGame();
  }
}

void JaipurActivity::routeTutorial() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    goToMenu();
    return;
  }

  int tapX = 0;
  int tapY = 0;
  if (!mappedInput.wasScreenTapped(tapX, tapY)) return;
  if (!interactionsReady) return;

  freeink::ui::InputSnapshot input;
  input.touchReleased = true;
  input.touchX = static_cast<int16_t>(tapX);
  input.touchY = static_cast<int16_t>(tapY);
  if (interactions.route(input).action != jaipurui::ActionAdvance) return;

  // Anywhere on the page turns it, and the last page turns back to the menu.
  if (tutorialPage + 1 < jaipurui::tutorialPages()) {
    ++tutorialPage;
    requestUpdate();
    return;
  }
  goToMenu();
}

void JaipurActivity::gameLoop() {
  if (!interactionsReady) return;

  // One pass later than the move that caused it, which is what puts your own
  // move on the panel first.
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
    case View::RoundOver:
      routeRoundOver();
      break;
    case View::Rules:
      routeTutorial();
      break;
  }
}
