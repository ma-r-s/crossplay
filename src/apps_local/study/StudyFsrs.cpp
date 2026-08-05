#include "StudyFsrs.h"

#include <cmath>

namespace study {

// Anki's published FSRS-5 defaults. Only used for a deck that arrives without
// its own optimized parameters.
const float kDefaultParams[kNumParams] = {0.40255f, 1.18385f, 3.173f,  15.69105f, 7.1949f, 0.5345f, 1.4604f,
                                          0.0046f,  1.54575f, 0.1192f, 1.01925f,  1.9395f, 0.11f,   0.29605f,
                                          2.2698f,  0.2315f,  2.9898f, 0.51655f,  0.6621f};

namespace {

// The FSRS-5 forgetting curve is a power function, not an exponential:
//   R(t) = (1 + FACTOR * t / S) ^ DECAY
// DECAY is fixed in FSRS-5 (FSRS-6 learns it as a 20th weight). FACTOR is
// derived so that R(S) is exactly 0.9 -- that is what makes stability mean
// "days until 90% recall" rather than an arbitrary scale.
constexpr float kDecay = -0.5f;
const float kFactor = std::pow(0.9f, 1.0f / kDecay) - 1.0f;  // 19/81

// Anki clamps stability into this band before storing it.
constexpr float kMinStability = 0.01f;
constexpr float kMaxStability = 36500.0f;

float clampf(const float v, const float lo, const float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int ratingIndex(const Rating r) { return static_cast<int>(r); }

}  // namespace

Fsrs::Fsrs(const float* params, const float desiredRetention)
    : w_(params != nullptr ? params : kDefaultParams), desiredRetention_(desiredRetention) {}

float Fsrs::initialStability(const Rating r) const {
  // w[0..3] are the stabilities the four buttons give a brand-new card.
  return clampf(w_[ratingIndex(r) - 1], kMinStability, kMaxStability);
}

float Fsrs::initialDifficulty(const Rating r) const {
  return clampf(w_[4] - std::exp(w_[5] * static_cast<float>(ratingIndex(r) - 1)) + 1.0f, 1.0f, 10.0f);
}

float Fsrs::nextDifficulty(const float d, const Rating r) const {
  // Two steps Anki applies in order. Linear damping shrinks the change as
  // difficulty approaches its 10 ceiling, so a wall of "Again"s saturates
  // smoothly instead of slamming into the clamp.
  const float delta = -w_[6] * static_cast<float>(ratingIndex(r) - 3);
  const float damped = d + delta * ((10.0f - d) / 9.0f);
  // Mean reversion pulls difficulty back towards what an "Easy" first answer
  // would have produced, which is what stops it ratcheting up forever.
  const float reverted = w_[7] * initialDifficulty(Rating::Easy) + (1.0f - w_[7]) * damped;
  return clampf(reverted, 1.0f, 10.0f);
}

float Fsrs::shortTermStability(const float s, const Rating r) const {
  // Same-day review. The multiplier is below 1 for Again/Hard and above it for
  // Good/Easy; the clamps stop a badly-fit parameter set inverting that, which
  // is a rule Anki applies and which two of Mario's cards depend on.
  float inc = std::exp(w_[17] * (static_cast<float>(ratingIndex(r)) - 3.0f + w_[18]));
  if (ratingIndex(r) >= 3) {
    if (inc < 1.0f) inc = 1.0f;
  } else {
    if (inc > 1.0f) inc = 1.0f;
  }
  return s * inc;
}

float Fsrs::recallStability(const float d, const float s, const float retr, const Rating r) const {
  const float hardPenalty = (r == Rating::Hard) ? w_[15] : 1.0f;
  const float easyBonus = (r == Rating::Easy) ? w_[16] : 1.0f;
  // Growth is largest when the card was nearly forgotten (low retr) and least
  // when it was answered while still fresh -- the spacing effect, made explicit.
  return s * (1.0f + std::exp(w_[8]) * (11.0f - d) * std::pow(s, -w_[9]) * (std::exp((1.0f - retr) * w_[10]) - 1.0f) *
                         hardPenalty * easyBonus);
}

float Fsrs::forgetStability(const float d, const float s, const float retr) const {
  return w_[11] * std::pow(d, -w_[12]) * (std::pow(s + 1.0f, w_[13]) - 1.0f) * std::exp((1.0f - retr) * w_[14]);
}

float Fsrs::retrievability(const Memory& m, const float elapsedDays) const {
  if (!m.learned || m.stability <= 0.0f) return 1.0f;
  const float t = elapsedDays < 0.0f ? 0.0f : elapsedDays;
  return std::pow(1.0f + kFactor * t / m.stability, kDecay);
}

Memory Fsrs::review(const Memory& m, const Rating rating, const int elapsedDays) const {
  Memory out;
  out.learned = true;

  if (!m.learned) {
    out.stability = initialStability(rating);
    out.difficulty = initialDifficulty(rating);
    return out;
  }

  if (elapsedDays <= 0) {
    out.stability = shortTermStability(m.stability, rating);
  } else {
    const float retr = retrievability(m, static_cast<float>(elapsedDays));
    if (rating == Rating::Again) {
      const float forgotten = forgetStability(m.difficulty, m.stability, retr);
      // A lapse must never be worth more than not lapsing.
      out.stability = forgotten < m.stability ? forgotten : m.stability;
    } else {
      out.stability = recallStability(m.difficulty, m.stability, retr, rating);
    }
  }

  out.stability = clampf(out.stability, kMinStability, kMaxStability);
  out.difficulty = nextDifficulty(m.difficulty, rating);
  return out;
}

int Fsrs::intervalDays(const Memory& m) const {
  if (!m.learned || m.stability <= 0.0f) return 1;
  // Invert the forgetting curve: how long until R falls to the target.
  const float days = (m.stability / kFactor) * (std::pow(desiredRetention_, 1.0f / kDecay) - 1.0f);
  const int rounded = static_cast<int>(days + 0.5f);
  if (rounded < 1) return 1;
  return rounded > maximumInterval_ ? maximumInterval_ : rounded;
}

void Fsrs::previewIntervals(const Memory& m, const int elapsedDays, int out[4]) const {
  for (int i = 0; i < 4; ++i) {
    const Rating r = static_cast<Rating>(i + 1);
    out[i] = intervalDays(review(m, r, elapsedDays));
  }
}

}  // namespace study
