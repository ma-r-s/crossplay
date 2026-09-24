// Underhand's rules, checked on a host.
//
// The rule tests build small card files in the original game's own JSON shape
// and script every random number, so each expectation below is worked out by
// hand from the rule, not read back from the code.
//
// Then bots play the real cards, embedded in UnderhandOriginalData.h, and
// check the invariants over thousands of runs. With UNDERHAND_DATA set to a
// folder holding the APK's cardwip.json and savedatafiletemplate.json, the
// embedded copies are also compared with them.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "UnderhandCards.h"
#include "UnderhandEngine.h"
#include "UnderhandOriginalData.h"
#include "UnderhandSave.h"
#include "UnderhandView.h"

using namespace underhand;

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                 \
    }                                                             \
  } while (0)

// Hands out a fixed list of numbers and records every bound it was asked for.
struct Script final : Random {
  std::vector<int> values;
  std::vector<int> asked;
  size_t next = 0;
  explicit Script(std::vector<int> v = {}) : values(std::move(v)) {}
  int below(int n) override {
    asked.push_back(n);
    if (next >= values.size()) {
      std::printf("FAIL script ran out: below(%d) was call %zu\n", n, asked.size());
      ++failures;
      return 0;
    }
    const int v = values[next++];
    if (v < 0 || v >= n) {
      std::printf("FAIL script value %d is outside below(%d)\n", v, n);
      ++failures;
    }
    return v;
  }
  bool used() const { return next == values.size(); }
};

// ---- fixtures in the original's JSON shape --------------------------------

struct Opt {
  std::string text = "Go";
  std::string outcome;
  std::vector<int> cost = std::vector<int>(6, 0);  // relic money cultist food prisoner suspicion
  std::vector<int> gain = std::vector<int>(6, 0);
  int swap = 0;
  int randomCost = 0;
  int see = 0;
  int discard = 0;
  std::vector<int> ids;
  std::vector<int> copies;  // one per id; written as a list
  int lo = 999;
  int hi = 999;
  int count = 0;  // random shuffle count; written as a scalar
  int dupes = 0;
  std::string win;
  int lose = 0;
};

struct CardSpec {
  CardSpec(int id, int weight = 10, int initial = 0, int recurring = 0)
      : id(id), weight(weight), initial(initial), recurring(recurring) {}
  int id;
  int weight;
  int initial;
  int recurring;
  std::vector<Opt> opts = {Opt{}};
  std::string title;
};

const char* const kNames[] = {"relic", "money", "cultist", "food", "prisoner", "suspicion"};

std::string counts(const std::vector<int>& c) {
  std::string s = "{";
  for (int i = 0; i < 6; ++i) {
    s += std::string(i ? ", " : "") + "\"" + kNames[i] + "\": " + std::to_string(c[i]);
  }
  return s + "}";
}

std::string optionJson(const Opt& o) {
  std::string ids = "[";
  std::string copies = "[";
  for (size_t i = 0; i < o.ids.size(); ++i) {
    ids += (i ? ", " : "") + std::to_string(o.ids[i]);
    copies += (i ? ", " : "") + std::to_string(o.copies[i]);
  }
  ids += "]";
  copies += "]";
  const std::string numcards = o.ids.empty() ? std::to_string(o.count) : copies;
  return "{\"optiontext\": \"" + o.text + "\", \"outputtext\": \"" + o.outcome +
         "\", \"cultistequalsprisoner\": " + std::to_string(o.swap) +
         ", \"foresight\": {\"hasforesight\": " + std::to_string(o.see) +
         ", \"candiscard\": " + std::to_string(o.discard) + "}, \"shuffle\": {\"lowerbound\": " + std::to_string(o.lo) +
         ", \"upperbound\": " + std::to_string(o.hi) + ", \"specificids\": " + ids +
         ", \"allowsdupes\": " + std::to_string(o.dupes) + ", \"numcards\": " + numcards +
         "}, \"randomrequirements\": " + std::to_string(o.randomCost) + ", \"requirements\": " + counts(o.cost) +
         ", \"rewards\": " + counts(o.gain) + ", \"iswin\": \"" + o.win + "\", \"islose\": " + std::to_string(o.lose) +
         "}";
}

std::string cardsJson(const std::vector<CardSpec>& cards) {
  std::string s = "{\n";
  for (size_t i = 0; i < cards.size(); ++i) {
    const CardSpec& c = cards[i];
    const std::string title = c.title.empty() ? "Card " + std::to_string(c.id) : c.title;
    s += (i ? ",\n" : "") + std::string("\"") + std::to_string(c.id) + "\": {\"title\": \"" + title +
         "\", \"flavortext\": \"flavor\"";
    for (int k = 0; k < 3; ++k) {
      Opt blank;
      blank.text = "";
      s += ", \"option" + std::to_string(k + 1) +
           "\": " + optionJson(k < static_cast<int>(c.opts.size()) ? c.opts[k] : blank);
    }
    s += ", \"isinitial\": " + std::to_string(c.initial) + ", \"isrecurring\": " + std::to_string(c.recurring) +
         ", \"animationframes\": 1, \"weight\": " + std::to_string(c.weight) + ", \"cardartdone\": 1}";
  }
  return s + "\n}\n";
}

const char* const kGodNames[] = {"Alpha", "Beta", "Gamma", "Delta", "Epsilon", "Zeta", "Eta"};

// Seven gods; god g unlocks cards 200+2g and 201+2g.
std::string godsJson() {
  std::string names = "{";
  std::string unlocks = "{";
  for (int g = 0; g < 7; ++g) {
    names += std::string(g ? ", " : "") + "\"" + std::to_string(g) + "\": \"" + kGodNames[g] + "\"";
    unlocks += std::string(g ? ", " : "") + "\"" + kGodNames[g] + "\": [" + std::to_string(200 + 2 * g) + ", " +
               std::to_string(201 + 2 * g) + "]";
  }
  return "{\"tutorial\": 0, \"num_gods\": 7, \"previous_summon\": \"\", \"names\": " + names +
         "}, \"unlocked_cards\": " + unlocks + "}, \"settings\": {\"master_volume\": 3}}";
}

// Every card the rules name, plus the gods' unlocks, each with one free
// option, so that start() and rulesFit() have what they need.
std::vector<CardSpec> world(std::vector<CardSpec> extra) {
  std::set<int> have;
  for (const CardSpec& c : extra) have.insert(c.id);
  const int named[] = {2, 27, 29, 56, 57, 71, 79, 85, 106};
  for (int id : named) {
    if (!have.count(id)) extra.push_back(CardSpec{id});
  }
  for (int id = 200; id < 214; ++id) {
    if (!have.count(id)) extra.push_back(CardSpec{id});
  }
  return extra;
}

std::unique_ptr<Cards> load(const std::vector<CardSpec>& specs) {
  auto cards = std::make_unique<Cards>();
  const std::string gods = godsJson();
  const std::string json = cardsJson(specs);
  const char* why = nullptr;
  if (!CardsReader::read(*cards, CardsReader::File::Gods, gods.data(), gods.size(), &why)) {
    std::printf("FAIL gods fixture: %s\n", why);
    ++failures;
  }
  if (!CardsReader::read(*cards, CardsReader::File::Cards, json.data(), json.size(), &why)) {
    std::printf("FAIL cards fixture: %s\n", why);
    ++failures;
  }
  return cards;
}

Counts C(int relic, int money, int cultist, int food, int prisoner, int suspicion) {
  return Counts{static_cast<int16_t>(relic), static_cast<int16_t>(money),    static_cast<int16_t>(cultist),
                static_cast<int16_t>(food),  static_cast<int16_t>(prisoner), static_cast<int16_t>(suspicion)};
}

std::vector<int> pile(const Pile& p) { return std::vector<int>(p.card, p.card + p.size); }

// A game with `id` on the table and nothing else, costs resolved.
// A Why as text: its words, then each symbol as a count and a letter
// ("MISSING 1M 1F", "ONLY WITH NO C", "MISSING 1C/P").
std::string said(const view::Why& why) {
  std::string out = why.words;
  const char* const letters = "RMCFPS";
  for (int i = 0; i < why.tokens.count; ++i) {
    const view::Token& t = why.tokens.token[i];
    out += ' ';
    if (t.kind != view::Token::Symbol) out += std::to_string(t.amount);
    out += t.kind == view::Token::Either ? std::string("C/P") : std::string(1, letters[t.resource]);
  }
  return out;
}

Game table(const Cards& cards, int id, Counts held, std::vector<int> draw = {}) {
  Game g;
  g.held = held;
  for (int d : draw) g.draw.push(static_cast<uint8_t>(d));
  g.card = static_cast<uint8_t>(id);
  Script none;
  detail::resolve(g, cards, none);
  return g;
}

// Takes option k, paid the way suggest() proposes.
bool take(Game& g, const Cards& cards, int k, Random& random) {
  Counts offer{};
  suggest(g, cards, k, offer);
  return choose(g, cards, k, offer, random);
}

// ---- the tests ------------------------------------------------------------

