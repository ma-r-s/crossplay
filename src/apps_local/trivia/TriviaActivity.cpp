#include "TriviaActivity.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstdlib>
#include <cstring>

#include "../../components/UITheme.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxTheme.h"

namespace fui = freeink::ui;

namespace {

constexpr const char* kPackPath = "/trivia/pack.dat";
constexpr const char* kStatePath = "/trivia/pack.state";

// A ByteSource over a HalFile. Every read seeks first: an index entry and the
// record it points at are in different places, so sequential reads are the
// exception rather than the rule.
class FileSource final : public trivia::WritableByteSource {
 public:
  void attach(HalFile& file) {
    file_ = &file;
    size_ = static_cast<uint32_t>(file.size());
  }
  bool read(const uint32_t offset, void* dst, const uint32_t length) override {
    if (length == 0) return true;
    if (file_ == nullptr || offset > size_ || offset + length > size_) return false;
    if (!file_->seekSet(offset)) return false;
    return file_->read(dst, length) == static_cast<int>(length);
  }
  bool write(const uint32_t offset, const void* src, const uint32_t length) override {
    if (length == 0) return true;
    if (file_ == nullptr || offset + length > size_) return false;   // state never grows
    if (!file_->seekSet(offset)) return false;
    return file_->write(static_cast<const uint8_t*>(src), length) == length;
  }
  bool flush() override {
    if (file_ != nullptr) file_->flush();
    return true;
  }
  uint32_t size() const override { return size_; }

 private:
  HalFile* file_ = nullptr;
  uint32_t size_ = 0;
};

HalFile g_packFile;
HalFile g_stateFile;
FileSource g_packSource;
FileSource g_stateSource;

}  // namespace

std::unique_ptr<Activity> TriviaActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<TriviaActivity>(renderer, mappedInput);
}

bool TriviaActivity::ensureState(const uint32_t count) {
  g_stateFile = Storage.open(kStatePath, O_RDWR);
  if (!g_stateFile.isOpen() || static_cast<uint32_t>(g_stateFile.size()) < count) {
    // Missing, or shorter than the pack because the pack was replaced under it.
    // Rewriting loses which questions have been seen, which is the right trade:
    // reading a stale byte for a question it does not describe is worse.
    HalFile fresh;
    if (!Storage.openFileForWrite("TRIVIA", kStatePath, fresh)) {
      LOG_ERR("TRIVIA", "Cannot create %s", kStatePath);
      return false;
    }
    uint8_t zeros[256] = {};
    for (uint32_t written = 0; written < count;) {
      const uint32_t chunk = (count - written) < sizeof(zeros) ? (count - written) : sizeof(zeros);
      if (fresh.write(zeros, chunk) != chunk) {
        LOG_ERR("TRIVIA", "Short write creating %s", kStatePath);
        return false;
      }
      written += chunk;
    }
    fresh.flush();
    fresh.close();
    g_stateFile = Storage.open(kStatePath, O_RDWR);
  }
  if (!g_stateFile.isOpen()) return false;
  g_stateSource.attach(g_stateFile);
  return state_.open(g_stateSource, count);
}

bool TriviaActivity::openPack() {
  if (!Storage.openFileForRead("TRIVIA", kPackPath, g_packFile)) {
    LOG_ERR("TRIVIA", "No pack at %s", kPackPath);
    return false;
  }
  g_packSource.attach(g_packFile);
  if (!pack_.open(g_packSource)) {
    LOG_ERR("TRIVIA", "%s is not a readable trivia pack", kPackPath);
    return false;
  }
  if (!ensureState(pack_.count())) return false;
  chooser_.begin(pack_, state_, rng_);
  return true;
}

void TriviaActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);

  rng_.seed(static_cast<uint32_t>(millis()) | 1u);

  if (openPack()) {
    view_ = View::Menu;
    LOG_INF("TRIVIA", "Pack open: %u questions", static_cast<unsigned>(pack_.count()));
  } else {
    view_ = View::NoPack;
  }

#if !defined(FREEINK_NET_WOLFSSL)
  // Render harness for the three arrangements of the question screen, so the
  // choice is made by looking. Simulator-only, by the same gate Study's
  // sync-flow preview uses. Goes away when one is chosen.
  if (const char* forced = std::getenv("TRIVIA_LAYOUT")) {
    if (std::strcmp(forced, "page") == 0) layout_ = triviaui::QuestionLayout::Page;
    if (std::strcmp(forced, "stage") == 0) layout_ = triviaui::QuestionLayout::Stage;
    if (std::strcmp(forced, "card") == 0) layout_ = triviaui::QuestionLayout::Card;
    if (view_ != View::NoPack) {
      view_ = View::Quizmaster;
      deal();
    }
  }
