#include "UnderhandActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <PaintClock.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "../Shelf.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"
#include "UnderhandOriginalData.h"
#include "UnderhandView.h"

namespace {

constexpr char kSavePath[] = "/.crosspoint/underhand.sav";
constexpr char kSaveTempPath[] = "/.crosspoint/underhand.sav.tmp";
constexpr char kSaveBadPath[] = "/.crosspoint/underhand.sav.bad";
constexpr char kSaveBad2Path[] = "/.crosspoint/underhand.sav.bad2";

namespace uh = underhand;
namespace ui = underhandui;

// In capitals like every other label in the Jersey face, except the x of
// "3 x Harvest", which reads as a times sign only in lower case.
void capitals(char* text) {
  for (char* c = text; *c; ++c) {
    const bool times =
        *c == 'x' && c - text >= 2 && c[-1] == ' ' && std::isdigit(static_cast<unsigned char>(c[-2])) && c[1] == ' ';
    if (!times) *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
  }
}

// Whether every option that can be taken ends the run: no card is drawn
// after this one, so no punishment is rolled.
bool lastCard(const uh::Cards& cards, const uh::Game& g) {
  const uh::Card* card = cards.card(g.card);
  if (!card || g.phase != uh::Phase::Choosing) return false;
  for (int k = 0; k < card->optionCount; ++k) {
    if (!card->option[k].lose && uh::view::optionState(g, cards, k) == uh::view::OptionState::Open) return false;
  }
  return true;
}

bool notable(const uh::Game& g) {
  if (g.addedCount > 0 || g.reshuffled) return true;
  for (int16_t n : g.lost) {
    if (n > 0) return true;
  }
  return false;
}

// The card screen for a game, the same for play and for the audit. `waysFor`
// lists that option's ways to pay, from page `waysPage`.
void fillCard(const uh::Cards& cards, const uh::Game& g, bool showOutcome, int waysFor, int waysPage,
              ui::CardModel& m) {
  const uh::Card* card = cards.card(showOutcome ? g.played : g.card);
  if (card) {
    std::snprintf(m.title, sizeof(m.title), "%s", cards.text(card->title));
    for (char* c = m.title; *c; ++c) *c = static_cast<char>(std::toupper(static_cast<unsigned char>(*c)));
    m.flavor = uh::view::flavorText(cards, *card);
  }
  m.turn = g.turn;
  m.deck = g.draw.size;
  for (int r = 0; r < uh::kResources; ++r) m.held[r] = g.held[r];
  // Under an outcome the next card is already drawn, so its odds would
  // describe the draw after it: the warning, and the black counts, wait for
  // the card. So do they on a card whose only way out ends the run.
  if (!showOutcome && !lastCard(cards, g)) {
    m.odds = uh::view::chances(g);
    m.rolls = uh::punishmentOdds(g);
  }
  if (!showOutcome) {
    m.lastPaid = uh::view::tokensOf(g.paid);
    m.lastGained = uh::view::tokensOf(g.gained);
  }
  if (!card) return;

  // The options are filled whatever the panel, so the list of ways can read them.
  m.optionCount = card->optionCount;
  for (int k = 0; k < card->optionCount; ++k) {
    ui::OptionRow& row = m.option[k];
    const uh::Option& o = card->option[k];
    row.text = uh::view::optionText(cards, *card, k);
    if (showOutcome) continue;
    row.state = uh::view::optionState(g, cards, k);
    row.get = uh::view::getTokens(g, k);
    row.give = uh::view::giveTokens(g, cards, k);
    // "Only with none" is said in the note of an option it closes; the mark
    // would say it twice.
    if (row.state != uh::view::OptionState::Open) {
      int kept = 0;
      for (int t = 0; t < row.give.count; ++t) {
        if (row.give.token[t].kind != uh::view::Token::OnlyIfNone) row.give.token[kept++] = row.give.token[t];
      }
      row.give.count = static_cast<uint8_t>(kept);
    }
    uh::Counts ways[ui::kChips];
    row.ways = uh::view::choices(g, cards, k, ways, ui::kChips);
    row.guarded = row.ways > 0 && uh::view::buysNothing(g, cards, k);
    for (int i = 0; i < row.ways && i < ui::kChips; ++i) {
      row.way[i] = uh::view::tokensOf(ways[i]);
      // The random part is the same whichever way the rest is paid.
      if (o.randomCost > 0)
        row.way[i].token[row.way[i].count++] = uh::view::Token{uh::view::Token::Random, 0, o.randomCost};
    }
    // What it does, and when it cannot be taken, why not: a summons out of
    // reach is exactly what the player is saving for.
    char effect[112];
    uh::view::effectLine(g, cards, k, effect, sizeof(effect));
    if (row.state == uh::view::OptionState::Open && row.guarded) {
      std::snprintf(row.note, sizeof(row.note), "No suspicion: relics buy nothing");
    } else if (row.state == uh::view::OptionState::Open) {
      std::snprintf(row.note, sizeof(row.note), "%s", effect);
    } else {
      char why[112];
      uh::view::whyNot(g, cards, k, why, sizeof(why));
      std::snprintf(row.note, sizeof(row.note), "%s%s%s", effect, effect[0] ? ". " : "", why);
      // Where the option's words need the room: only why, and without the
      // relics' share.
      char bare[112];
      uh::view::whyNot(g, cards, k, bare, sizeof(bare), false);
      if (effect[0] || std::strcmp(bare, why) != 0) {
        std::snprintf(row.shortNote, sizeof(row.shortNote), "%s", bare);
        capitals(row.shortNote);
      }
    }
    capitals(row.note);
  }

  if (showOutcome) {
    m.panel = ui::Panel::Outcome;
    m.chose = g.playedOption >= 0 ? uh::view::optionText(cards, *card, g.playedOption) : "";
    m.paid = uh::view::tokensOf(g.paid);
    m.gained = uh::view::tokensOf(g.gained);
    m.lost = uh::view::tokensOf(g.lost);
    uh::view::deckSentence(g, cards, m.outcome[0], sizeof(m.outcome[0]));
    if (m.outcome[0][0]) m.outcomeLines = 1;
  } else if (g.phase == uh::Phase::Foresight) {
    m.panel = ui::Panel::Foresight;
    m.seenCount = g.seenCount;
    m.mayDiscard = g.mayDiscard;
    for (int i = 0; i < g.seenCount; ++i) {
      const uh::Card* seen = cards.card(g.seen[i]);
      m.seenTitle[i] = seen ? cards.text(seen->title) : "?";
      m.discard[i] = (g.discardMask & (1 << i)) != 0;
    }
  } else if (waysFor >= 0 && waysFor < card->optionCount) {
    m.panel = ui::Panel::Ways;
    m.waysFor = waysFor;
    m.wayPage = waysPage;
    m.waysGuarded = uh::view::buysNothing(g, cards, waysFor);
    uh::Counts ways[ui::kMostWays];
    const int n = uh::view::choices(g, cards, waysFor, ways, ui::kMostWays);
    m.wayCount = n < ui::kMostWays ? n : ui::kMostWays;
    for (int i = 0; i < m.wayCount; ++i) {
      uh::view::Tokens& t = m.listed[i];
      t = uh::view::tokensOf(ways[i]);
      const uh::Option& o = card->option[waysFor];
      if (o.randomCost > 0) t.token[t.count++] = uh::view::Token{uh::view::Token::Random, 0, o.randomCost};
    }
  }
}

void fillEnd(const uh::Cards& cards, const uh::Game& g, const uh::Profile& profile, ui::EndModel& m) {
  m.won = g.phase == uh::Phase::Won;
  if (m.won) {
    const uh::God& god = cards.god(g.god);
    std::snprintf(m.headline, sizeof(m.headline), "%s", cards.text(god.name));
    capitals(m.headline);
    const int serving = profile.summonedCount();
    std::snprintf(m.detail[0], sizeof(m.detail[0]), "has answered your call on turn %d. %d of %d gods now %s you.",
                  g.turn, serving, cards.godCount(), serving == 1 ? "serves" : "serve");
    const uh::Card* a = cards.card(god.unlock[0]);
    const uh::Card* b = cards.card(god.unlock[1]);
    std::snprintf(m.detail[1], sizeof(m.detail[1]), "Your next run begins with \"%s\" and \"%s\".",
                  a ? cards.text(a->title) : "?", b ? cards.text(b->title) : "?");
    return;
  }
  std::snprintf(m.headline, sizeof(m.headline), "THE CULT FALLS");
  const uh::Card* card = cards.card(g.loss == uh::LossReason::Choice ? g.played : g.card);
  const char* title = card ? cards.text(card->title) : "?";
  switch (g.loss) {
    case uh::LossReason::Choice:
      std::snprintf(m.detail[0], sizeof(m.detail[0]), "%s: %s.", title,
                    card && g.playedOption >= 0 ? uh::view::optionText(cards, *card, g.playedOption) : "");
      break;
    case uh::LossReason::Stuck:
      std::snprintf(m.detail[0], sizeof(m.detail[0]), "Nothing \"%s\" asked for could be paid.", title);
      break;
    default:
      std::snprintf(m.detail[0], sizeof(m.detail[0]), "The deck ran out of cards.");
      break;
  }
  std::snprintf(m.detail[1], sizeof(m.detail[1]), "It lasted %d turns.", g.turn);
}

void fillMenu(const uh::Cards& cards, const uh::Save& state, bool confirm, bool setAside, ui::MenuModel& m) {
  m.inRun = state.inRun;
  m.saveSetAside = setAside && !state.inRun;
  m.confirmGiveUp = confirm && state.inRun;
  m.tutorial = !state.profile.tutorialDone;
  m.turn = state.game.turn;
  m.gods = cards.godCount();
  for (int g = 0; g < m.gods; ++g) {
    m.godName[g] = cards.text(cards.god(g).name);
    m.summoned[g] = (state.profile.summoned & (1 << g)) != 0;
  }
}

}  // namespace

