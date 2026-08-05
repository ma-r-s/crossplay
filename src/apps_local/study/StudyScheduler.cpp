#include "StudyScheduler.h"

#include <cstdio>

namespace study {

namespace {

constexpr int kMinutesPerDay = 1440;

int stepMinutes(const float* steps, const uint8_t count, const int index) {
  if (count == 0) return 0;
  const int clamped = index < 0 ? 0 : (index >= count ? count - 1 : index);
  const int minutes = static_cast<int>(steps[clamped] + 0.5f);
  return minutes < 1 ? 1 : minutes;
}

}  // namespace

Steps Steps::defaults() {
  Steps s;
  // Anki's out-of-the-box preset. Mario's deck ships 1m/10m and 10m, which the
  // converter reads out of the collection, so these are only a fallback.
  s.learn[0] = 1.0f;
  s.learn[1] = 10.0f;
  s.learnCount = 2;
  s.relearn[0] = 10.0f;
  s.relearnCount = 1;
  return s;
}

bool Scheduler::isDue(const CardState& card, const int today, const int nowMinute) {
  const State state = static_cast<State>(card.state);
  if (state == State::New) return true;
  if (state == State::Learning || state == State::Relearning) {
    // A learning card carries a minute-of-day as well as a day, because "come
    // back in ten minutes" cannot be expressed in Anki's day numbering.
    if (card.dueDay != today) return card.dueDay < today;
    return card.dueMinute <= nowMinute;
  }
  return card.dueDay <= today;
}

Outcome Scheduler::answer(const CardState& card, const Rating rating, const int today, const int nowMinute) const {
  Outcome out;
  out.card = card;

  // FSRS sees every answer, including the ones that keep the card inside a step
  // list. That is what Anki does, and it is why a card that took four attempts
  // to learn ends up with a lower stability than one that took a single Good.
  const Memory before = card.memory();
  const int elapsed = card.lastReviewDay < 0 ? 0 : today - card.lastReviewDay;
  const Memory after = fsrs_->review(before, rating, elapsed);
  out.card.setMemory(after);
  out.card.lastReviewDay = today;
  if (out.card.reps < 0xFFFF) ++out.card.reps;

  const State state = static_cast<State>(card.state);
  const bool wasReview = state == State::Review;
  const bool inRelearn = state == State::Relearning;
  const bool inLearn = state == State::Learning || state == State::New;

  const float* list = (inRelearn || (wasReview && rating == Rating::Again)) ? steps_.relearn : steps_.learn;
  const uint8_t count = (inRelearn || (wasReview && rating == Rating::Again)) ? steps_.relearnCount : steps_.learnCount;

  // Which step list, and where in it, the card lands.
  int nextStep = -1;  // -1 means "graduate to day-scale review"
  if (wasReview) {
    // A review card only leaves day-scale scheduling by being forgotten.
    if (rating == Rating::Again) {
      if (out.card.lapses < 0xFFFF) ++out.card.lapses;
      nextStep = count > 0 ? 0 : -1;
    }
  } else if (inRelearn || inLearn) {
    switch (rating) {
      case Rating::Again:
        nextStep = count > 0 ? 0 : -1;
        break;
      case Rating::Hard:
        // Hard repeats the step rather than advancing. A new card answered
        // Hard has not been seen before, so it repeats the first one.
        nextStep = count > 0 ? (state == State::New ? 0 : card.stepIndex) : -1;
        break;
      case Rating::Good:
        nextStep =
            (state == State::New) ? (count > 1 ? 1 : -1) : (card.stepIndex + 1 < count ? card.stepIndex + 1 : -1);
        break;
      case Rating::Easy:
        // Easy always leaves the step list, however many steps remain.
        nextStep = -1;
        break;
    }
  }

  if (nextStep < 0) {
    out.card.state = static_cast<uint8_t>(State::Review);
    out.card.stepIndex = 0;
    out.intervalDays = fsrs_->intervalDays(after);
    out.card.dueDay = today + out.intervalDays;
    out.card.dueMinute = 0;
    out.graduated = !wasReview;
    return out;
  }

  out.card.state = static_cast<uint8_t>((wasReview || inRelearn) ? State::Relearning : State::Learning);
  out.card.stepIndex = static_cast<uint8_t>(nextStep);
  out.delayMinutes = stepMinutes(list, count, nextStep);

  const int dueAt = nowMinute + out.delayMinutes;
  out.card.dueDay = today + dueAt / kMinutesPerDay;
  out.card.dueMinute = static_cast<uint16_t>(dueAt % kMinutesPerDay);
  return out;
}

void Scheduler::preview(const CardState& card, const int today, const int nowMinute, Outcome out[4]) const {
  for (int i = 0; i < 4; ++i) {
    out[i] = answer(card, static_cast<Rating>(i + 1), today, nowMinute);
  }
}

void formatDelay(const int minutes, const int days, char* out, const size_t size) {
  if (minutes > 0) {
    if (minutes < 60) {
      std::snprintf(out, size, "%dm", minutes);
    } else {
      std::snprintf(out, size, "%dh", (minutes + 30) / 60);
    }
    return;
  }
  if (days < 1) {
    std::snprintf(out, size, "1d");
  } else if (days < 31) {
    std::snprintf(out, size, "%dd", days);
  } else if (days < 365) {
    std::snprintf(out, size, "%.1fmo", static_cast<double>(days) / 30.4);
  } else {
    std::snprintf(out, size, "%.1fy", static_cast<double>(days) / 365.0);
  }
}

}  // namespace study