void loadsTheOriginalShape() {
  // Raw tabs inside strings, a doubled key, a list and a scalar numcards, a
  // blank third option: all as the shipped file has them.
  const std::string json =
      "{\"1\": {\"title\": \"\t\tA  Title \", \"flavortext\": \"Some   flavor\","
      " \"option1\": {\"optiontext\": \"\t\t\t\tBuy Supplies\", \"outputtext\": \"\", \"outputtext\": \"\","
      " \"cultistequalsprisoner\": 1, \"foresight\": {\"hasforesight\": 1, \"candiscard\": 1},"
      " \"shuffle\": {\"lowerbound\": 999, \"upperbound\": 999, \"specificids\": [2], \"allowsdupes\": 0,"
      " \"numcards\": [3]}, \"randomrequirements\": 0,"
      " \"requirements\": {\"relic\": 0, \"money\": 2, \"cultist\": 0, \"food\": 0, \"prisoner\": 0, \"suspicion\": "
      "420},"
      " \"rewards\": {\"relic\": 0, \"money\": 0, \"cultist\": 0, \"prisoner\": 0, \"food\": 1, \"suspicion\": 0},"
      " \"iswin\": \"\", \"islose\": 0},"
      " \"option2\": {\"optiontext\": \"Roll\", \"outputtext\": \"Insert 'Two'\", \"cultistequalsprisoner\": 0,"
      " \"foresight\": {\"hasforesight\": 0, \"candiscard\": 0},"
      " \"shuffle\": {\"lowerbound\": 2, \"upperbound\": 3, \"specificids\": [], \"allowsdupes\": 0, \"numcards\": 1},"
      " \"randomrequirements\": 5, \"requirements\": {\"relic\": 0, \"money\": 0, \"cultist\": 840, \"food\": 0,"
      " \"prisoner\": 0, \"suspicion\": 0}, \"rewards\": {\"relic\": 1, \"money\": 0, \"cultist\": 0, \"food\": 0,"
      " \"prisoner\": 0, \"suspicion\": 0}, \"iswin\": \"Beta\", \"islose\": 0},"
      " \"option3\": {\"optiontext\": \"\", \"outputtext\": \"\"},"
      " \"isinitial\": 1, \"isrecurring\": 0, \"animationframes\": 1, \"weight\": 50, \"cardartdone\": 1},"
      " \"2\": {\"title\": \"Two\", \"flavortext\": \"\", \"option1\": {\"optiontext\": \"Ok\"}, \"weight\": 10},"
      " \"3\": {\"title\": \"Three\", \"flavortext\": \"\", \"option1\": {\"optiontext\": \"Ok\", \"islose\": 1},"
      " \"weight\": 10, \"isrecurring\": 1}}";
  Cards cards;
  const std::string gods =
      "{\"tutorial\": 0, \"num_gods\": 2, \"previous_summon\": \"\", \"names\": {\"0\": \"Alpha\", \"1\": \"Beta\"},"
      " \"progress\": {\"Alpha\": 0, \"Beta\": 0}, \"unlocked_cards\": {\"Alpha\": [2, 3], \"Beta\": [3, 2]},"
      " \"settings\": {\"master_volume\": 3}}";
  const char* why = nullptr;
  CHECK(CardsReader::read(cards, CardsReader::File::Gods, gods.data(), gods.size(), &why));
  CHECK(cards.godCount() == 2);
  CHECK(std::strcmp(cards.text(cards.god(1).name), "Beta") == 0);
  CHECK(cards.god(1).unlock[0] == 3 && cards.god(1).unlock[1] == 2);

  // Fed in small chunks, as the device reads it.
  CardsReader reader(cards, CardsReader::File::Cards);
  for (size_t at = 0; at < json.size(); at += 7) reader.feed(json.data() + at, std::min<size_t>(7, json.size() - at));
  CHECK(reader.finish());
  if (reader.error()) std::printf("  reader: %s\n", reader.error());

  CHECK(cards.count() == 3);
  const Card* c = cards.card(1);
  CHECK(c != nullptr);
  if (!c) return;
  CHECK(std::strcmp(cards.text(c->title), "A Title") == 0);
  CHECK(std::strcmp(cards.text(c->flavor), "Some flavor") == 0);
  CHECK(c->weight == 50 && c->initial && !c->recurring);
  CHECK(c->optionCount == 2);
  const Option& buy = c->option[0];
  CHECK(std::strcmp(cards.text(buy.text), "Buy Supplies") == 0);
  CHECK(buy.swap && buy.foresight && buy.foresightDiscard);
  CHECK(buy.addCount == 1 && buy.add[0].card == 2 && buy.add[0].copies == 3);
  CHECK(buy.cost == C(0, 2, 0, 0, 0, kHalf));
  CHECK(buy.gain == C(0, 0, 0, 1, 0, 0));  // keys arrive in a different order here
  CHECK(buy.rollCount == 0 && buy.win == -1 && !buy.lose);
  const Option& roll = c->option[1];
  CHECK(std::strcmp(cards.text(roll.outcome), "Insert 'Two'") == 0);
  CHECK(roll.rollCount == 1 && roll.rollLow == 2 && roll.rollHigh == 3 && !roll.rollRepeats);
  CHECK(roll.randomCost == 5 && roll.cost[Cultist] == kOnlyIfNone && roll.gain[Relic] == 1);
  CHECK(roll.win == 1);
  CHECK(cards.card(3)->recurring && cards.card(3)->option[0].lose);
}

void refusesWhatItCannotPlay() {
  const char* why = nullptr;
  auto reject = [&](std::vector<CardSpec> specs, const char* expect) {
    Cards cards;
    const std::string gods = godsJson();
    const std::string json = cardsJson(world(specs));
    CardsReader::read(cards, CardsReader::File::Gods, gods.data(), gods.size());
    const bool ok = CardsReader::read(cards, CardsReader::File::Cards, json.data(), json.size(), &why);
    CHECK(!ok);
    if (!ok && std::strstr(why, expect) == nullptr) {
      std::printf("FAIL expected an error about \"%s\", got \"%s\"\n", expect, why);
      ++failures;
    }
  };
  CardSpec weightless{1};
  weightless.weight = 0;
  reject({weightless}, "weight");

  CardSpec ghost{1};
  ghost.opts[0].ids = {9};
  ghost.opts[0].copies = {1};
  reject({ghost}, "shuffles in a card that does not exist");

  CardSpec stranger{1};
  stranger.opts[0].win = "Nobody";
  reject({stranger}, "summons a god missing");

  CardSpec tight{1};
  tight.opts[0].lo = 1;
  tight.opts[0].hi = 1;
  tight.opts[0].count = 2;
  reject({tight}, "more cards than its range holds");

  // The same fixtures are loadable once the defect is taken out.
  Cards fine;
  const std::string gods = godsJson();
  const std::string json = cardsJson(world({CardSpec{1}}));
  CardsReader::read(fine, CardsReader::File::Gods, gods.data(), gods.size());
  CHECK(CardsReader::read(fine, CardsReader::File::Cards, json.data(), json.size(), &why));

  CardSpec unlocked{1};
  Cards orphan;
  const std::string bare = cardsJson({unlocked});
  CardsReader::read(orphan, CardsReader::File::Gods, gods.data(), gods.size());
  CHECK(!CardsReader::read(orphan, CardsReader::File::Cards, bare.data(), bare.size(), &why));
  CHECK(why && std::strstr(why, "god unlocks a card that does not exist"));

  Cards cards;
  const std::string bad = "{\"1\": {\"title\": \"x\", ";
  CHECK(!CardsReader::read(cards, CardsReader::File::Cards, bad.data(), bad.size(), &why));
}

void reshuffleIsWeightedAndPicksToTheBottom() {
  auto cards = load(world({{1, 10}, {2, 50}, {3, 10}, {4, 10}}));

  // Pool [1 2 3]. The shuffle swaps nothing; the weighted picks land on 2
  // (15 falls past 1's 10 into 2's 50), then 1, then 3, each placed under the
  // last, so 2 is drawn first.
  Game g;
  for (int id : {1, 2, 3}) g.discard.push(static_cast<uint8_t>(id));
  Script s({0, 0, 15, 5, 0});
  detail::reshuffle(g, *cards, s);
  CHECK((s.asked == std::vector<int>{3, 2, 70, 20, 10}));
  CHECK((pile(g.draw) == std::vector<int>{3, 1, 2}));
  CHECK(g.discard.size == 0 && g.reshuffled);

  // What is left in the draw pile goes in ahead of the discard pile.
  Game h;
  h.draw.push(4);
  h.discard.push(1);
  Script t({1, 10, 3});  // swap to [1 4]; 10 is not past 1's weight, so 4
  detail::reshuffle(h, *cards, t);
  CHECK((pile(h.draw) == std::vector<int>{1, 4}));
  CHECK(t.used());

  // One weight-50 card against one weight-10: drawn first 50/60 of the time.
  Rng rng(7);
  int first = 0;
  const int runs = 200000;
  for (int i = 0; i < runs; ++i) {
    Game k;
    k.discard.push(1);
    k.discard.push(2);
    detail::reshuffle(k, *cards, rng);
    if (k.draw.top() == 2) ++first;
  }
  const double share = static_cast<double>(first) / runs;
  CHECK(share > 0.828 && share < 0.839);
}

void punishmentOddsAreWhatIsRolled() {
  Game g;
  g.card = 1;
  CHECK(punishmentOdds(g).greed == 0 && punishmentOdds(g).police == 0 && punishmentOdds(g).desperate == 20);
  g.held = C(0, 5, 5, 1, 0, 5);  // 16 held, suspicion 5
  Odds o = punishmentOdds(g);
  CHECK(o.greed == 35 && o.police == 35 && o.desperate == 0);
  g.held = C(0, 5, 5, 1, 0, 4);  // 15 held, suspicion 4: neither
  o = punishmentOdds(g);
  CHECK(o.greed == 0 && o.police == 0);
  g.held = C(0, 9, 9, 1, 0, 9);  // 28 held, suspicion 9
  o = punishmentOdds(g);
  CHECK(o.greed == 100 && o.police == 95);
  g.held[Suspicion] = 10;
  CHECK(punishmentOdds(g).police == 100);
  // Nothing is rolled in the tutorial or straight after a punishment.
  g.tutorial = true;
  o = punishmentOdds(g);
  CHECK(o.greed == 0 && o.police == 0 && o.desperate == 0);
  g.tutorial = false;
  for (uint8_t id : {ids::kGreed, ids::kPoliceRaid, ids::kDesperate}) {
    g.card = id;
    o = punishmentOdds(g);
    CHECK(o.greed == 0 && o.police == 0 && o.desperate == 0);
  }
  // What the screen says: each roll happens only if the ones before missed.
  g.card = 1;
  g.held = C(0, 5, 5, 0, 1, 5);  // 16 held, suspicion 5, no food
  o = view::chances(g);
  CHECK(o.greed == 35 && o.police == 23 && o.desperate == 8);  // 65% x 35%, 65% x 65% x 20%
  g.held = C(0, 9, 9, 0, 9, 9);                                // Greed certain
  o = view::chances(g);
  CHECK(o.greed == 100 && o.police == 0 && o.desperate == 0);
}