std::unique_ptr<Activity> UnderhandActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<UnderhandActivity>(renderer, mappedInput);
}

void UnderhandActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  cards = makeUniqueNoThrow<uh::Cards>();
  cardModel = makeUniqueNoThrow<ui::CardModel>();
  const char* why = nullptr;
  if (!cards || !cardModel) {
    LOG_ERR("UNDERHAND", "OOM: %d bytes for the cards and %d for the screen", static_cast<int>(sizeof(uh::Cards)),
            static_cast<int>(sizeof(ui::CardModel)));
    view = View::Broken;
  } else if (!uh::CardsReader::read(*cards, uh::CardsReader::File::Gods, uh::kGodsJson, sizeof(uh::kGodsJson) - 1,
                                    &why) ||
             !uh::CardsReader::read(*cards, uh::CardsReader::File::Cards, uh::kCardsJson, sizeof(uh::kCardsJson) - 1,
                                    &why) ||
             !uh::rulesFit(*cards, &why)) {
    LOG_ERR("UNDERHAND", "Cards refused: %s", why ? why : "?");
    view = View::Broken;
  } else {
    LOG_INF("UNDERHAND", "%d cards, %d gods, %d text bytes", cards->count(), cards->godCount(),
            static_cast<int>(cards->textUsed()));
    load();
    // Straight back into a run in progress: the menu is one Back away.
    view = state.inRun ? View::Play : View::Menu;
#ifdef SIMULATOR
    auditPending = std::getenv("UNDERHAND_AUDIT") != nullptr;
#endif
  }
  flashNext = true;
  ready = true;
  requestUpdate();
}

