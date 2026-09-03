#pragma once

#include <memory>

#include "../../components/UITheme.h"  // Rect
#include "../link/LinkActivity.h"
#include "../ui/ToyboxScreen.h"
#include "ChessCore.h"
#include "ChessEngine.h"
#include "ChessScreens.h"
#include "ChessWire.h"

// Chess board screen. Tap a piece, then tap a destination.
//
// Touch only, and that is the device rather than a preference: the X4 Pro has
// two buttons, both side page keys. This header used to promise "move a cursor
// with the D-pad and press Confirm twice", which was never possible here --
// left, right and confirm are unassigned pins. See docs/buttons.md.
//
// You play one colour, the engine answers as the other. Board input is ignored
// while it is thinking. The gear in the header opens the settings overlay.
class ChessActivity final : public linkplay::LinkActivity {
 public:
  ChessActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : linkplay::LinkActivity("Chess", renderer, mappedInput) {}
  ~ChessActivity() override = default;

  static std::unique_ptr<Activity> create(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;

 protected:
  // The multiplayer half, which LinkActivity drives. Everything else about a
  // match -- the screens, the notes, the rematch, the tick, the sleep
  // suppression -- lives there and is the same in every game.
  linkplay::PlayBase& linkState() override { return link; }
  const linkplay::PlayBase& linkState() const override { return link; }
  const char* linkGameTitle() const override { return "CHESS"; }
  const char* linkHeadline() const override;
  void onMatchStart(bool goesFirst) override;
  bool takeOpponentState() override;
  void onRematch() override;
  void onLinkEnded() override;
  bool matchGameOver() const override { return gameOver; }
  void onLinkPhaseChanged() override { refreshTurnLabel(); }
  void drawLinkArt(const Rect& slot) override { drawMiniBoard(slot); }
  void gameLoop() override;
  void gameRender() override;

 private:
  // Board placement, derived once and used by both painting and hit-testing so
  // the drawn squares and the tappable squares cannot drift apart.
  struct BoardGeometry {
    int originX;
    int originY;
    int squareSize;
  };
  BoardGeometry boardGeometry() const;

  // Board orientation. Your own pieces always sit at the bottom, so playing
  // Black mirrors the board rather than asking you to read it upside down.
  // Every square-to-screen mapping goes through these two, in both the drawing
  // and the hit test.
  int screenColumnOf(int square) const;
  int screenRowOf(int square) const;

  // Square under a point in logical screen coordinates, or -1 when outside.
  int squareAtPoint(int x, int y) const;
  void drawSquareContents(const BoardGeometry& geometry, int square) const;
  // Material one side has taken, drawn small in a strip. This is what turns the
  // slack above and below a width-constrained board from dead space into
  // information, which matters on a screen that sits showing one frame for hours.
  void drawCapturedStrip(int y, bool capturedFromBlack) const;
  // The running score sheet, in the band between the captures and the status
  // capsule. That band was dead space; a move list is the thing you actually
  // want to look at on a board you are staring at anyway.
  void drawMoveList(int top, int height) const;

  // --- settings overlay ---------------------------------------------------
  // Drawn in place rather than as its own Activity: leaving and re-entering
  // would round-trip the game through the save file and lose the selection for
  // no benefit.
  // The full catalogue. Which of these are on screen depends on the opponent:
  // Level and PlayAs mean nothing when two people share the device, and a menu
  // that shows inert rows is worse than one that shows fewer.
  // The row model lives in ChessScreens, where it is freestanding and host-
  // testable: which rows exist depends on the opponent, and that is a rule
  // worth asserting rather than eyeballing on a screen.
  using MenuRow = chessui::MenuRow;
  chessui::SettingsModel settingsModel() const;
  int visibleMenuRows() const { return chessui::visibleRows(settingsModel()); }
  MenuRow menuRowAt(const int visibleIndex) const { return chessui::rowAt(settingsModel(), visibleIndex); }
  Rect gearRect() const;
  void drawSettings();
  void routeSettings();
  void activateMenuRow(MenuRow row);
  // Closes the overlay, starting a new game only if a setting that cannot apply
  // mid-game was changed while it was open.
  void closeSettings();
  bool restartPending() const;

  // Undoes the last full move pair, so a blunder against the engine is not the
  // end of the game. Cheap because ChessCore already unmakes moves; the cost is
  // 8 bytes a ply to remember them.
  void takeBack();
  bool canTakeBack() const;

  void recordMove(const chess::Move& move);
  // Records, makes and remembers a move. Single path, so the SAN, the board and
  // the undo stack cannot disagree.
  void applyMove(const chess::Move& move);
  void resetGame();
  // Persisted so leaving the app and coming back resumes the same game. Stored
  // as FEN plus the SAN history, on the SD card next to the reader's own state.
  void saveGame() const;
  bool loadGame();
  // Preferences outlive a game, so they live in their own file: starting a new
  // game deletes the save and must not reset how you like to play.
  void saveSettings() const;
  void loadSettings();

  void refreshLegalMoves();
  // The repetition window: the key of every position since the last capture or
  // pawn move, oldest first, the position on the board last. Kept here rather
  // than derived from `history` because history holds SAN text and undo data,
  // and a resumed game has the first but not the second.
  void resetRepetition();
  void pushRepetitionKey();
  int currentRepetitionCount() const;
  bool isLegalDestination(int square) const;
  // Applies the selected-to-target move if one is legal. Promotion always takes
  // a queen for now; underpromotion needs a picker and is not worth a dialog on
  // e-ink until someone asks for it.
  bool tryMove(int from, int to);
  void handleSquareActivated(int square);
  const char* statusText() const;
  // "CALM FINCH'S MOVE". Built when the phase changes rather than inside
  // statusText(), which is const and has nowhere to put it.
  void refreshTurnLabel();
  const char* resultText() const;
  int statusPillY() const;
  // True when it is the engine's turn, whichever colour it is playing. Always
  // false when two people are sharing the device.
  bool engineToMove() const;
  bool vsComputer() const { return !linkRequested() && opponent == chessui::Opponent::Computer; }
  // Chess's word for it, kept because a dozen call sites read better with it.
  bool linkPlaying() const { return linkRequested(); }
  chessui::StartModel startModel() const;
  void drawStartMenu();
  // Refreshes what the menu says next to CONTINUE. Called on every route back to
  // the menu, or the row describes a position you left two games ago.
  void refreshContinueDetail();
  void goToMenu();
  // Draws the saved position into the menu's art slot. Its own routine rather
  // than the board's, because this one has no hints, no selection and no
  // cursor: it is a picture of the game, not a surface to play on.
  void drawMiniBoard(const Rect& slot) const;
  void routeStartMenu();
  void activateStartRow(chessui::StartRow row);
  // The single entry point for "the player asked for a new game". Every door
  // calls this rather than deciding for itself, because deciding for itself is
  // what three of them got wrong: solo it starts one, in a match it asks the
  // other device. A fourth door cannot pick the wrong branch, because there is
  // no branch left at the call site.
  void requestNewGame();
  // Leaving the board: save if solo, tell the opponent and stop the radio if in
  // a match, then the menu. Its own function because getting all three right
  // matters more than which key called it.
  void leaveBoard();

  void sendPosition();
  void adoptRemote(const ChessWire& wire);
  void recordRemoteMove(const char* san);
  // Which way up the board is drawn. Against the engine it follows your colour;
  // passing the device to a friend it follows whoever is to move, so the player
  // holding it always has their own pieces nearest them.
  bool whiteAtBottom() const;

  void playEngineMove();

  chess::Position position;
  // ~1KB. A member, not a local: ChessCore.h warns that a MoveList is far too
  // big for an ESP32-C3 stack frame, and activities are heap-allocated.
  chess::MoveList legalMoves;
  // ~5KB of per-ply move lists. Allocated once with the activity rather than on
  // the stack, for the same reason.
  chess::SearchBuffers searchBuffers;

  // 128 plies is a long game; beyond that the oldest are dropped and
  // historyBase keeps the printed move numbers honest.
  static constexpr int kMaxHistory = 128;
  char history[kMaxHistory][10] = {};
  // Parallel to `history`, for take-back. 8 bytes a ply against the ~70 a whole
  // Position would cost, which is what makes a full-game undo stack affordable.
  chess::Move historyMove[kMaxHistory] = {};
  chess::Undo historyUndo[kMaxHistory] = {};
  int historyCount = 0;
  int historyBase = 0;
  // The first ply whose undo data is real. A resumed game restores the SAN
  // list for the move sheet but not historyMove/historyUndo -- those cannot be
  // recovered from the save, which stores the position and the notation, not
  // the plies. Take-back therefore stops here rather than unmaking a
  // zero-initialised Move, which put the board back wrong: it moved nothing
  // (a1 to a1) while restoring castling = 0 and dropping a move from the sheet.
  int undoableFrom = 0;

  // 128 covers any window the fifty-move rule allows to exist (it draws at 100
  // halfmoves), so the shift below is a backstop rather than a normal path.
  static constexpr int kRepWindow = 128;
  uint64_t repKeys[kRepWindow] = {};
  int repCount = 0;
  // Why the game ended, when it ended in a draw. Checkmate and stalemate are
  // still read off the position; these two cannot be, because they are facts
  // about the game rather than about the board.
  enum class DrawReason : uint8_t { None, Repetition, FiftyMove };
  DrawReason drawReason = DrawReason::None;

  int selectedSquare = -1;
  bool gameOver = false;
  // What a tap on the play surface means. See Activity::surfaceMeaning().
  uint32_t surfaceMeaning() const override;

  bool engineThinking = false;

  // Chess opens here. Multiplayer is a row on it rather than a saved setting,
  // so re-entering the app never resumes a search nobody asked for.
  bool showingMenu = true;
  int startIndex = 0;
  bool hasSavedGame = false;
  char continueDetail[24] = {};
  // Settings has two doors -- the menu's row and the board's gear -- and closing
  // it has to return through the one you came in by. Getting dumped on the
  // board after opening settings from the menu was the first thing that felt
  // broken about the flow.
  enum class SettingsFrom : uint8_t { Menu, Board };
  SettingsFrom settingsFrom = SettingsFrom::Board;
  bool showingSettings = false;
  int menuIndex = 0;
  // What the game was actually started with. Opponent and colour cannot change
  // mid-game, so the overlay edits the live setting and compares against these
  // to decide whether leaving needs to start a new game.
  chessui::Opponent startedOpponent = chessui::Opponent::Computer;
  bool startedHumanWhite = true;
  // 0 easy, 1 normal, 2 hard. Maps to search depth in playEngineMove().
  uint8_t level = 1;
  bool humanPlaysWhite = true;
  bool showHints = true;
  chessui::Opponent opponent = chessui::Opponent::Computer;
  // Long enough for the widest name the lists can roll plus "'S MOVE".
  char turnLabel[32] = {};
  // ~2KB: the radio's receive ring plus the session. A member rather than a
  // local because it lives for the whole activity, and activities are
  // heap-allocated.
  linkplay::Play<ChessWire> link;
};