void punishmentsFollowTheOriginalOdds() {
  auto cards = load(world({{1}}));
  auto run = [&](Counts held, std::vector<int> script, int card = 1, bool tutorial = false) {
    Game g;
    g.held = held;
    g.card = static_cast<uint8_t>(card);
    g.tutorial = tutorial;
    Script s(script);
    detail::punish(g, s);
    CHECK(s.used());
    return std::make_pair(g.draw.top(), s.asked);
  };
  // Fifteen resources, some food, no suspicion: nothing is even rolled.
  CHECK(run(C(0, 14, 0, 1, 0, 0), {}).second.empty());
  // Sixteen: Greed at 15*16-205 = 35%.
  CHECK(run(C(0, 15, 0, 1, 0, 0), {34}).first == ids::kGreed);
  CHECK(run(C(0, 15, 0, 1, 0, 0), {35}).first == 0);
  // Twenty-one or more: certain.
  CHECK(run(C(0, 20, 0, 1, 0, 0), {99}).first == ids::kGreed);
  // Suspicion five: Police Raid at 15*5-40 = 35%; ten: certain.
  CHECK(run(C(0, 0, 0, 1, 0, 5), {34}).first == ids::kPoliceRaid);
  CHECK(run(C(0, 0, 0, 1, 0, 5), {35}).first == 0);
  CHECK(run(C(0, 0, 0, 1, 0, 10), {99}).first == ids::kPoliceRaid);
  // No food: Desperate Measures at 20%.
  CHECK(run(C(0, 1, 0, 0, 0, 0), {19}).first == ids::kDesperate);
  CHECK(run(C(0, 1, 0, 0, 0, 0), {20}).first == 0);
  // Greed is checked first and stops the rest: one roll, not three.
  CHECK((run(C(0, 11, 0, 0, 0, 5), {0}).second == std::vector<int>{100}));
  // A failed Greed roll falls through to the raid and then the famine.
  CHECK((run(C(0, 11, 0, 0, 0, 5), {99, 99, 0}).second == std::vector<int>{100, 100, 100}));

  // nextCard skips them right after a punishment and during the tutorial.
  Game after;
  after.held = C(0, 30, 0, 0, 0, 10);
  after.card = ids::kGreed;
  after.draw.push(1);
  Script quiet;
  detail::nextCard(after, *cards, quiet);
  CHECK(quiet.asked.empty() && after.card == 1);
  Game tut = after;
  tut.card = 1;
  tut.tutorial = true;
  tut.draw.push(1);
  detail::nextCard(tut, *cards, quiet);
  CHECK(quiet.asked.empty());
}

void drawnCardsResolveHalvesAndRolls() {
  CardSpec c{1};
  c.opts[0].cost = {0, 0, kOnlyIfNone, 0, 0, kHalf};
  c.opts[0].gain = {0, 0, 0, kHalf, 0, 0};
  c.opts[0].lo = 5;
  c.opts[0].hi = 6;
  c.opts[0].count = 2;
  auto cards = load(world({c, {5}, {6}}));

  Game g;
  g.held = C(0, 0, 0, 3, 0, 5);
  g.card = 1;
  // The second roll repeats the first and is rolled again.
  Script s({0, 0, 1});
  detail::resolve(g, *cards, s);
  CHECK(s.used());
  CHECK(g.cost[0] == C(0, 0, 0, 0, 0, 3));  // half of 5, rounded up; none held, so 840 costs 0
  CHECK(g.gain[0] == C(0, 0, 0, 2, 0, 0));  // half of 3, rounded up
  CHECK(g.rolled[0][0] == 5 && g.rolled[0][1] == 6);

  Game h;
  h.held = C(0, 0, 2, 0, 0, 0);
  h.card = 1;
  Script t({0, 1});
  detail::resolve(h, *cards, t);
  CHECK(h.cost[0][Cultist] == kOnlyIfNone);
  CHECK(!affordable(h, *cards, 0));
}

void paymentIsExactWithRelicsAsWildcards() {
  CardSpec plain{1};
  plain.opts[0].cost = {0, 2, 0, 1, 0, 0};
  CardSpec relic{2};
  relic.opts[0].cost = {1, 1, 0, 0, 0, 0};
  CardSpec swap{3};
  swap.opts[0].cost = {0, 0, 2, 0, 0, 0};
  swap.opts[0].swap = 1;
  CardSpec greed{4};
  greed.opts[0].randomCost = 5;
  auto cards = load(world({plain, relic, swap, greed}));
  Counts p;

  CHECK(suggest(table(*cards, 1, C(0, 2, 0, 1, 0, 0)), *cards, 0, p));
  CHECK(p == C(0, 2, 0, 1, 0, 0));
  // One money short: a relic covers it.
  CHECK(suggest(table(*cards, 1, C(1, 1, 0, 1, 0, 0)), *cards, 0, p));
  CHECK(p == C(1, 1, 0, 1, 0, 0));
  CHECK(!affordable(table(*cards, 1, C(0, 1, 0, 1, 0, 0)), *cards, 0));
  // A relic cost and a money shortfall need two relics.
  CHECK(suggest(table(*cards, 2, C(2, 0, 0, 0, 0, 0)), *cards, 0, p));
  CHECK(p == C(2, 0, 0, 0, 0, 0));
  CHECK(!affordable(table(*cards, 2, C(1, 0, 0, 0, 0, 0)), *cards, 0));

  // Any exact payment is accepted, as in the original: a relic may pay for
  // money that is held. Paying too much, or with what is not held, is not.
  const Game rich = table(*cards, 1, C(1, 2, 0, 1, 0, 0));
  CHECK(exact(rich, *cards, 0, C(0, 2, 0, 1, 0, 0)));
  CHECK(exact(rich, *cards, 0, C(1, 1, 0, 1, 0, 0)));
  CHECK(exact(rich, *cards, 0, C(1, 2, 0, 0, 0, 0)));
  CHECK(!exact(rich, *cards, 0, C(1, 2, 0, 1, 0, 0)));
  CHECK(!exact(rich, *cards, 0, C(0, 2, 0, 0, 0, 0)));
  CHECK(!exact(rich, *cards, 0, C(0, 3, 0, 0, 0, 0)));
  CHECK(!exact(rich, *cards, 0, C(0, 2, 0, 1, 0, -1)));

  // Cultists and prisoners pay for each other in any split; the suggestion
  // spends prisoners first.
  const Game both = table(*cards, 3, C(0, 0, 3, 0, 1, 0));
  CHECK(suggest(both, *cards, 0, p) && p == C(0, 0, 1, 0, 1, 0));
  CHECK(exact(both, *cards, 0, C(0, 0, 2, 0, 0, 0)));
  CHECK(exact(both, *cards, 0, C(0, 0, 1, 0, 1, 0)));
  CHECK(!exact(both, *cards, 0, C(0, 0, 2, 0, 1, 0)));
  CHECK(!exact(table(*cards, 1, C(0, 2, 1, 1, 1, 0)), *cards, 0, C(0, 2, 0, 0, 1, 0)));  // no swap there

  // A random cost needs that many resources held, of any kind, and is paid
  // with nothing up front.
  CHECK(!affordable(table(*cards, 4, C(0, 4, 0, 0, 0, 0)), *cards, 0));
  const Game greedy = table(*cards, 4, C(0, 4, 0, 0, 0, 1));
  CHECK(affordable(greedy, *cards, 0) && exact(greedy, *cards, 0, C(0, 0, 0, 0, 0, 0)));

  // satisfiability itself: 0 exact, negative overpaid, 1 short.
  CHECK(detail::satisfiability(C(0, 2, 0, 0, 0, 0), false, C(0, 2, 0, 0, 0, 0)) == 0);
  CHECK(detail::satisfiability(C(0, 2, 0, 0, 0, 0), false, C(1, 2, 0, 0, 0, 0)) == -1);
  CHECK(detail::satisfiability(C(0, 2, 0, 0, 0, 0), false, C(0, 1, 0, 0, 0, 0)) == 1);
}

void losingOptionsAreLockedWhileAnotherCanBePaid() {
  CardSpec fight{1};
  fight.opts.resize(2);
  fight.opts[0].cost = {0, 1, 0, 0, 0, 0};
  fight.opts[1].lose = 1;
  CardSpec doom{2};  // two ways to lose and nothing else
  doom.opts.resize(2);
  doom.opts[0].lose = 1;
  doom.opts[1].lose = 1;
  auto cards = load(world({fight, doom}));
  if (failures) return;

  const Game can = table(*cards, 1, C(0, 1, 0, 0, 0, 0));
  CHECK(affordable(can, *cards, 0) && !affordable(can, *cards, 1));
  CHECK(can.lockedMask == 0b10 && can.cost[1] == C(840, 840, 840, 840, 840, 840));
  // The screen draws no cost for it, so it says why it is closed.
  CHECK(said(view::whyNot(can, *cards, 1)) == "ONLY WHEN NOTHING ELSE IS OPEN");
  const Game cannot = table(*cards, 1, C(0, 0, 0, 0, 0, 0));
  CHECK(!affordable(cannot, *cards, 0) && affordable(cannot, *cards, 1));
  CHECK(cannot.lockedMask == 0);

  // In order, each against the others as they stand: the first is locked by
  // the second, which is then the only one left open.
  const Game both = table(*cards, 2, C(0, 0, 0, 1, 0, 0));
  CHECK(both.lockedMask == 0b01 && !affordable(both, *cards, 0) && affordable(both, *cards, 1));
}