ui::CardModel& UnderhandActivity::freshCard() { return *new (cardModel.get()) ui::CardModel(); }

void UnderhandActivity::load() {
  // Power lost between save()'s remove and its rename leaves only the new
  // file, complete, under its temporary name.
  if (!Storage.exists(kSavePath) && Storage.exists(kSaveTempPath)) {
    LOG_INF("UNDERHAND", "Recovering the save from %s", kSaveTempPath);
    Storage.rename(kSaveTempPath, kSavePath);
  }
  if (!Storage.exists(kSavePath)) return;
  // Whatever length it is: a save from a build with another Game still
  // carries the profile, and decode() keeps it. A card read can fail once
  // and not twice, so a failed read is tried again before anything is given
  // up on.
  uint8_t bytes[uh::kSaveBytes];
  size_t size = 0;
  size_t want = 0;
  auto readSave = [&]() {
    HalFile f = Storage.open(kSavePath, O_RDONLY);
    if (!f.isOpen()) return false;
    size = f.size();
    want = size < sizeof(bytes) ? size : sizeof(bytes);
    return f.read(bytes, want) == static_cast<int>(want);
  };
  const bool read = readSave() || readSave();
  if (!read || !uh::decode(bytes, want, *cards, state)) {
    // Set aside rather than overwritten by the next save, since the gods
    // summoned may still be in it. The first such file is never replaced;
    // a later one takes the second name.
    const char* aside = Storage.exists(kSaveBadPath) ? kSaveBad2Path : kSaveBadPath;
    LOG_ERR("UNDERHAND", "Save not %s (%d bytes); set aside as %s, starting fresh", read ? "valid" : "readable",
            static_cast<int>(size), aside);
    if (Storage.exists(aside)) Storage.remove(aside);
    Storage.rename(kSavePath, aside);
    state = uh::Save{};
    saveSetAside = true;
    return;
  }
  if (size != uh::kSaveBytes)
    LOG_INF("UNDERHAND", "Save from another build (%d bytes): profile kept", static_cast<int>(size));
  rng = uh::Rng(state.rng);
  LOG_INF("UNDERHAND", "Loaded: %s, turn %d, gods 0x%02x", state.inRun ? "in a run" : "no run", state.game.turn,
          state.profile.summoned);
}

