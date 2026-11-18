#pragma once

// What each input does to a card on the review screen, now that a card can be
// longer than one screen.
//
// Freestanding, like StudyText.h: the activity classifies a press, a swipe or
// a tap into a CardInput and applies the CardOutcome, and everything in
// between is here, where host-tests/study can walk every state. The rules:
//
//   - The side page keys turn the page. On the last page of the question the
//     next press shows the answer, opened on the page where the answer's own
//     text begins (the face repeats the question above it, and paging back
//     reaches that); a one-page question shows it on either key. On the
//     answer they only ever turn pages.
//   - A swipe and the pager's arrows only ever turn pages, on either face.
//   - Grading is a tap on the rating cells and nothing else: no page turn, at
//     the end of the answer or anywhere, ever grades a card.
//   - Taps elsewhere keep their old meaning: the question reveals, the answer
//     opens the card's photograph when it has one.
//
// The page-turn rule is adapted from CrossNext (github.com/manuelx900/CrossNext,
// MIT), a fork built from this one.

#include <cstdint>

namespace study {

enum class CardInput : uint8_t {
  KeyNext,        // the page-forward side key (Button::PageForward)
  KeyPrevious,    // the page-back side key (Button::PageBack)
  SwipeNext,      // a swipe up: the page moves the way the finger does
  SwipePrevious,  // a swipe down
  PagerNext,      // the pager's right arrow
  PagerPrevious,  // the pager's left arrow
  Pager,          // the pager between its arrows: taken, and does nothing
  Body,           // anywhere else above the footer
  Footer,         // the footer; which quarter of it is `footerSlot`
};

struct CardView {
  bool answer = false;
  int page = 0;
  int pages = 1;
  bool canUndo = false;   // UNDO holds the footer's first quarter on the question
  bool hasPhoto = false;  // the answer's body opens the photograph
};

enum class CardAction : uint8_t { None, Turn, Reveal, Undo, Grade, Photo };

// The page a reveal asks for: wherever the answer's own text begins, which
// only the paint that lays the answer out can know (StudyText.h TextFlow).
inline constexpr int kAnswerStartPage = -1;

struct CardOutcome {
  CardAction action = CardAction::None;
  int page = 0;    // the page to show afterwards, or kAnswerStartPage
  int rating = 0;  // 1..4 (Again..Easy) when the action is Grade
};

// The page actually shown for a held page: kAnswerStartPage resolves to where
// the answer begins, and anything else is capped to the pages the face has.
// The paint draws by it and the loop routes by it, so the two cannot differ.
inline int resolveCardPage(const int held, const int answerStart, const int pages) {
  const int last = pages > 1 ? pages - 1 : 0;
  const int page = held == kAnswerStartPage ? answerStart : held;
  return page < 0 ? 0 : (page > last ? last : page);
}

inline CardOutcome routeCardInput(const CardView& state, const CardInput input, const int footerSlot = 0) {
  const int last = state.pages > 1 ? state.pages - 1 : 0;
  const int page = state.page < 0 ? 0 : (state.page > last ? last : state.page);
  CardOutcome out;
  out.page = page;
  const auto turn = [&](const int to) {
    if (to < 0 || to > last || to == page) return;
    out.action = CardAction::Turn;
    out.page = to;
  };
  const auto reveal = [&]() {
    out.action = CardAction::Reveal;
    out.page = kAnswerStartPage;
  };

  switch (input) {
    case CardInput::KeyNext:
      if (page < last) {
        turn(page + 1);
      } else if (!state.answer) {
        reveal();
      }
      break;
    case CardInput::KeyPrevious:
      if (page > 0) {
        turn(page - 1);
      } else if (!state.answer && last == 0) {
        reveal();
      }
      break;
    case CardInput::SwipeNext:
    case CardInput::PagerNext:
      turn(page + 1);
      break;
    case CardInput::SwipePrevious:
    case CardInput::PagerPrevious:
      turn(page - 1);
      break;
    case CardInput::Pager:
      break;
    case CardInput::Body:
      if (!state.answer) {
        reveal();
      } else if (state.hasPhoto) {
        out.action = CardAction::Photo;
      }
      break;
    case CardInput::Footer:
      if (!state.answer) {
        if (state.canUndo && footerSlot == 0) {
          out.action = CardAction::Undo;
        } else {
          reveal();
        }
      } else {
        out.action = CardAction::Grade;
        out.rating = (footerSlot < 0 ? 0 : (footerSlot > 3 ? 3 : footerSlot)) + 1;
      }
      break;
  }
  return out;
}

}  // namespace study