void choosingPaysGainsAndShufflesIn() {
  CardSpec trade{1};
  trade.recurring = 1;
  trade.opts[0].cost = {0, 2, 0, 0, 0, 0};
  trade.opts[0].gain = {0, 0, 0, 1, 0, 0};
  trade.opts[0].ids = {3};
  trade.opts[0].copies = {2};
  CardSpec roller{10};  // not recurring
  roller.opts[0].lo = 4;
  roller.opts[0].hi = 5;
  roller.opts[0].count = 1;
  CardSpec once{2};  // not recurring
  CardSpec greed{6};
  greed.opts[0].randomCost = 1;
  CardSpec win{7};
  win.opts[0].win = "Gamma";
  CardSpec lose{8};
  lose.opts[0].lose = 1;
  CardSpec priced{9};
  priced.opts[0].cost = {0, 9, 0, 0, 0, 0};
  auto cards = load(world({trade, roller, once, {3}, {4}, {5}, greed, win, lose, priced}));
  if (failures) return;

  // Food held and little else, so no punishment is rolled on the next draw.
  Game g;
  g.held = C(0, 2, 0, 1, 0, 0);
  g.card = 1;
  g.draw.push(2);
  Script none;
  detail::resolve(g, *cards, none);
  CHECK(take(g, *cards, 0, none));
  CHECK(g.paid == C(0, 2, 0, 0, 0, 0));
  CHECK(g.gained == C(0, 0, 0, 1, 0, 0));
  CHECK(g.held == C(0, 0, 0, 2, 0, 0));
  // The shuffled-in cards and then the played card, which recurs, wait in the
  // discard pile; the next card comes off the deck.
  CHECK((pile(g.discard) == std::vector<int>{3, 3, 1}));
  CHECK(g.addedCount == 1 && g.added[0].card == 3 && g.added[0].copies == 2);
  CHECK(g.card == 2 && g.draw.size == 0 && g.phase == Phase::Choosing);

  // A card rolled when the option was drawn goes the same way.
  Game rolled;
  rolled.held = C(0, 0, 0, 1, 0, 0);
  rolled.card = 10;
  rolled.draw.push(2);
  Script roll({1});
  detail::resolve(rolled, *cards, roll);
  CHECK(rolled.rolled[0][0] == 5);
  CHECK(take(rolled, *cards, 0, none));
  CHECK((pile(rolled.discard) == std::vector<int>{5}));
  CHECK(rolled.played == 10 && rolled.playedOption == 0);
  CHECK(rolled.addedCount == 1 && rolled.added[0].card == 5 && rolled.added[0].copies == 1);

  // A card that does not recur leaves the game.
  Game h = table(*cards, 2, C(0, 0, 0, 1, 0, 0), {3});
  CHECK(take(h, *cards, 0, none));
  CHECK(h.discard.size == 0 && h.card == 3);

  // In the tutorial, shuffled-in cards go straight onto the deck.
  Game tut;
  tut.tutorial = true;
  tut.held = C(0, 2, 0, 1, 0, 0);
  tut.card = 1;
  detail::resolve(tut, *cards, none);
  CHECK(take(tut, *cards, 0, none));
  CHECK(tut.card == 3 && (pile(tut.draw) == std::vector<int>{3}));

  // A random loss takes only kinds that are held: relic is rolled first and
  // there is none, so it rolls again and takes money.
  Game r = table(*cards, 6, C(0, 1, 0, 1, 0, 0), {3});
  Script loss({0, 1});
  CHECK(take(r, *cards, 0, loss));
  CHECK(loss.used());
  CHECK(r.lost == C(0, 1, 0, 0, 0, 0) && r.held == C(0, 0, 0, 1, 0, 0));

  // Winning and losing end the run.
  Game w = table(*cards, 7, C(0, 0, 0, 1, 0, 0), {3});
  CHECK(take(w, *cards, 0, none));
  CHECK(w.phase == Phase::Won && w.god == 2);
  Game l = table(*cards, 8, C(0, 0, 0, 1, 0, 0), {3});
  CHECK(take(l, *cards, 0, none));
  CHECK(l.phase == Phase::Lost && l.loss == LossReason::Choice);

  // An option that cannot be paid is refused and changes nothing.
  Game poor = table(*cards, 9, C(0, 1, 0, 1, 0, 0), {3});
  const Game before = poor;
  CHECK(!take(poor, *cards, 0, none));
  CHECK(std::memcmp(&poor, &before, sizeof(Game)) == 0);

  // Drawing a card nothing can pay for ends the run.
  Game stuck = table(*cards, 2, C(0, 1, 0, 1, 0, 0), {9});
  CHECK(take(stuck, *cards, 0, none));
  CHECK(stuck.phase == Phase::Lost && stuck.loss == LossReason::Stuck);

  // So does running out of cards altogether.
  Game empty = table(*cards, 2, C(0, 0, 0, 1, 0, 0));
  CHECK(take(empty, *cards, 0, none));
  CHECK(empty.phase == Phase::Lost && empty.loss == LossReason::NoCards);
}

void foresightShowsTheTopAndDiscardsByPosition() {
  CardSpec see{1};
  see.opts[0].see = 1;
  see.opts[0].discard = 1;
  CardSpec seeAgain{7, 10, 0, 1};  // recurs
  seeAgain.opts[0].see = 1;
  seeAgain.opts[0].discard = 1;
  CardSpec seeAndAdd{8};
  seeAndAdd.opts[0].see = 1;
  seeAndAdd.opts[0].discard = 1;
  seeAndAdd.opts[0].ids = {9};
  seeAndAdd.opts[0].copies = {1};
  auto cards = load(world({see, seeAgain, seeAndAdd, {3}, {4}, {5}, {6}, {9}}));

  Game g = table(*cards, 1, C(0, 0, 0, 1, 0, 0), {6, 5, 4, 3});
  Script none;
  CHECK(take(g, *cards, 0, none));
  CHECK(g.phase == Phase::Foresight && g.mayDiscard);
  CHECK(g.seenCount == 3 && g.seen[0] == 3 && g.seen[1] == 4 && g.seen[2] == 5);
  toggleDiscard(g, 0);
  toggleDiscard(g, 2);
  endForesight(g, *cards, none);
  // 3 and 5 go to the discard pile; 4 was between them and is drawn next.
  CHECK((pile(g.discard) == std::vector<int>{3, 5}));
  CHECK(g.card == 4 && (pile(g.draw) == std::vector<int>{6}));

  // Fewer than three left: the deck is reshuffled first, then shown.
  Game h = table(*cards, 1, C(0, 0, 0, 1, 0, 0), {3});
  h.discard.push(4);
  h.discard.push(5);
  Script s({0, 0, 0, 0, 0});  // no swaps; picks 3, 4, 5 in turn, so 3 on top
  CHECK(take(h, *cards, 0, s));
  CHECK(s.used());
  CHECK(h.seenCount == 3 && h.seen[0] == 3 && h.seen[1] == 4 && h.seen[2] == 5);
  CHECK(h.reshuffled);

  // A played card that recurs goes to the discard pile after the discards.
  Game r = table(*cards, 7, C(0, 0, 0, 1, 0, 0), {6, 5, 4, 3});
  CHECK(take(r, *cards, 0, none));
  toggleDiscard(r, 0);
  endForesight(r, *cards, none);
  CHECK((pile(r.discard) == std::vector<int>{3, 7}));

  // In the tutorial a shuffle-in lands on top after the peek. The card marked
  // is still the one discarded. (The original removes the card one position
  // up and discards the marked one, so a card is lost and another doubled; no
  // shipped card has foresight and a shuffle-in together.)
  Game t = table(*cards, 8, C(0, 0, 0, 1, 0, 0), {6, 5, 4, 3});
  t.tutorial = true;
  CHECK(take(t, *cards, 0, none));
  CHECK(t.seenCount == 3 && t.seen[0] == 3 && t.seen[1] == 4 && t.seen[2] == 5);
  toggleDiscard(t, 2);
  endForesight(t, *cards, none);
  CHECK((pile(t.discard) == std::vector<int>{5}));
  CHECK(t.card == 9 && (pile(t.draw) == std::vector<int>{6, 4, 3}));
}