// Temp file then rename, so power lost mid-write leaves the old save whole.
void UnderhandActivity::save() {
  state.rng = rng.state();
  uint8_t bytes[uh::kSaveBytes];
  uh::encode(state, bytes);
  HalFile f;
  if (!Storage.openFileForWrite("UNDERHAND", kSaveTempPath, f)) {
    LOG_ERR("UNDERHAND", "Could not write %s", kSaveTempPath);
    return;
  }
  const size_t wrote = f.write(bytes, sizeof(bytes));
  f.close();
  if (wrote != sizeof(bytes)) {
    LOG_ERR("UNDERHAND", "Short save: %d of %d bytes", static_cast<int>(wrote), static_cast<int>(sizeof(bytes)));
    return;
  }
  Storage.remove(kSavePath);
  Storage.rename(kSaveTempPath, kSavePath);
}

void UnderhandActivity::newRun() {
  rng = uh::Rng((static_cast<uint64_t>(millis()) << 20) ^ (rng.state() * 0x9E3779B97F4A7C15ULL) ^ 1);
  uh::start(state.game, *cards, state.profile, rng);
  state.inRun = true;
  state.showOutcome = false;
  saveSetAside = false;
  waysFor = -1;
  confirmGiveUp = false;
  view = View::Play;
  flashNext = true;
  LOG_INF("UNDERHAND", "New run: %s, %d gods summoned, first card %d", state.game.tutorial ? "tutorial" : "normal",
          state.profile.summonedCount(), state.game.card);
  afterChoice();
}

