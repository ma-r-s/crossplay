// The learning-step state machine, driven through every transition Anki has.
//
// The bug this suite exists to prevent shipped once: a mature card's Again
// button offered 15 days, because a lapse jumped straight to the interval the
// card gets *after* relearning. A user pressing Again is saying "I forgot
// this"; the honest answer is ten minutes.

#include <cstdio>
#include <cstring>

#include "../../src/apps_local/study/StudyScheduler.h"

namespace {

int failures = 0;
int checks = 0;

void check(const bool ok, const char* what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("  FAIL: %s\n", what);
  }
}

// Mario's deck: learn 1m then 10m, relearn 10m, read out of his Anki preset.
study::Steps marioSteps() {
  study::Steps s;
  s.learn[0] = 1.0f;
  s.learn[1] = 10.0f;
  s.learnCount = 2;
  s.relearn[0] = 10.0f;
  s.relearnCount = 1;
  return s;
}

study::CardState newCard() {
  study::CardState c;
  c.ankiCardId = 1;
  c.state = static_cast<uint8_t>(study::State::New);
  return c;
}

study::CardState matureCard() {
  study::CardState c;
  c.ankiCardId = 2;
  c.state = static_cast<uint8_t>(study::State::Review);
  c.stability = 40.0f;
  c.difficulty = 5.0f;
  c.reps = 9;
  c.dueDay = 100;
  c.lastReviewDay = 60;
  return c;
}

constexpr int kToday = 100;
constexpr int kNoon = 12 * 60;

void testNewCardWalksTheSteps() {
  const study::Fsrs fsrs;
  const study::Scheduler sched(fsrs, marioSteps());
  const study::CardState card = newCard();

  const study::Outcome again = sched.answer(card, study::Rating::Again, kToday, kNoon);
  check(again.delayMinutes == 1, "new + Again lands on the first step (1m)");
  check(again.card.state == static_cast<uint8_t>(study::State::Learning), "new + Again is Learning");
  check(again.card.stepIndex == 0, "new + Again sits at step 0");
  check(again.intervalDays == 0, "new + Again does not schedule a day");
  check(again.card.dueMinute == kNoon + 1, "due a minute from now");

  const study::Outcome good = sched.answer(card, study::Rating::Good, kToday, kNoon);
  check(good.delayMinutes == 10, "new + Good skips to the second step (10m)");
  check(good.card.stepIndex == 1, "new + Good sits at step 1");

  const study::Outcome easy = sched.answer(card, study::Rating::Easy, kToday, kNoon);
  check(easy.delayMinutes == 0, "new + Easy leaves the step list");
  check(easy.card.state == static_cast<uint8_t>(study::State::Review), "new + Easy is Review");
  check(easy.intervalDays >= 1, "new + Easy schedules a real day");
  check(easy.graduated, "new + Easy graduated");

  // Answering the last step with Good is what graduates a card.
  study::CardState onLastStep = good.card;
  const study::Outcome out = sched.answer(onLastStep, study::Rating::Good, kToday, kNoon + 10);
  check(out.card.state == static_cast<uint8_t>(study::State::Review), "last step + Good graduates");
  check(out.intervalDays >= 1, "graduating schedules a day");
  check(out.delayMinutes == 0, "graduating does not schedule a minute");
}