void startDealsTheOriginalDeck() {
  auto cards = load(world({{1, 10, 1}, {2, 10, 1}, {3, 10, 1}}));
  Profile p;
  p.tutorialDone = true;
  p.summoned = 0b0000111;  // three gods: the first tier of quest openers
  p.previous = 1;          // Beta, who unlocks 202 and 203

  Game g;
  // Coin flips for the initial cards in id order: 1 in, 2 out, 3 in. Then the
  // deck is [1 3 85 106 202 203]: nothing is swapped and each weighted pick
  // takes the first card, so 1 ends on top. Rumors goes in at the very top.
  Script s({0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  start(g, *cards, p, s);
  CHECK(s.used());
  CHECK((s.asked == std::vector<int>{2, 2, 2, 6, 5, 4, 3, 2, 60, 50, 40, 30, 20, 10, 5}));
  CHECK(g.held == C(0, 2, 2, 2, 2, 0));
  CHECK(g.card == ids::kRumors);
  CHECK((pile(g.draw) == std::vector<int>{203, 202, 106, 85, 3, 1}));
  CHECK(!g.reshuffled && !g.tutorial && g.phase == Phase::Choosing);
  // The unlocks are dealt into this run only.
  CHECK(p.previous == -1);

  // The next run deals no unlocks, so the deck is [1 3 85 106], drawn 1
  // first. Rumors goes three from the top, under 1, 3 and 85.
  Game d;
  Script deep({0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3});
  start(d, *cards, p, deep);
  CHECK(deep.used());
  CHECK((deep.asked == std::vector<int>{2, 2, 2, 4, 3, 2, 40, 30, 20, 10, 5}));
  CHECK(d.card == 1 && (pile(d.draw) == std::vector<int>{106, ids::kRumors, 85, 3}));

  // Five gods bring in the second tier; with no god summoned yet and the
  // tutorial not won, the hand is empty and the tutorial card is on top.
  Profile five;
  five.tutorialDone = true;
  five.summoned = 0b0011111;
  Game f;
  Rng rng(3);
  start(f, *cards, five, rng);
  std::set<int> dealt(f.draw.card, f.draw.card + f.draw.size);
  dealt.insert(f.card);
  CHECK(dealt.count(57) && dealt.count(79) && dealt.count(85) && dealt.count(106));

  Profile fresh;
  Game t;
  start(t, *cards, fresh, rng);
  CHECK(t.tutorial && t.card == ids::kTutorial);
  CHECK(t.held == C(0, 0, 0, 0, 0, 0));
}

void profileRemembersWins() {
  Game won;
  won.phase = Phase::Won;
  won.god = 3;
  won.tutorial = true;
  Profile p;
  finish(p, won);
  CHECK(p.summoned == 0b1000 && p.previous == 3 && p.tutorialDone);
  Game lost;
  lost.phase = Phase::Lost;
  lost.tutorial = true;
  Profile q;
  q.previous = 2;
  finish(q, lost);
  CHECK(q.summoned == 0 && q.previous == -1 && !q.tutorialDone);
  won.tutorial = false;
  Profile r;
  finish(r, won);
  CHECK(!r.tutorialDone);
}

void savesAreCheckedBeforeTheyAreTrusted() {
  auto cards = load(world({{1}, {3}}));
  Game g = table(*cards, 1, C(0, 1, 0, 1, 0, 0), {3});
  CHECK(valid(g, *cards));
  Game badCard = g;
  badCard.card = 99;
  CHECK(!valid(badCard, *cards));
  Game badPile = g;
  badPile.draw.card[0] = 99;
  CHECK(!valid(badPile, *cards));
  Game badSize = g;
  badSize.discard.size = Pile::kCapacity + 1;
  CHECK(!valid(badSize, *cards));
  Game badPhase = g;
  badPhase.phase = static_cast<Phase>(9);
  CHECK(!valid(badPhase, *cards));
  Game badGod = g;
  badGod.phase = Phase::Won;
  badGod.god = 7;
  CHECK(!valid(badGod, *cards));
  Game negative = g;
  negative.held[Food] = -1;
  CHECK(!valid(negative, *cards));

  Pile full;
  for (int i = 0; i < Pile::kCapacity; ++i) CHECK(full.push(1));
  CHECK(!full.overflowed && !full.push(1) && full.overflowed && full.size == Pile::kCapacity);
}

// Mario's one reason to pay a relic where the named resource is held: it
// keeps the last food, since starving invites Desperate Measures.
void lastFoodIsWorthARelic() {
  CardSpec plain{10};
  plain.opts[0].cost = {0, 1, 0, 1, 0, 0};
  CardSpec either{11};
  either.opts[0].cost = {0, 0, 1, 1, 0, 0};
  either.opts[0].swap = 1;
  CardSpec wins{12};
  wins.opts[0].cost = {0, 1, 0, 1, 0, 0};
  wins.opts[0].win = "Gamma";
  CardSpec loses{13};
  loses.opts[0].cost = {0, 1, 0, 1, 0, 0};
  loses.opts[0].lose = 1;
  CardSpec feeds{14};
  feeds.opts[0].cost = {0, 1, 0, 1, 0, 0};
  feeds.opts[0].gain = {0, 0, 0, 1, 0, 0};
  CardSpec twice{15};
  twice.opts[0].cost = {0, 2, 0, 2, 0, 0};
  auto cards = load(world({plain, either, wins, loses, feeds, twice}));
  if (failures) return;
  Counts ways[8];
  const Counts empty{};

  // The last food: a relic may pay for it, and the panel opens on the money.
  Game hungry = table(*cards, 10, C(1, 1, 0, 1, 0, 0));
  CHECK(view::choices(hungry, *cards, 0, ways, 8) == 2);
  CHECK(ways[0] == C(0, 1, 0, 1, 0, 0) && ways[1] == C(1, 1, 0, 0, 0, 0));
  CHECK(view::savesLastFood(hungry, *cards, 0) && view::common(hungry, *cards, 0) == C(0, 1, 0, 0, 0, 0));
  // Not the last food, or no relic: nothing to choose.
  CHECK(view::choices(table(*cards, 10, C(1, 1, 0, 2, 0, 0)), *cards, 0, ways, 8) == 1);
  CHECK(ways[0] == C(0, 1, 0, 1, 0, 0));
  CHECK(!view::savesLastFood(table(*cards, 10, C(1, 1, 0, 2, 0, 0)), *cards, 0));
  CHECK(view::choices(table(*cards, 10, C(0, 1, 0, 1, 0, 0)), *cards, 0, ways, 8) == 1);
  // Short of money with the last food: one relic for the money, or two.
  CHECK(view::choices(table(*cards, 10, C(2, 0, 0, 1, 0, 0)), *cards, 0, ways, 8) == 2);
  CHECK(ways[0] == C(1, 0, 0, 1, 0, 0) && ways[1] == C(2, 0, 0, 0, 0, 0));
  // Two asked and two held: the way keeping one food, not the one keeping two.
  CHECK(view::choices(table(*cards, 15, C(2, 2, 0, 2, 0, 0)), *cards, 0, ways, 8) == 2);
  CHECK(ways[0] == C(0, 2, 0, 2, 0, 0) && ways[1] == C(1, 2, 0, 1, 0, 0));
  CHECK(view::common(table(*cards, 15, C(2, 2, 0, 2, 0, 0)), *cards, 0) == C(0, 2, 0, 1, 0, 0));

  // Either a cultist or a prisoner goes with the relic, as with the food.
  Game both = table(*cards, 11, C(1, 0, 1, 1, 1, 0));
  CHECK(view::choices(both, *cards, 0, ways, 8) == 4);
  CHECK(ways[2][Relic] == 1 && ways[3][Relic] == 1 && ways[2][Food] == 0 && ways[3][Food] == 0);
  CHECK(ways[2][Cultist] + ways[3][Cultist] == 1 && ways[2][Prisoner] + ways[3][Prisoner] == 1);
  CHECK(view::canAdd(both, *cards, 0, C(0, 0, 0, 0, 1, 0), Relic) &&
        view::canAdd(both, *cards, 0, C(0, 0, 1, 0, 0, 0), Relic));
  CHECK(view::common(both, *cards, 0) == empty);

  // Nowhere nothing is rolled after (a win, a loss, the tutorial), nor when
  // the choice gives the food back.
  for (int id : {12, 13, 14}) {
    CHECK(view::choices(table(*cards, id, C(1, 1, 0, 1, 0, 0)), *cards, 0, ways, 8) == 1);
    CHECK(ways[0] == C(0, 1, 0, 1, 0, 0));
  }
  Game learning = hungry;
  learning.tutorial = true;
  CHECK(view::choices(learning, *cards, 0, ways, 8) == 1 && !view::savesLastFood(learning, *cards, 0));

  // Choosing from the bar: only what the option asks for, never more.
  CHECK(view::canAdd(hungry, *cards, 0, empty, Money) && view::canAdd(hungry, *cards, 0, empty, Food));
  CHECK(view::canAdd(hungry, *cards, 0, empty, Relic) && !view::canAdd(hungry, *cards, 0, empty, Cultist));
  CHECK(!view::canAdd(hungry, *cards, 0, C(0, 1, 0, 1, 0, 0), Relic));  // already paid in full
  CHECK(!view::canAdd(hungry, *cards, 0, C(1, 0, 0, 0, 0, 0), Relic));  // the only relic is taken
  CHECK(view::canAdd(hungry, *cards, 0, C(1, 0, 0, 0, 0, 0), Money));
  CHECK(exact(hungry, *cards, 0, C(1, 1, 0, 0, 0, 0)) && !exact(hungry, *cards, 0, C(1, 0, 0, 0, 0, 0)));

  // The hand a payment leaves is what the paying panel warns about.
  CHECK(punishmentOdds(hungry, C(1, 0, 0, 0, 0, 0)).desperate > 0 && punishmentOdds(hungry).desperate == 0);
  CHECK(punishmentOdds(learning, C(1, 0, 0, 0, 0, 0)).desperate == 0);
}

// Every pick the bar allows, from nothing or from what the panel opens on,
// can still be finished, and PAY lights exactly on the ways offered.
void walkPicks(const Game& g, const Cards& cards, int k, const Counts* ways, int n) {
  std::set<Counts> seen;
  std::vector<Counts> todo = {Counts{}, view::common(g, cards, k)};
  while (!todo.empty()) {
    const Counts at = todo.back();
    todo.pop_back();
    if (!seen.insert(at).second) continue;
    bool isWay = false;
    bool fits = false;
    for (int i = 0; i < n; ++i) {
      isWay = isWay || ways[i] == at;
      bool inside = true;
      for (int r = 0; r < kResources; ++r) inside = inside && at[r] <= ways[i][r];
      fits = fits || inside;
    }
    CHECK(fits);
    CHECK(exact(g, cards, k, at) == isWay);
    for (int r = 0; r < kResources; ++r) {
      if (!view::canAdd(g, cards, k, at, r)) continue;
      Counts more = at;
      ++more[r];
      todo.push_back(more);
    }
  }
}

void waysToPayAreEveryExactPayment() {
  CardSpec swap{1};
  swap.opts[0].cost = {0, 0, 2, 0, 0, 0};
  swap.opts[0].swap = 1;
  CardSpec plain{2};
  plain.opts[0].cost = {0, 1, 0, 1, 0, 0};
  CardSpec none{3};
  none.opts[0].cost = {0, 0, underhand::kOnlyIfNone, 0, 0, 0};
  CardSpec calm{4};
  calm.opts[0].cost = {0, 1, 0, 0, 0, 1};
  CardSpec tamper{5};  // Reduce Suspicion's Tamper: 3 suspicion, nothing else
  tamper.opts[0].cost = {0, 0, 0, 0, 0, 3};
  tamper.opts.push_back(Opt{});
  tamper.opts[1].cost = {0, 0, 0, 0, 0, 1};
  tamper.opts[1].gain = {1, 0, 0, 0, 0, 0};  // and one that gives something for it
  auto cards = load(world({swap, plain, none, calm, tamper}));
  if (failures) return;
  Counts ways[8];

  // Two cultists or prisoners from three cultists and one prisoner: the
  // prisoner first, then both cultists.
  Game g = table(*cards, 1, C(0, 0, 3, 1, 1, 0));
  CHECK(view::payments(g, *cards, 0, ways, 8) == 2);
  CHECK(ways[0] == C(0, 0, 1, 0, 1, 0) && ways[1] == C(0, 0, 2, 0, 0, 0));
  // A relic adds the ways it could stand in, after the ways without it.
  Game r = table(*cards, 1, C(1, 0, 3, 1, 1, 0));
  const int n = view::payments(r, *cards, 0, ways, 8);
  CHECK(n == 4);
  CHECK(ways[0][Relic] == 0 && ways[1][Relic] == 0 && ways[2][Relic] == 1 && ways[3][Relic] == 1);
  Counts first;
  CHECK(suggest(r, *cards, 0, first) && first == ways[0]);
  for (int i = 0; i < n && i < 8; ++i) CHECK(exact(r, *cards, 0, ways[i]));
  // One way only when nothing can stand in.
  CHECK(view::payments(table(*cards, 2, C(0, 1, 0, 1, 0, 0)), *cards, 0, ways, 8) == 1);
  // None when it cannot be paid. What is short shows by itself (the cost
  // beside the bar), so nothing is said; a cost of none is not drawn, so it is.
  const Game poor = table(*cards, 2, C(0, 0, 0, 1, 0, 0));
  CHECK(view::payments(poor, *cards, 0, ways, 8) == 0);
  CHECK(view::optionState(poor, *cards, 0) != view::OptionState::Open && said(view::whyNot(poor, *cards, 0)).empty());
  CHECK(said(view::whyNot(table(*cards, 2, C(1, 0, 0, 0, 0, 0)), *cards, 0)).empty());
  const Game swapPoor = table(*cards, 1, C(0, 0, 1, 1, 0, 0));
  CHECK(said(view::whyNot(swapPoor, *cards, 0)).empty());
  CHECK(said(view::whyNot(table(*cards, 3, C(0, 0, 2, 1, 0, 0)), *cards, 0)) == "ONLY WITH NO C");
  CHECK(said(view::whyNot(g, *cards, 0)).empty());
  // A relic paying for suspicion keeps the suspicion: those ways come last,
  // even after a way spending more relics on something else.
  const Game calmHand = table(*cards, 4, C(2, 1, 0, 1, 0, 1));
  CHECK(view::payments(calmHand, *cards, 0, ways, 8) == 4);
  CHECK(ways[0] == C(0, 1, 0, 0, 0, 1) && ways[1] == C(1, 0, 0, 0, 0, 1));
  CHECK(ways[2] == C(1, 1, 0, 0, 0, 0) && ways[3] == C(2, 0, 0, 0, 0, 0));
  CHECK(suggest(calmHand, *cards, 0, first) && first == ways[0]);

  // What is offered: never a way that leaves held suspicion unspent.
  // Holding all 3: the relic ways keep suspicion and are not offered.
  Game t = table(*cards, 5, C(2, 0, 0, 1, 0, 3));
  CHECK(view::payments(t, *cards, 0, ways, 8) == 3);
  CHECK(view::choices(t, *cards, 0, ways, 8) == 1 && ways[0] == C(0, 0, 0, 0, 0, 3));
  CHECK(!view::buysNothing(t, *cards, 0));
  // Holding 2 of the 3: a relic makes up the third, and that is the way.
  t = table(*cards, 5, C(2, 0, 0, 1, 0, 2));
  CHECK(view::choices(t, *cards, 0, ways, 8) == 1 && ways[0] == C(1, 0, 0, 0, 0, 2));
  CHECK(!view::buysNothing(t, *cards, 0));
  // Holding none: relics pay and take no suspicion off, which is nothing.
  t = table(*cards, 5, C(3, 0, 0, 1, 0, 0));
  CHECK(view::choices(t, *cards, 0, ways, 8) == 1 && ways[0] == C(3, 0, 0, 0, 0, 0));
  CHECK(view::buysNothing(t, *cards, 0));
  // Unless the option gives something besides.
  CHECK(view::choices(t, *cards, 1, ways, 8) == 1 && !view::buysNothing(t, *cards, 1));
  // Or paying lowers Greed's chance: at 16 held, 3 relics take it to 0.
  t = table(*cards, 5, C(3, 5, 5, 3, 0, 0));
  CHECK(punishmentOdds(t).greed == 35 && !view::buysNothing(t, *cards, 0));
  // But where Greed stays certain after paying, the relics still buy nothing.
  t = table(*cards, 5, C(3, 9, 9, 3, 0, 0));
  CHECK(punishmentOdds(t).greed == 100 && view::buysNothing(t, *cards, 0));
  // Without the relics to pay it is closed, and the bar's 0 says why.
  CHECK(view::optionState(table(*cards, 5, C(0, 1, 0, 1, 0, 0)), *cards, 0) != view::OptionState::Open);
  CHECK(said(view::whyNot(table(*cards, 5, C(0, 1, 0, 1, 0, 0)), *cards, 0)).empty());
  // Relics are offered only where the hand needs them: 1 money and 1 food
  // asked, both held, a relic spare: one way, the money and the food...
  CHECK(view::choices(table(*cards, 2, C(1, 1, 0, 2, 0, 0)), *cards, 0, ways, 8) == 1);
  CHECK(ways[0] == C(0, 1, 0, 1, 0, 0));
  // Card 2 is the Police Raid's id, a punishment, after which nothing is
  // rolled: even the last food is not worth a relic there (the food rule is
  // lastFoodIsWorthARelic's).
  CHECK(view::choices(table(*cards, 2, C(1, 1, 0, 1, 0, 0)), *cards, 0, ways, 8) == 1);
  // Short of money, the relic pays for it and there is nothing to choose.
  CHECK(view::choices(table(*cards, 2, C(1, 0, 0, 2, 0, 0)), *cards, 0, ways, 8) == 1);
  CHECK(ways[0] == C(1, 0, 0, 1, 0, 0));

  // Choosing from the bar: only what the option asks for, never more.
  const Counts empty{};
  // A cultist or a prisoner, either, up to what is held.
  Game people = table(*cards, 1, C(0, 0, 3, 1, 1, 0));
  CHECK(view::canAdd(people, *cards, 0, empty, Prisoner) && view::canAdd(people, *cards, 0, empty, Cultist));
  CHECK(!view::canAdd(people, *cards, 0, C(0, 0, 0, 0, 1, 0), Prisoner));
  CHECK(view::canAdd(people, *cards, 0, C(0, 0, 0, 0, 1, 0), Cultist));
  CHECK(!view::canAdd(people, *cards, 0, C(0, 0, 1, 0, 1, 0), Cultist));
  // A spare relic is not offered for people who are held.
  Game spare = table(*cards, 1, C(1, 0, 3, 1, 1, 0));
  CHECK(!view::canAdd(spare, *cards, 0, empty, Relic) && view::canAdd(spare, *cards, 0, empty, Prisoner));

  // The first choice is always suggest()'s.
  for (const Counts& held : {C(2, 0, 0, 1, 0, 3), C(2, 0, 0, 1, 0, 2), C(3, 0, 0, 1, 0, 0)}) {
    t = table(*cards, 5, held);
    CHECK(view::choices(t, *cards, 0, ways, 8) >= 1 && suggest(t, *cards, 0, first) && first == ways[0]);
  }
}

void savesRoundTripAndRefuseDamage() {
  auto cards = load(world({{1}, {3}}));
  if (failures) return;
  Save s;
  s.profile.summoned = 0b101;
  s.profile.previous = 2;
  s.profile.tutorialDone = true;
  s.inRun = true;
  s.game = table(*cards, 1, C(1, 2, 3, 4, 5, 6), {3});
  s.rng = 0xABCDEF;
  uint8_t bytes[kSaveBytes];
  encode(s, bytes);
  Save back;
  CHECK(decode(bytes, sizeof(bytes), *cards, back));
  CHECK(back.inRun && back.rng == 0xABCDEF && back.profile.summoned == 0b101 && back.profile.previous == 2);
  CHECK(std::memcmp(&back.game, &s.game, sizeof(Game)) == 0);
  CHECK(!back.showOutcome);
  // The outcome panel survives leaving, but only over a choice that was made.
  Save shown = s;
  shown.showOutcome = true;
  encode(shown, bytes);
  CHECK(decode(bytes, sizeof(bytes), *cards, back) && !back.showOutcome);
  shown.game.played = 1;
  encode(shown, bytes);
  CHECK(decode(bytes, sizeof(bytes), *cards, back) && back.showOutcome && back.inRun);
  // A damaged flag inside the Game reads as set, never as a bool that is
  // neither true nor false.
  encode(s, bytes);
  bytes[8 + sizeof(Profile) + 1 + offsetof(Game, reshuffled)] = 0x5A;
  CHECK(decode(bytes, sizeof(bytes), *cards, back) && back.inRun && back.game.reshuffled == true);
  // A flags byte no build writes drops the run and keeps the profile.
  encode(s, bytes);
  bytes[8 + sizeof(Profile)] = 0x7F;
  CHECK(decode(bytes, sizeof(bytes), *cards, back) && !back.inRun && !back.showOutcome);
  CHECK(back.profile.summoned == 0b101 && back.profile.tutorialDone);
  // A save from a build whose Game differs keeps the gods and the tutorial.
  encode(s, bytes);
  uint8_t older[kSaveBytes];
  std::memcpy(older, bytes, sizeof(bytes));
  older[6] ^= 0x10;  // another sizeof(Game)
  CHECK(decode(older, sizeof(older), *cards, back));
  CHECK(!back.inRun && back.profile.summoned == 0b101 && back.profile.previous == 2 && back.profile.tutorialDone);
  CHECK(decode(older, 8 + sizeof(Profile), *cards, back) && back.profile.summoned == 0b101);
  // What the last choice added is read back only if it fits and exists.
  Save spilled = s;
  spilled.game.addedCount = 200;
  encode(spilled, older);
  CHECK(decode(older, sizeof(older), *cards, back) && !back.inRun && back.profile.summoned == 0b101);
  spilled.game.addedCount = 1;
  spilled.game.added[0] = Game::Added{77, 1};
  encode(spilled, older);
  CHECK(decode(older, sizeof(older), *cards, back) && !back.inRun);
  // The wrong length keeps the profile and drops the run; another app's
  // bytes, or too few to hold a profile, are refused outright.
  CHECK(decode(bytes, sizeof(bytes) - 1, *cards, back) && !back.inRun && back.profile.summoned == 0b101);
  CHECK(!decode(bytes, 8 + sizeof(Profile) - 1, *cards, back));
  uint8_t other[kSaveBytes];
  std::memcpy(other, bytes, sizeof(bytes));
  other[0] = 'X';
  CHECK(!decode(other, sizeof(other), *cards, back));
  // A run these cards cannot continue is dropped and the profile kept.
  Save broken = s;
  broken.game.card = 99;
  encode(broken, other);
  CHECK(decode(other, sizeof(other), *cards, back));
  CHECK(!back.inRun && back.profile.summoned == 0b101);
}

void rngIsUniformAndRepeatable() {
  Rng a(42);
  Rng b(42);
  int bins[6] = {};
  for (int i = 0; i < 60000; ++i) {
    const int x = a.below(6);
    CHECK(x == b.below(6));
    if (x >= 0 && x < 6) ++bins[x];
  }
  for (int n : bins) CHECK(n > 9500 && n < 10500);
  CHECK(a.below(1) == 0 && a.below(0) == 0);
}

// ---- the real cards -------------------------------------------------------

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

struct Stats {
  int runs = 0;
  int won = 0;
  int capped = 0;
  int maxPile = 0;
  long long turns = 0;
  int wins[8] = {};
  int losses[4] = {};
  int killedBy[256] = {};
  std::set<int> seen;
};

// For every card, how many shuffle-ins away it is from a card that can
// summon a god: 0 for such a card, kFar for a dead end.
constexpr int kFar = 1000;
std::vector<int> distanceToWin(const Cards& cards) {
  std::vector<int> d(256, kFar);
  for (int i = 0; i < cards.count(); ++i) {
    const Card& c = cards.at(i);
    for (int k = 0; k < c.optionCount; ++k) {
      if (c.option[k].win >= 0) d[c.id] = 0;
    }
  }
  for (bool changed = true; changed;) {
    changed = false;
    for (int i = 0; i < cards.count(); ++i) {
      const Card& c = cards.at(i);
      for (int k = 0; k < c.optionCount; ++k) {
        const Option& o = c.option[k];
        auto consider = [&](int id) {
          if (d[id] + 1 < d[c.id]) {
            d[c.id] = d[id] + 1;
            changed = true;
          }
        };
        for (int a = 0; a < o.addCount; ++a) consider(o.add[a].card);
        for (int id = o.rollLow; o.rollCount && id <= o.rollHigh; ++id) consider(id);
      }
    }
  }
  return d;
}

// Players who keep playing: each runs until all seven gods are summoned or
// their runs are spent, checking the invariants at every step. They take a
// winning option when there is one, avoid options that end the run, and
// otherwise prefer options that shuffle in cards closer to a summoning. With
// `explore`, the hand is dealt afresh before every choice and half the
// choices ignore the quest.
Stats campaign(const Cards& cards, bool explore) {
  const std::vector<int> distance = distanceToWin(cards);
  const int kPlayers = explore ? 2000 : 400;
  const int kRunsEach = explore ? 100 : 60;
  constexpr int kTurnCap = 5000;
  Stats s;
  for (int pl = 0; pl < kPlayers; ++pl) {
    Rng rng((explore ? 50000 : 1000) + pl);
    Profile profile;
    for (int run = 0; run < kRunsEach && profile.summoned != 0x7f; ++run) {
      Game g;
      start(g, cards, profile, rng);
      ++s.runs;
      int t = 0;
      for (; t < kTurnCap && (g.phase == Phase::Choosing || g.phase == Phase::Foresight); ++t) {
        for (int16_t n : g.held) CHECK(n >= 0);
        const int inPlay = g.draw.size + g.discard.size;
        if (inPlay > s.maxPile) s.maxPile = inPlay;
        CHECK(inPlay < Pile::kCapacity - 8);
        CHECK(cards.card(g.card) != nullptr);
        s.seen.insert(g.card);
        if (g.phase == Phase::Foresight) {
          for (int i = 0; i < g.seenCount; ++i) {
            if (rng.below(2)) toggleDiscard(g, i);
          }
          endForesight(g, cards, rng);
          continue;
        }
        if (explore) {
          for (int r = 0; r < kResources; ++r) g.held[r] = static_cast<int16_t>(rng.below(r == Relic ? 2 : 5));
          detail::resolve(g, cards, rng);
        }
        const Card& card = *cards.card(g.card);
        // What the screen offers and pay() reads: some way exactly when the
        // option can be paid, the first suggest()'s, each exact, none leaving
        // held suspicion unspent.
        for (int k = 0; k < card.optionCount; ++k) {
          Counts ways[8];
          const int n = view::choices(g, cards, k, ways, 8);
          CHECK((n > 0) == affordable(g, cards, k));
          Counts first;
          if (n > 0) CHECK(suggest(g, cards, k, first) && first == ways[0]);
          const int asked = g.cost[k][Suspicion] > 0 ? g.cost[k][Suspicion] : 0;
          const int spendable = std::min(asked, static_cast<int>(g.held[Suspicion]));
          for (int i = 0; i < n && i < 8; ++i) {
            CHECK(exact(g, cards, k, ways[i]));
            CHECK(ways[i][Suspicion] == spendable);
          }
          // The full list, in place, agrees with the short one.
          Counts all[view::kMostPayments];
          CHECK(view::choices(g, cards, k, all, view::kMostPayments) == n);
          for (int i = 0; i < n && i < 8; ++i) CHECK(all[i] == ways[i]);
          // Every way uses the fewest relics, but for those keeping the
          // last food; and every pick the bar allows can be finished.
          for (int i = 0; i < n && i < view::kMostPayments; ++i) {
            const int food = g.held[Food] - all[i][Food] + g.gain[k][Food];
            CHECK(all[i][Relic] == all[0][Relic] || (all[i][Relic] > all[0][Relic] && food == 1));
          }
          if (n > 1) walkPicks(g, cards, k, all, std::min(n, view::kMostPayments));
        }
        int best[kMaxOptions];
        int n = 0;
        int bestScore = -1;
        // The explorer wanders half the time, so side branches come up too.
        const bool wander = explore && rng.below(2);
        for (int k = 0; k < card.optionCount; ++k) {
          if (!affordable(g, cards, k)) continue;
          const Option& o = card.option[k];
          int nearest = kFar;
          for (int a = 0; a < o.addCount; ++a) nearest = std::min(nearest, distance[o.add[a].card]);
          for (int id = o.rollLow; o.rollCount && id <= o.rollHigh; ++id) nearest = std::min(nearest, distance[id]);
          int score = o.win >= 0 ? 2 * kFar : o.lose ? 0 : nearest < kFar ? 2 * kFar - 1 - nearest : 1;
          if (wander && score > 0 && o.win < 0) score = 1;
          if (score > bestScore) n = 0;
          if (score >= bestScore) {
            bestScore = score;
            best[n++] = k;
          }
        }
        if (n == 0) break;  // the explorer's fresh hand can leave nothing payable
        const uint8_t playing = g.card;
        CHECK(take(g, cards, best[rng.below(n)], rng));
        CHECK(!g.draw.overflowed && !g.discard.overflowed);
        if (g.phase == Phase::Lost && g.loss == LossReason::Choice) ++s.killedBy[playing];
      }
      s.turns += t;
      if (t >= kTurnCap) ++s.capped;
      if (g.phase == Phase::Won) {
        ++s.won;
        ++s.wins[g.god];
      } else if (g.phase == Phase::Lost) {
        ++s.losses[static_cast<int>(g.loss)];
      }
      finish(profile, g);
    }
  }
  return s;
}

// The embedded cards are the original files apart from line endings.
void embeddedCardsAreTheOriginalFiles(const char* dir) {
  auto withoutCR = [](std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
    return s;
  };
  const std::string gods = withoutCR(readFile(std::string(dir) + "/savedatafiletemplate.json"));
  const std::string json = withoutCR(readFile(std::string(dir) + "/cardwip.json"));
  CHECK(!gods.empty() && !json.empty());
  CHECK(gods == std::string(kGodsJson));
  CHECK(json == std::string(kCardsJson));
}

std::unique_ptr<Cards> realCards() {
  auto cards = std::make_unique<Cards>();
  const std::string gods(kGodsJson);
  const std::string json(kCardsJson);
  CHECK(CardsReader::read(*cards, CardsReader::File::Gods, gods.data(), gods.size()));
  CHECK(CardsReader::read(*cards, CardsReader::File::Cards, json.data(), json.size()));
  return cards;
}

std::string said(void (*line)(const Game&, const Cards&, char*, size_t), const Game& g, const Cards& cards) {
  char out[160];
  line(g, cards, out, sizeof(out));
  return out;
}

// The words the screen adds to the card data: added cards counted by title,
// what a choice did to the deck, and the tutorial lines that describe the
// phone's controls.
void theScreenSaysWhatHappened() {
  auto cards = realCards();

  // The Necronomicon rolls three of six cards that share one title.
  Game g;
  g.card = 14;
  g.rolled[0][0] = 15;
  g.rolled[0][1] = 17;
  g.rolled[0][2] = 19;
  char effect[112];
  view::effectLine(g, *cards, 0, effect, sizeof(effect));
  CHECK(std::string(effect) == "Adds 3 x \"Reading the Necronomicon\"");
  const view::Adds adds = view::optionAdds(g, *cards, 0);
  CHECK(adds.count == 1 && adds.copies[0] == 3);
  g.card = 4;  // Gods Demand Sacrifice: its third option names its card
  view::effectLine(g, *cards, 2, effect, sizeof(effect));
  CHECK(std::string(effect) == "Adds \"Wrath of the Gods\"");

  // After the choice: one sentence, by where the cards went.
  Game d;
  d.added[0] = Game::Added{15, 1};
  d.added[1] = Game::Added{16, 1};
  d.added[2] = Game::Added{5, 2};
  d.addedCount = 3;
  CHECK(said(view::deckSentence, d, *cards) ==
        "2 x \"Reading the Necronomicon\" and 2 x \"Wrath of the Gods\" join the deck at the next shuffle.");
  d.reshuffled = true;
  CHECK(said(view::deckSentence, d, *cards) ==
        "The deck was reshuffled and now holds 2 x \"Reading the Necronomicon\" and 2 x \"Wrath of the Gods\".");
  d.addedCount = 0;
  CHECK(said(view::deckSentence, d, *cards) == "The deck was reshuffled.");
  d.reshuffled = false;
  CHECK(said(view::deckSentence, d, *cards).empty());
  d.tutorial = true;
  d.added[0] = Game::Added{92, 1};
  d.addedCount = 1;
  CHECK(said(view::deckSentence, d, *cards) == "\"Other Options Require Resources\" goes on top of the deck.");
  d.tutorial = false;
  d.added[1] = Game::Added{93, 1};
  d.added[2] = Game::Added{94, 1};
  d.addedCount = 3;
  CHECK(said(view::deckSentence, d, *cards) ==
        "\"Other Options Require Resources\", \"Adding Cards to the Deck\" and \"God Event Chains\" join the deck "
        "at the next shuffle.");

  // The tutorial's words for dragging, as words for tapping; everything else
  // is the card's own.
  CHECK(std::string(view::flavorText(*cards, *cards->card(92))) == "Tap an option to pay for it from what you hold");
  CHECK(std::string(view::optionText(*cards, *cards->card(92), 0)) == "What an option takes is shown at its left");
  CHECK(std::string(view::optionText(*cards, *cards->card(93), 0)) == "The option says which card it adds");
  CHECK(std::string(view::optionText(*cards, *cards->card(99), 0)) == "Here prisoners and cultists pay in any mix");
  CHECK(std::string(view::flavorText(*cards, *cards->card(91))) ==
        "What an option gives is shown at its right, and what else it does below");
  CHECK(std::string(view::flavorText(*cards, *cards->card(1))) == "She probably doesn't know who she's selling to");
  CHECK(std::string(view::optionText(*cards, *cards->card(91), 0)) == "This option gives you one of each resource");
  // Matched on the words as well as the id: a card 92 that says something
  // else says it.
  // No option of the real cards has more ways to pay than the screens list,
  // even from a hand fuller than any run reaches.
  int most = 0;
  for (int i = 0; i < cards->count(); ++i) {
    Game h;
    h.card = cards->at(i).id;
    h.held = C(9, 25, 25, 25, 25, 25);
    Rng rolls(static_cast<uint64_t>(i) + 1);
    detail::resolve(h, *cards, rolls);
    Counts all[view::kMostPayments];
    for (int k = 0; k < cards->at(i).optionCount; ++k) {
      const int n = view::payments(h, *cards, k, all, view::kMostPayments);
      most = n > most ? n : most;
    }
  }
  std::printf("  most ways to pay any option: %d of %d\n", most, view::kMostPayments);
  CHECK(most <= view::kMostPayments);

  auto fixture = load(world({CardSpec{92}}));
  CHECK(std::string(view::flavorText(*fixture, *fixture->card(92))) == "flavor");
  CHECK(std::string(view::optionText(*fixture, *fixture->card(92), 0)) == "Go");
}

void playsTheRealCards() {
  auto cards = std::make_unique<Cards>();
  const std::string gods(kGodsJson);
  const std::string json(kCardsJson);
  const char* why = nullptr;
  CHECK(CardsReader::read(*cards, CardsReader::File::Gods, gods.data(), gods.size(), &why));
  CHECK(CardsReader::read(*cards, CardsReader::File::Cards, json.data(), json.size(), &why));
  if (why) std::printf("  load: %s\n", why);
  CHECK(rulesFit(*cards, &why));
  CHECK(cards->count() == 118 && cards->godCount() == 7);
  int options = 0;
  for (int i = 0; i < cards->count(); ++i) options += cards->at(i).optionCount;
  CHECK(options == 278);
  std::printf("  real cards: %d cards, %d options, %zu of %zu text bytes\n", cards->count(), options, cards->textUsed(),
              kTextBytes);

  // Every card can reach the deck: a walk along the shuffle-ins from all that
  // the engine deals itself, with every god summoned.
  {
    std::set<int> roots = {ids::kTutorial, ids::kRumors, ids::kGreed, ids::kPoliceRaid, ids::kDesperate};
    for (const ids::Tier& tier : ids::kTiers) roots.insert(tier.card);
    for (int i = 0; i < cards->count(); ++i) {
      if (cards->at(i).initial) roots.insert(cards->at(i).id);
    }
    for (int gd = 0; gd < cards->godCount(); ++gd) {
      for (uint8_t id : cards->god(gd).unlock) roots.insert(id);
    }
    std::set<int> reached;
    std::vector<int> todo(roots.begin(), roots.end());
    while (!todo.empty()) {
      const int id = todo.back();
      todo.pop_back();
      if (!reached.insert(id).second) continue;
      const Card& c = *cards->card(id);
      for (int k = 0; k < c.optionCount; ++k) {
        const Option& o = c.option[k];
        for (int a = 0; a < o.addCount; ++a) todo.push_back(o.add[a].card);
        for (int r = o.rollLow; o.rollCount && r <= o.rollHigh; ++r) todo.push_back(r);
      }
    }
    CHECK(static_cast<int>(reached.size()) == cards->count());
  }

  const Stats real = campaign(*cards, false);
  std::printf("  bot: %d runs, %d won (%.1f%%), avg %.1f turns, %d hit the turn cap\n", real.runs, real.won,
              100.0 * real.won / real.runs, static_cast<double>(real.turns) / real.runs, real.capped);
  std::printf("  losses: by choice %d, stuck %d, out of cards %d; most cards in play %d\n", real.losses[1],
              real.losses[2], real.losses[3], real.maxPile);
  std::printf("  runs ended by a choice on:");
  for (int id = 1; id < 256; ++id) {
    if (real.killedBy[id]) std::printf(" %s=%d", cards->text(cards->card(id)->title), real.killedBy[id]);
  }
  std::printf("\n");

  // The explorer's hand is dealt afresh every turn, so it reaches states a
  // careful player avoids. Every god and every card must turn up.
  const Stats wide = campaign(*cards, true);
  std::printf("  explorer: %d runs; wins by god:", wide.runs);
  for (int gd = 0; gd < cards->godCount(); ++gd) std::printf(" %s=%d", cards->text(cards->god(gd).name), wide.wins[gd]);
  std::printf("; cards drawn %zu of %d\n", wide.seen.size(), cards->count());
  for (int gd = 0; gd < cards->godCount(); ++gd) CHECK(wide.wins[gd] > 0);
  CHECK(static_cast<int>(wide.seen.size()) == cards->count());
  if (static_cast<int>(wide.seen.size()) != cards->count()) {
    std::printf("  never drawn:");
    for (int i = 0; i < cards->count(); ++i) {
      if (!wide.seen.count(cards->at(i).id)) std::printf(" %d", cards->at(i).id);
    }
    std::printf("\n");
  }
}

}  // namespace

static_assert(std::is_trivially_copyable<Game>::value, "a Game is saved by writing its bytes");
static_assert(std::is_trivially_copyable<Profile>::value, "a Profile is saved by writing its bytes");

#define RUN(test)               \
  do {                          \
    std::printf("%s\n", #test); \
    test();                     \
  } while (0)

int main() {
  // Unbuffered, so a hang or a crash shows which test it was in.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  RUN(loadsTheOriginalShape);
  RUN(refusesWhatItCannotPlay);
  RUN(reshuffleIsWeightedAndPicksToTheBottom);
  RUN(punishmentsFollowTheOriginalOdds);
  RUN(punishmentOddsAreWhatIsRolled);
  RUN(drawnCardsResolveHalvesAndRolls);
  RUN(paymentIsExactWithRelicsAsWildcards);
  RUN(losingOptionsAreLockedWhileAnotherCanBePaid);
  RUN(choosingPaysGainsAndShufflesIn);
  RUN(foresightShowsTheTopAndDiscardsByPosition);
  RUN(startDealsTheOriginalDeck);
  RUN(profileRemembersWins);
  RUN(savesAreCheckedBeforeTheyAreTrusted);
  RUN(waysToPayAreEveryExactPayment);
  RUN(lastFoodIsWorthARelic);
  RUN(savesRoundTripAndRefuseDamage);
  RUN(rngIsUniformAndRepeatable);

  RUN(theScreenSaysWhatHappened);
  RUN(playsTheRealCards);
  if (const char* dir = std::getenv("UNDERHAND_DATA")) {
    std::printf("embeddedCardsAreTheOriginalFiles\n");
    embeddedCardsAreTheOriginalFiles(dir);
  } else {
    std::printf("embedded cards not compared with the APK: set UNDERHAND_DATA to use its two JSON files\n");
  }

  std::printf("%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