void UnderhandActivity::pay(int option, int way) {
  uh::Game& g = state.game;
  // The same list the screen offered, so a way's number means the same way.
  uh::Counts ways[ui::kWayStride];
  const int n = uh::view::choices(g, *cards, option, ways, ui::kWayStride);
  if (way < 0 || way >= n || way >= ui::kWayStride || !uh::choose(g, *cards, option, ways[way], rng)) {
    LOG_ERR("UNDERHAND", "Card %d option %d way %d refused (%d ways)", g.card, option, way, n);
    return;
  }
  waysFor = -1;
  LOG_DBG("UNDERHAND", "Card %d option %d way %d; next card %d", g.played, option, way, g.card);
  afterChoice();
}

// After anything that moved the game: record an ending, decide whether the
// options step aside for what just happened, and save.
void UnderhandActivity::afterChoice() {
  const uh::Game& g = state.game;
  if (g.phase == uh::Phase::Won || g.phase == uh::Phase::Lost) {
    ended = g;
    uh::finish(state.profile, g);
    state.inRun = false;
    view = View::End;
    flashNext = true;
    LOG_INF("UNDERHAND", "Run over on turn %d: %s", g.turn,
            g.phase == uh::Phase::Won ? cards->text(cards->god(g.god).name) : "lost");
  } else {
    state.showOutcome = g.phase == uh::Phase::Choosing && g.played != 0 && notable(g);
  }
  save();
  requestUpdate();
}

// Called with the render lock held: every change here is to state the render
// task reads. Returns true when the app should be left, which the caller does
// after letting go of the lock.
bool UnderhandActivity::route(int action, int value) {
  switch (view) {
    case View::Menu:
      if (confirmGiveUp) {
        if (action == ui::ActionNewRun) newRun();
        // KEEP PLAYING means the card, not the menu it was asked from.
        if (action == ui::ActionCancel) {
          confirmGiveUp = false;
          view = View::Play;
          flashNext = true;
          requestUpdate();
        }
      } else if (action == ui::ActionMain) {
        if (state.inRun) {
          view = View::Play;
          flashNext = true;
          requestUpdate();
        } else {
          newRun();
        }
      } else if (action == ui::ActionGiveUp) {
        confirmGiveUp = true;
        flashNext = true;
        requestUpdate();
      } else if (action == ui::ActionHelp) {
        helpFrom = View::Menu;
        helpPage = 0;
        view = View::Help;
        flashNext = true;
        requestUpdate();
      }
      return false;
    case View::Play:
      // A tap that pays is for the card it was drawn on; one made on an
      // earlier card, while this one was being painted, does nothing.
      if ((action == ui::ActionOption || action == ui::ActionPay || action == ui::ActionMore) &&
          !ui::stampedFor(value, state.game.turn)) {
        LOG_DBG("UNDERHAND", "Tap for an earlier card ignored (turn %d)", state.game.turn);
        return false;
      }
      if (action == ui::ActionOption) {
        pay(ui::payloadOf(value), 0);
      } else if (action == ui::ActionPay) {
        pay(ui::payloadOf(value) / ui::kWayStride, ui::payloadOf(value) % ui::kWayStride);
      } else if (action == ui::ActionMore) {
        waysFor = ui::payloadOf(value);
        waysPage = 0;
        requestUpdate();
      } else if (action == ui::ActionNextWays) {
        waysPage = value;
        requestUpdate();
      } else if (action == ui::ActionHelp) {
        helpFrom = View::Play;
        helpPage = 0;
        view = View::Help;
        flashNext = true;
        requestUpdate();
      } else if (action == ui::ActionCancel) {
        waysFor = -1;
        requestUpdate();
      } else if (action == ui::ActionContinue) {
        if (state.showOutcome) {
          state.showOutcome = false;
          save();
          requestUpdate();
        } else if (state.game.phase == uh::Phase::Foresight) {
          uh::endForesight(state.game, *cards, rng);
          afterChoice();
          // The choice was shown before foresight; its outcome is not news.
          state.showOutcome = false;
          save();
        }
      } else if (action == ui::ActionSeen) {
        uh::toggleDiscard(state.game, value);
        save();
        requestUpdate();
      }
      return false;
    case View::End:
      if (action == ui::ActionNewRun) {
        newRun();
      } else if (action == ui::ActionMenu) {
        view = View::Menu;
        flashNext = true;
        requestUpdate();
      }
      return false;
    case View::Help:
      if (action == ui::ActionCancel) {
        view = helpFrom;
        flashNext = true;
        requestUpdate();
      } else if (action == ui::ActionHelpPage) {
        helpPage = value;
        requestUpdate();
      }
      return false;
    case View::Broken:
      // Its one button leaves: there is nothing to play.
      return action == ui::ActionMenu;
  }
  return false;
}