void testLapseGoesToRelearning() {
  const study::Fsrs fsrs;
  const study::Scheduler sched(fsrs, marioSteps());
  const study::CardState card = matureCard();

  const study::Outcome again = sched.answer(card, study::Rating::Again, kToday, kNoon);
  // The whole point of this file.
  check(again.delayMinutes == 10, "mature + Again comes back in 10 minutes, not 15 days");
  check(again.intervalDays == 0, "mature + Again schedules no day");
  check(again.card.state == static_cast<uint8_t>(study::State::Relearning), "mature + Again is Relearning");
  check(again.card.lapses == card.lapses + 1, "mature + Again counts a lapse");
  check(again.card.stability < card.stability, "mature + Again costs stability");

  // The other three keep it in day-scale review.
  for (int r = 2; r <= 4; ++r) {
    const study::Outcome o = sched.answer(card, static_cast<study::Rating>(r), kToday, kNoon);
    check(o.card.state == static_cast<uint8_t>(study::State::Review), "mature + non-Again stays Review");
    check(o.intervalDays >= 1, "mature + non-Again schedules a day");
    check(o.card.lapses == card.lapses, "mature + non-Again counts no lapse");
  }

  // And relearning ends by going back to review.
  const study::Outcome back = sched.answer(again.card, study::Rating::Good, kToday, kNoon + 10);
  check(back.card.state == static_cast<uint8_t>(study::State::Review), "relearn + Good returns to Review");
  check(back.intervalDays >= 1, "returning schedules a day");

  const study::Outcome stuck = sched.answer(again.card, study::Rating::Again, kToday, kNoon + 10);
  check(stuck.card.state == static_cast<uint8_t>(study::State::Relearning), "relearn + Again stays Relearning");
  check(stuck.delayMinutes == 10, "relearn + Again repeats the step");

  // Hard inside a step list sits between this step and the next, or at 1.5x
  // when there is no next one. Mario's single 10-minute relearning step is the
  // second case, and Anki prints 15m for it.
  const study::Outcome hard = sched.answer(again.card, study::Rating::Hard, kToday, kNoon + 10);
  check(hard.delayMinutes == 15, "Hard on the last relearn step is 1.5x the step");

  // With two learning steps of 1m and 10m, Hard on the first is the midpoint.
  const study::Outcome newHard = sched.answer(newCard(), study::Rating::Hard, kToday, kNoon);
  check(newHard.delayMinutes == 6, "Hard on a step with a successor is the midpoint");
}

void testElapsedDaysReachFsrs() {
  const study::Fsrs fsrs;
  const study::Scheduler sched(fsrs, marioSteps());

  // Two identical cards, one last reviewed today and one forty days ago. FSRS
  // must treat them differently: the same-day path applies a flat multiplier,
  // the recall path weighs how nearly the card was forgotten. Conflating them
  // is not academic -- the converter shipped with lastReviewDay unset, so every
  // card's first review on the device took the same-day path and inflated its
  // interval. This is the assertion that would have caught it.
  study::CardState sameDay = matureCard();
  sameDay.lastReviewDay = kToday;
  study::CardState longAgo = matureCard();
  longAgo.lastReviewDay = kToday - 40;

  const study::Outcome a = sched.answer(sameDay, study::Rating::Good, kToday, kNoon);
  const study::Outcome b = sched.answer(longAgo, study::Rating::Good, kToday, kNoon);
  check(a.card.stability != b.card.stability, "elapsed days change the outcome");
  check(b.card.stability > a.card.stability, "a card recalled after a long gap gains more stability");

  // And a card that has never been reviewed must not pretend it was.
  study::CardState never = matureCard();
  never.lastReviewDay = -1;
  const study::Outcome c = sched.answer(never, study::Rating::Good, kToday, kNoon);
  check(c.card.lastReviewDay == kToday, "reviewing stamps the day");
}

void testButtonsAreOrdered() {
  const study::Fsrs fsrs;
  const study::Scheduler sched(fsrs, marioSteps());
  study::Outcome out[4];

  // Whatever state the card is in, the four buttons must run shortest to
  // longest, or the labels are lying about what they do.
  const study::CardState cards[] = {newCard(), matureCard()};
  for (const study::CardState& card : cards) {
    sched.preview(card, kToday, kNoon, out);
    long previous = -1;
    for (int i = 0; i < 4; ++i) {
      // Compare on one axis: minutes, with days folded in at 1440 each.
      const long total = out[i].delayMinutes > 0 ? out[i].delayMinutes : out[i].intervalDays * 1440L;
      check(total > 0, "every button leads somewhere");
      check(total >= previous, "buttons run shortest to longest");
      previous = total;
    }
  }
}