#endif
  flashOnNextPaint_ = true;
  requestUpdate();
}

void TriviaActivity::onExit() {
  g_packFile.close();
  g_stateFile.close();
  Activity::onExit();
}

void TriviaActivity::go(const View next) {
  view_ = next;
  interactionsReady_ = false;
  flashOnNextPaint_ = true;      // a mode change is a page turn, so spend the flash
  requestUpdate();
}

void TriviaActivity::deal() {
  const bool solo = view_ == View::Solo;
  revealed_ = false;
  chosen_ = -1;
  haveQuestion_ = false;

  uint32_t index = 0;
  if (!chooser_.next(index, solo, difficulty_)) {
    LOG_ERR("TRIVIA", "No question available (difficulty %d, solo %d)", difficulty_, solo ? 1 : 0);
    return;
  }
  if (!pack_.read(index, question_)) {
    LOG_ERR("TRIVIA", "Unreadable record %u", static_cast<unsigned>(index));
    return;
  }
  current_ = index;
  haveQuestion_ = true;
  state_.setFlag(index, trivia::kSeen);
  if (solo) trivia::buildChoices(question_, rng_, choices_);
  requestUpdate();
}

void TriviaActivity::routeAction(const int action, const int value) {
  switch (action) {
    case triviaui::ActionMenuRow:
      selected_ = value;
      if (value == 0) {
        go(View::Quizmaster);
        deal();
      } else if (value == 1) {
        score_.reset();
        go(View::Solo);
        deal();
      } else {
        difficulty_ = (difficulty_ + 1) % (trivia::kDifficulties + 1);
        requestUpdate();
      }
      break;
    case triviaui::ActionReveal:
      revealed_ = true;
      requestUpdate();
      break;
    case triviaui::ActionNext:
      deal();
      break;
    case triviaui::ActionOption:
      if (chosen_ < 0 && value >= 0 && value < trivia::kOptions) {
        chosen_ = value;
        score_.record(value == choices_.correct);
        requestUpdate();
      }
      break;
    case triviaui::ActionFlag:
      // The whole curation loop: a question judged bad at a bar is never served
      // again, and the index is read back off the card into verdicts.tsv.
      if (haveQuestion_) {
        state_.setFlag(current_, trivia::kFlagged);
        LOG_INF("TRIVIA", "Flagged question %u", static_cast<unsigned>(current_));
        deal();
      }
      break;
    default:
      break;
  }
}

void TriviaActivity::loop() {
  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // Touch only, like the rest of this fork's games. The app never blocks the
  // loop, so it must not call mappedInput.update(): the shell already did.
  if (!input.touchReleased || !interactionsReady_) return;

  const fui::ActionEvent action = interactions_.route(input);
  routeAction(static_cast<int>(action.action), static_cast<int>(action.value));
}

void TriviaActivity::render(RenderLock&&) {
  renderer.clearScreen();

  fui::GfxRendererTarget target = toybox::makeTarget(renderer, toybox::readingChromeFaces());
  const fui::InputSnapshot noInput{};
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions_);
  toybox::Screen screen(frame);

  switch (view_) {
    case View::NoPack: {
      triviaui::MenuModel model;
      model.packMissing = true;
      triviaui::buildMenu(screen, model);
      break;
    }
    case View::Menu: {
      triviaui::MenuModel model;
      model.selected = selected_;
      model.difficulty = difficulty_;
      model.packCount = pack_.count();
      triviaui::buildMenu(screen, model);
      break;
    }
    case View::Quizmaster: {
      triviaui::QuestionModel model;
      model.layout = layout_;
      if (haveQuestion_) {
        model.clue = question_.clue();
        model.difficulty = question_.difficulty();
        if (revealed_) model.answer = question_.answer();
      } else {
        model.clue = "No question available. Try another difficulty.";
      }
      triviaui::buildQuestion(screen, model);
      break;
    }
    case View::Solo: {
      triviaui::ChoiceModel model;
      model.asked = score_.asked;
      model.right = score_.right;
      model.chosen = chosen_;
      if (haveQuestion_) {
        model.clue = question_.clue();
        model.difficulty = question_.difficulty();
        model.correct = choices_.correct;
        for (int i = 0; i < trivia::kOptions; ++i) model.option[i] = choices_.option[i];
      } else {
        model.clue = "No multiple-choice question available at this difficulty.";
      }
      triviaui::buildChoice(screen, model);
      break;
    }
  }

  interactionsReady_ = true;
  toybox::reportOverflow(interactions_, "Trivia");

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer(flashOnNextPaint_ ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  flashOnNextPaint_ = false;
}