// Called with the render lock held, like route(); true means leave the app.
bool UnderhandActivity::back() {
  if (view == View::Play && waysFor >= 0) {
    waysFor = -1;
  } else if (view == View::Menu && confirmGiveUp) {
    confirmGiveUp = false;
    flashNext = true;
  } else if (view == View::Help) {
    view = helpFrom;
    flashNext = true;
  } else if (view == View::Play || view == View::End) {
    view = View::Menu;
    flashNext = true;
  } else {
    return true;
  }
  requestUpdate();
  return false;
}

void UnderhandActivity::loop() {
  bool leave = false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    RenderLock lock;
    leave = back();
  } else {
    freeink::ui::InputSnapshot input;
    int tapX = 0;
    int tapY = 0;
    if (mappedInput.wasScreenTapped(tapX, tapY)) {
      input.touchReleased = true;
      input.touchX = static_cast<int16_t>(tapX);
      input.touchY = static_cast<int16_t>(tapY);
    }
    if (!input.touchReleased || !interactionsReady) return;
    // The table and the game both belong to the render task while it draws,
    // and it holds the lock until the panel has changed. A tap that waited
    // out a paint was made on the screen before it: drop it rather than route
    // it against a card the player has not seen.
    const uint32_t seen = paintclock::painted();
    RenderLock lock;
    if (paintclock::painted() != seen) {
      LOG_DBG("UNDERHAND", "Tap made during a repaint dropped");
      return;
    }
    const freeink::ui::ActionEvent event = interactions.route(input);
    leave = route(static_cast<int>(event.action), static_cast<int>(event.value));
  }
  if (leave) shelf::leave(renderer, mappedInput);
}