void testDueness() {
  study::CardState learning;
  learning.state = static_cast<uint8_t>(study::State::Learning);
  learning.dueDay = kToday;
  learning.dueMinute = kNoon;

  check(!study::Scheduler::isDue(learning, kToday, kNoon - 1), "a learning card is not due a minute early");
  check(study::Scheduler::isDue(learning, kToday, kNoon), "a learning card is due on the minute");
  check(study::Scheduler::isDue(learning, kToday, kNoon + 5), "a learning card stays due once passed");
  check(study::Scheduler::isDue(learning, kToday + 1, 0), "yesterday's learning card is due today");
  check(!study::Scheduler::isDue(learning, kToday - 1, 1439), "tomorrow's learning card is not due yet");

  study::CardState review;
  review.state = static_cast<uint8_t>(study::State::Review);
  review.dueDay = kToday;
  check(study::Scheduler::isDue(review, kToday, 0), "a review card is due all day");
  check(!study::Scheduler::isDue(review, kToday - 1, 1439), "a future review card is not due");

  check(study::Scheduler::isDue(newCard(), 0, 0), "a new card is always available");

  // A card Anki has suspended is never due, whatever its dates say. Meeting
  // one here that the phone has suspended is the divergence that makes a user
  // stop trusting the sync.
  study::CardState suspended = matureCard();
  suspended.state = static_cast<uint8_t>(study::State::Suspended);
  suspended.dueDay = 0;  // long overdue, and still must not appear
  check(!study::Scheduler::isDue(suspended, kToday, kNoon), "a suspended card is never due");
}

void testStepDelaysCrossMidnight() {
  const study::Fsrs fsrs;
  const study::Scheduler sched(fsrs, marioSteps());
  // 11:55pm plus a ten-minute step lands on the next day, which the day/minute
  // pair has to carry or the card comes back a whole day late.
  const study::Outcome out = sched.answer(matureCard(), study::Rating::Again, kToday, 1435);
  check(out.card.dueDay == kToday + 1, "a step past midnight rolls the day");
  check(out.card.dueMinute == 5, "and keeps the right minute");
}

void testFormatting() {
  char buf[16];
  study::formatDelay(1, 0, buf, sizeof(buf));
  check(std::strcmp(buf, "1m") == 0, "1 minute reads as 1m");
  study::formatDelay(10, 0, buf, sizeof(buf));
  check(std::strcmp(buf, "10m") == 0, "10 minutes reads as 10m");
  study::formatDelay(90, 0, buf, sizeof(buf));
  check(std::strcmp(buf, "2h") == 0, "90 minutes reads as 2h");
  study::formatDelay(0, 5, buf, sizeof(buf));
  check(std::strcmp(buf, "5d") == 0, "5 days reads as 5d");
  study::formatDelay(0, 60, buf, sizeof(buf));
  check(std::strcmp(buf, "2.0mo") == 0, "60 days reads in months");
  study::formatDelay(0, 730, buf, sizeof(buf));
  check(std::strcmp(buf, "2.0y") == 0, "730 days reads in years");
}

void testNoStepsGraduatesImmediately() {
  // A preset with the step lists emptied must not trap a card in a list that
  // has nowhere to go.
  study::Steps none;
  const study::Fsrs fsrs;
  const study::Scheduler sched(fsrs, none);

  const study::Outcome out = sched.answer(newCard(), study::Rating::Again, kToday, kNoon);
  check(out.card.state == static_cast<uint8_t>(study::State::Review), "no steps: a new card goes straight to Review");
  check(out.intervalDays >= 1, "no steps: it still gets a real interval");

  const study::Outcome lapse = sched.answer(matureCard(), study::Rating::Again, kToday, kNoon);
  check(lapse.card.state == static_cast<uint8_t>(study::State::Review), "no steps: a lapse stays in Review");
  check(lapse.card.lapses == matureCard().lapses + 1, "no steps: a lapse is still counted");
}

}  // namespace

int main() {
  std::printf("StudyScheduler\n");
  testNewCardWalksTheSteps();
  testLapseGoesToRelearning();
  testElapsedDaysReachFsrs();
  testButtonsAreOrdered();
  testDueness();
  testStepDelaysCrossMidnight();
  testFormatting();
  testNoStepsGraduatesImmediately();
  std::printf("%s %d checks, %d failed\n", failures == 0 ? "PASS" : "FAIL", checks, failures);
  return failures == 0 ? 0 : 1;
}