// Simulator only (UNDERHAND_AUDIT set): every card through every panel, with a
// full hand and an empty one, every ending and every menu. The screens report
// anything that does not fit; sim-shot fails on an overflow the renderer
// truncates. Nothing is displayed and nothing is saved.
void UnderhandActivity::audit() {
  ui::resetLayoutProblems();
  int screens = 0;
  int reported = 0;
  auto check = [&](const char* what, int id) {
    ++screens;
    if (ui::layoutProblems() > reported) {
      LOG_ERR("UNDERHAND", "AUDIT %s %d: %s", what, id, ui::lastLayoutProblem());
      reported = ui::layoutProblems();
    }
  };
  auto draw = [&](auto build) {
    renderer.clearScreen();
    freeink::ui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::readingChromeFaces());
    const freeink::ui::InputSnapshot noInput{};
    toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
    toybox::Screen screen(frame);
    build(screen);
  };
  // Rich enough for every way to pay and every danger at once; starving adds
  // the third danger; poor closes every option that costs anything; crowded
  // puts two digits in every cell of the bar.
  const uh::Counts rich = {3, 9, 9, 9, 9, 9};
  const uh::Counts starving = {3, 9, 9, 0, 9, 9};
  const uh::Counts poor = {0, 0, 0, 0, 0, 0};
  // Two digits in every cell, the widest counts a long run reaches.
  const uh::Counts crowded = {12, 25, 18, 10, 22, 15};
  // Only relics: every cost paid by them, suspicion included.
  const uh::Counts relics = {9, 0, 0, 0, 0, 0};
  // All three punishments possible at once, none of them certain.
  const uh::Counts threeOdds = {0, 5, 5, 0, 1, 5};
  uh::Rng r(7);
  for (int i = 0; i < cards->count(); ++i) {
    const uh::Card& c = cards->at(i);
    for (const uh::Counts* held : {&rich, &starving, &poor, &crowded, &relics, &threeOdds}) {
      uh::Game g;
      g.held = *held;
      g.card = c.id;
      for (int d = 0; d < 20; ++d) g.draw.push(c.id);
      uh::detail::resolve(g, *cards, r);
      ui::CardModel& m = freshCard();
      fillCard(*cards, g, false, -1, 0, m);
      draw([&](toybox::Screen& s) { ui::buildCard(s, m); });
      check("card", c.id);
      for (int k = 0; k < c.optionCount; ++k) {
        for (int page = 0;; ++page) {
          ui::CardModel& w = freshCard();
          fillCard(*cards, g, false, k, page, w);
          if (w.panel != ui::Panel::Ways || (w.wayCount < 2 && !w.waysGuarded)) break;
          draw([&](toybox::Screen& s) { ui::buildCard(s, w); });
          check("ways on card", c.id);
          if (page + 1 >= ui::lastWaysPages()) break;
        }
      }
    }
    // The last turn along the status line, for every option this card has.
    for (int k = 0; k < c.optionCount; ++k) {
      uh::Game g;
      g.held = {1, 1, 1, 1, 1, 1};
      g.card = c.id;
      uh::detail::resolve(g, *cards, r);
      for (int x = 0; x < uh::kResources; ++x) {
        g.paid[x] = g.cost[k][x] == uh::kOnlyIfNone || g.cost[k][x] < 0 ? 0 : g.cost[k][x];
        g.gained[x] = g.gain[k][x];
      }
      g.played = c.id;
      ui::CardModel& m = freshCard();
      fillCard(*cards, g, false, -1, 0, m);
      draw([&](toybox::Screen& s) { ui::buildCard(s, m); });
      check("last turn of card", c.id);
    }
    // Every option's outcome, with the longest events it can bring.
    for (int k = 0; k < c.optionCount; ++k) {
      uh::Game g;
      g.held = rich;
      g.card = c.id;
      uh::detail::resolve(g, *cards, r);
      g.played = c.id;
      g.playedOption = static_cast<int8_t>(k);
      for (int x = 0; x < uh::kResources; ++x) {
        g.paid[x] = g.cost[k][x] == uh::kOnlyIfNone ? 0 : g.cost[k][x];
        g.gained[x] = g.gain[k][x];
      }
      g.lost = {1, 1, 1, 1, 1, 0};
      const uh::Option& o = c.option[k];
      for (int a = 0; a < o.addCount; ++a) g.added[g.addedCount++] = uh::Game::Added{o.add[a].card, o.add[a].copies};
      for (int j = 0; j < o.rollCount; ++j) g.added[g.addedCount++] = uh::Game::Added{g.rolled[k][j], 1};
      g.reshuffled = true;
      ui::CardModel& m = freshCard();
      fillCard(*cards, g, true, -1, 0, m);
      draw([&](toybox::Screen& s) { ui::buildCard(s, m); });
      check("outcome of card", c.id);
    }
  }
  // Foresight over every card, three at a time.
  for (int i = 0; i + 2 < cards->count(); i += 3) {
    uh::Game g;
    g.card = cards->at(i).id;
    g.phase = uh::Phase::Foresight;
    g.mayDiscard = true;
    g.discardMask = 0b010;
    g.seenCount = 3;
    for (int s = 0; s < 3; ++s) g.seen[s] = cards->at(i + s).id;
    ui::CardModel& m = freshCard();
    fillCard(*cards, g, false, -1, 0, m);
    draw([&](toybox::Screen& s) { ui::buildCard(s, m); });
    check("foresight from card", g.card);
  }
  // Every ending.
  uh::Profile all;
  all.summoned = 0x7f;
  for (int god = 0; god < cards->godCount(); ++god) {
    uh::Game g;
    g.phase = uh::Phase::Won;
    g.god = static_cast<int8_t>(god);
    g.turn = 999;
    ui::EndModel m;
    fillEnd(*cards, g, all, m);
    draw([&](toybox::Screen& s) { ui::buildEnd(s, m); });
    check("win for god", god);
  }
  for (int i = 0; i < cards->count(); ++i) {
    const uh::Card& c = cards->at(i);
    for (int k = 0; k < c.optionCount; ++k) {
      if (!c.option[k].lose) continue;
      uh::Game g;
      g.phase = uh::Phase::Lost;
      g.loss = uh::LossReason::Choice;
      g.played = c.id;
      g.playedOption = static_cast<int8_t>(k);
      g.turn = 999;
      ui::EndModel m;
      fillEnd(*cards, g, all, m);
      draw([&](toybox::Screen& s) { ui::buildEnd(s, m); });
      check("loss on card", c.id);
    }
    uh::Game stuck;
    stuck.phase = uh::Phase::Lost;
    stuck.loss = uh::LossReason::Stuck;
    stuck.card = c.id;
    stuck.turn = 999;
    ui::EndModel m;
    fillEnd(*cards, stuck, all, m);
    draw([&](toybox::Screen& s) { ui::buildEnd(s, m); });
    check("stuck on card", c.id);
  }
  // The menu in each of its states: between runs, in one, asking to give it
  // up, and before the tutorial.
  for (int form = 0; form < 5; ++form) {
    uh::Save s;
    s.inRun = form == 1 || form == 2;
    s.game.turn = 999;
    s.profile.summoned = 0x55;
    s.profile.tutorialDone = form != 3;
    ui::MenuModel m;
    fillMenu(*cards, s, form == 2, form == 4, m);
    draw([&](toybox::Screen& sc) { ui::buildMenu(sc, m); });
    check("menu form", form);
  }
  for (int page = 0; page < ui::kHelpPages; ++page) {
    draw([&](toybox::Screen& sc) { ui::buildHelp(sc, page); });
    check("how to play page", page);
  }
  LOG_INF("UNDERHAND", "AUDIT: %d screens, %d layout problems", screens, ui::layoutProblems());
}

void UnderhandActivity::render(RenderLock&&) {
  // A render requested before onEnter finished has nothing to draw yet.
  if (!ready) return;
  if (auditPending) {
    auditPending = false;
    audit();
  }
  renderer.clearScreen();
  freeink::ui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::readingChromeFaces());
  const freeink::ui::InputSnapshot noInput{};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  switch (view) {
    case View::Menu: {
      ui::MenuModel model;
      fillMenu(*cards, state, confirmGiveUp, saveSetAside, model);
      ui::buildMenu(screen, model);
      break;
    }
    case View::Play: {
      ui::CardModel& model = freshCard();
      fillCard(*cards, state.game, state.showOutcome, waysFor, waysPage, model);
      ui::buildCard(screen, model);
      break;
    }
    case View::End: {
      ui::EndModel model;
      fillEnd(*cards, ended, state.profile, model);
      ui::buildEnd(screen, model);
      break;
    }
    case View::Help:
      ui::buildHelp(screen, helpPage);
      break;
    case View::Broken: {
      ui::EndModel model;
      model.leaveOnly = true;
      std::snprintf(model.headline, sizeof(model.headline), "CARDS MISSING");
      std::snprintf(model.detail[0], sizeof(model.detail[0]), "The card data could not be read. See the log.");
      ui::buildEnd(screen, model);
      break;
    }
  }
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Underhand");
  // Fast refreshes leave a little of every card behind; a run of a few dozen
  // cards gets one full refresh every kFastRefreshes to clear it.
  if (++fastRefreshes >= kFastRefreshes) flashNext = true;
  if (flashNext) fastRefreshes = 0;
  renderer.displayBuffer(flashNext ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashNext = false;
}
