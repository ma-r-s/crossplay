#include "ChessActivity.h"

#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../../components/UITheme.h"
#include "../Shelf.h"
#include "../player/PlayerName.h"
#include "../ui/Toybox.h"
#include "../ui/ToyboxFonts.h"
#include "../ui/ToyboxScreen.h"
#include "../ui/ToyboxTheme.h"
#include "ChessPieces.h"
#include "ChessScreens.h"

namespace {

constexpr int fileOf(const int square) { return square & 7; }
constexpr int rankOf(const int square) { return square >> 3; }

// Solid silhouette for black, outline for white. Colour reads from the drawing
// itself, so no background chip is needed and the board stays uncluttered.
const uint8_t* solidFor(const uint8_t type, const bool small) {
  switch (type) {
    case chess::Pawn:
      return small ? chessart::kPawnSolidSmall : chessart::kPawnSolid;
    case chess::Knight:
      return small ? chessart::kKnightSolidSmall : chessart::kKnightSolid;
    case chess::Bishop:
      return small ? chessart::kBishopSolidSmall : chessart::kBishopSolid;
    case chess::Rook:
      return small ? chessart::kRookSolidSmall : chessart::kRookSolid;
    case chess::Queen:
      return small ? chessart::kQueenSolidSmall : chessart::kQueenSolid;
    case chess::King:
      return small ? chessart::kKingSolidSmall : chessart::kKingSolid;
    default:
      return nullptr;
  }
}

const uint8_t* outlineFor(const uint8_t type, const bool small) {
  switch (type) {
    case chess::Pawn:
      return small ? chessart::kPawnLightSmall : chessart::kPawnLight;
    case chess::Knight:
      return small ? chessart::kKnightLightSmall : chessart::kKnightLight;
    case chess::Bishop:
      return small ? chessart::kBishopLightSmall : chessart::kBishopLight;
    case chess::Rook:
      return small ? chessart::kRookLightSmall : chessart::kRookLight;
    case chess::Queen:
      return small ? chessart::kQueenLightSmall : chessart::kQueenLight;
    case chess::King:
      return small ? chessart::kKingLightSmall : chessart::kKingLight;
    default:
      return nullptr;
  }
}

// Draws a piece at `x,y`.
//
// The light pieces are the reason this exists. Drawing only their outline left
// them hollow, so on a dithered square the board's own texture showed straight
// through the piece: fine on a white square, muddy on a dark one, and wrong in
// every theme because it was never a theme problem. The fix is to knock the
// silhouette out to the page colour first and only then stroke the outline over
// it, which is what a white piece physically is.
void drawPieceAt(const GfxRenderer& renderer, const uint8_t piece, const int size, const int x, const int y,
                 const bool turned = false) {
  const uint8_t type = chess::pieceType(piece);
  const bool small = size == chessart::kSmallPieceSize;
  const uint8_t* solid = solidFor(type, small);
  const uint8_t* outline = outlineFor(type, small);
  if (solid == nullptr || outline == nullptr) return;

  if (chess::isBlack(piece)) {
    toybox::blit1bpp(renderer, solid, size, x, y, true, turned);
    return;
  }
  toybox::blit1bpp(renderer, solid, size, x, y, false, turned);
  toybox::blit1bpp(renderer, outline, size, x, y, true, turned);
}

// Kings are never captured, so a strip only ever needs these five.
constexpr uint8_t kTrackedTypes[5] = {chess::Queen, chess::Rook, chess::Bishop, chess::Knight, chess::Pawn};
constexpr int kStartingCount[7] = {0, 8, 2, 2, 2, 1, 1};
// Optical gap between captured pieces, measured from ink to ink.
constexpr int kCapturedGap = 4;

// Next to the reader's own state, so a user clearing .crosspoint/ clears this
// too and there is one place to look.
constexpr char kSettingsPath[] = "/.crosspoint/chess.cfg";
constexpr char kSavePath[] = "/.crosspoint/chess.sav";
constexpr int kSaveVersion = 1;

}  // namespace

std::unique_ptr<Activity> ChessActivity::create(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  return makeUniqueNoThrow<ChessActivity>(renderer, mappedInput);
}

void ChessActivity::resetGame() {
  Storage.remove(kSavePath);
  startedOpponent = opponent;
  startedHumanWhite = humanPlaysWhite;
  saveSettings();
  chess::setStartPosition(position);
  selectedSquare = -1;
  gameOver = false;
  engineThinking = false;
  historyCount = 0;
  historyBase = 0;
  undoableFrom = 0;
  resetRepetition();
  refreshLegalMoves();
  // Playing Black means the engine opens.
  if (engineToMove() && !gameOver) engineThinking = true;
  requestUpdate();
}

void ChessActivity::onEnter() {
  Activity::onEnter();
  toybox::ensureFonts(renderer);
  loadSettings();
  hasSavedGame = loadGame();
  if (!hasSavedGame) {
    resetGame();
  } else if (engineToMove() && !gameOver) {
    // A game saved mid-search resumes with the engine still owing a move.
    engineThinking = true;
  }
  // Always the menu, never a resumed search. Multiplayer is an action you take,
  // so re-entering chess must not take it for you -- which is exactly what a
  // saved NEARBY setting used to do. Nothing to clear now that the base holds
  // it: a fresh activity has never asked for a link.
  goToMenu();
}

chessui::StartModel ChessActivity::startModel() const {
  chessui::StartModel model;
  model.hasSavedGame = hasSavedGame;
  model.continueDetail = continueDetail;
  model.selected = startIndex;
  return model;
}

void ChessActivity::drawStartMenu() {
  namespace fui = freeink::ui;
  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  const fui::Rect slot = chessui::buildStartMenu(screen, startModel());
  drawMiniBoard(Rect{slot.x, slot.y, slot.width, slot.height});
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Chess start");
}

void ChessActivity::drawMiniBoard(const Rect& slot) const {
  // Square art comes in two cuts and this uses the small one, so the mini board
  // is the real piece set rather than a scaled-down blur -- 1-bit artwork does
  // not survive resampling, which is why the SDK ships both sizes.
  // Shrink to fit, the way type does: pick the largest cut that fits rather
  // than scaling one. 1-bit artwork does not survive resampling, which is why
  // the set ships two sizes and neither is ever stretched.
  const int margin = toybox::kGutter * 3;
  int piece = chessart::kPieceSize;
  int square = piece + 4;
  if (square * 8 > slot.width - margin || square * 8 > slot.height - margin) {
    piece = chessart::kSmallPieceSize;
    square = piece + 8;
  }
  const int side = square * 8;
  if (slot.width < side || slot.height < side) return;
  const int originX = slot.x + (slot.width - side) / 2;
  // Anchored under the header rather than centred in the slack: a block with
  // equal air above and below reads as unresolved.
  const int originY = slot.y + toybox::kGutter * 2;

  // The frame the board screen uses, so the two read as the same object seen at
  // two sizes rather than as a board and a thumbnail.
  toybox::cornerMarks(renderer,
                      Rect{originX - toybox::kGutter, originY - toybox::kGutter, side + toybox::kGutter * 2,
                           side + toybox::kGutter * 2},
                      toybox::kGutter * 2, toybox::kFrame);

  for (int index = 0; index < 64; ++index) {
    const int file = fileOf(index);
    const int rank = rankOf(index);
    // Always White at the bottom here. The menu is a picture of the game, not a
    // seat at it, and flipping it would make the same position look like two
    // different ones depending on which colour you happened to be.
    const int x = originX + file * square;
    const int y = originY + (7 - rank) * square;
    if (((file + rank) & 1) == 0) renderer.fillRectDither(x, y, square, square, LightGray);
    if (position.squares[index] == chess::Empty) continue;
    const int inset = (square - piece) / 2;
    drawPieceAt(renderer, position.squares[index], piece, x + inset, y + inset);
  }
}

void ChessActivity::leaveBoard() {
  // One implementation for the chevron and the Back key, or the two drift and
  // only one of them remembers to tell the opponent.
  if (linkRequested()) {
    // The note is the base's business: it says one only when a question is
    // open, and stop() sends a Bye regardless.
    leaveLink();
    return;
  }
  if (!gameOver) saveGame();
  hasSavedGame = true;
  goToMenu();
}

void ChessActivity::requestNewGame() {
  // Not a guard inside resetGame(): startRematch() resets during a match on
  // purpose, once both sides have agreed. The distinction is not solo-versus-
  // match, it is authorised-versus-unilateral, and only the caller's intent
  // knows which -- so the intent gets one home instead of one branch per door.
  if (inMatch()) {
    proposeRematch();
    return;
  }
  resetGame();
}

void ChessActivity::refreshTurnLabel() {
  if (linkYourTurn()) {
    snprintf(turnLabel, sizeof(turnLabel), "YOUR MOVE");
    return;
  }
  // Their first word, not their whole name. A full name is three words now, and
  // "SHAGGY SLEEPY GOATEE'S MOVE" ran past the capsule and dropped "MOVE" --
  // losing the only word that said what the capsule was for. See
  // player::shortName.
  char them[player::kMaxShortNameLength + 1] = {};
  player::shortName(opponentName(), them, sizeof(them));
  if (them[0] == '\0') {
    snprintf(turnLabel, sizeof(turnLabel), "THEIR MOVE");
    return;
  }
  snprintf(turnLabel, sizeof(turnLabel), "%s'S MOVE", them);
}

void ChessActivity::refreshContinueDetail() {
  snprintf(continueDetail, sizeof(continueDetail), "%d %s", historyCount / 2 + 1,
           gameOver ? "OVER" : (position.whiteToMove ? "WHITE" : "BLACK"));
}

void ChessActivity::goToMenu() {
  refreshContinueDetail();
  showingMenu = true;
  showingSettings = false;
  startIndex = 0;
  requestUpdate();
}

void ChessActivity::activateStartRow(const chessui::StartRow row) {
  switch (row) {
    case chessui::StartRow::Continue:
      showingMenu = false;
      break;
    case chessui::StartRow::NewGame:
      showingMenu = false;
      requestNewGame();
      break;
    case chessui::StartRow::PlayNearby:
      // An action, taken now and remembered nowhere. Leaving multiplayer puts
      // the single-player game back exactly as it was.
      enterLink(linkplay::GameId::Chess);
      showingMenu = false;
      break;
    case chessui::StartRow::Settings:
      showingMenu = false;
      showingSettings = true;
      settingsFrom = SettingsFrom::Menu;
      menuIndex = 0;
      break;
    default:
      break;
  }
  requestUpdate();
}

void ChessActivity::routeStartMenu() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // Chess does not know where it lives. shelf::leave() puts it back in the
    // folder it was opened from; see src/apps_local/Shelf.h for why no app
    // names its own destination.
    shelf::leave(renderer, mappedInput);
    return;
  }

  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // No focusNext/focusPrev/confirm. Rows are tapped. Confirm does not exist on
  // this device, so a row selection only a button could open was a dead end.

  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);
  if (event.action == chessui::ActionStartRow) {
    startIndex = event.value;
    activateStartRow(chessui::startRowAt(startModel(), event.value));
  }
}

void ChessActivity::onExit() {
  // Runs on sleep and on an app switch as well as on the way out, which is why
  // this was the door that mattered: you press nothing to sleep. It needs no
  // match check of its own -- saveGame() holds that invariant.
  //
  // The link needs no stopping here either: ~PlayBase does it, and the peer gets
  // the Bye from leave().
  if (!gameOver) saveGame();
  Activity::onExit();
}

void ChessActivity::saveGame() const {
  // Never during a match. The position on screen is the shared game, and this
  // file is the single-player slot -- the one leaveLink() reloads from, which is
  // exactly why nothing may overwrite it here. Two callers got this wrong
  // separately (Back, and onExit on sleep), and no caller has ever wanted the
  // other behaviour, so the rule belongs here rather than at each door.
  if (linkRequested()) return;

  // Written line by line rather than assembled in one buffer: a full history is
  // ~1.4KB, far past the 256-byte stack-local limit, and this is a cold path
  // where a few small writes cost nothing.
  HalFile file;
  if (!Storage.openFileForWrite("CHESS", kSavePath, file)) {
    LOG_ERR("CHESS", "Could not open %s for write", kSavePath);
    return;
  }
  char line[96];
  snprintf(line, sizeof(line), "%d\n", kSaveVersion);
  file.write(reinterpret_cast<const uint8_t*>(line), strlen(line));
  char fen[90];
  chess::positionToFen(position, fen);
  snprintf(line, sizeof(line), "%s\n%d\n", fen, historyBase);
  file.write(reinterpret_cast<const uint8_t*>(line), strlen(line));
  for (int i = 0; i < historyCount; ++i) {
    snprintf(line, sizeof(line), "%s\n", history[i]);
    file.write(reinterpret_cast<const uint8_t*>(line), strlen(line));
  }
}

void ChessActivity::saveSettings() const {
  char line[48];
  snprintf(line, sizeof(line), "%d %d %d %d %d\n", kSaveVersion, static_cast<int>(level), humanPlaysWhite ? 1 : 0,
           showHints ? 1 : 0, static_cast<int>(opponent));
  Storage.writeFile(kSettingsPath, String(line));
}

void ChessActivity::loadSettings() {
  if (!Storage.exists(kSettingsPath)) return;
  char buffer[48] = {};
  if (Storage.readFileToBuffer(kSettingsPath, buffer, sizeof(buffer)) == 0) return;
  int version = 0;
  int storedLevel = 1;
  int white = 1;
  int hints = 1;
  int computer = 1;
  // Files written before the opponent setting existed simply lack the field,
  // so accept four and default the fifth rather than discarding the lot.
  const int fields = sscanf(buffer, "%d %d %d %d %d", &version, &storedLevel, &white, &hints, &computer);
  if (fields < 4) return;
  if (version != kSaveVersion) return;
  level = static_cast<uint8_t>(storedLevel < 0 || storedLevel > 2 ? 1 : storedLevel);
  humanPlaysWhite = white != 0;
  showHints = hints != 0;
  // Files written while NEARBY was briefly a saved value read back as COMPUTER,
  // which is the point: multiplayer must not survive a restart.
  opponent = fields >= 5 && computer > 0 && computer <= static_cast<int>(chessui::Opponent::FaceToFace)
                 ? static_cast<chessui::Opponent>(computer)
                 : chessui::Opponent::Computer;
  startedOpponent = opponent;
  startedHumanWhite = humanPlaysWhite;
}

bool ChessActivity::loadGame() {
  if (!Storage.exists(kSavePath)) return false;
  // 2KB: version + FEN + base + up to 128 SAN lines. Heap, not stack.
  auto buffer = makeUniqueNoThrow<char[]>(2048);
  if (!buffer) {
    LOG_ERR("CHESS", "OOM reading save");
    return false;
  }
  const size_t read = Storage.readFileToBuffer(kSavePath, buffer.get(), 2048);
  if (read == 0) return false;

  char* cursor = buffer.get();
  const auto nextLine = [&cursor]() -> char* {
    if (cursor == nullptr || *cursor == '\0') return nullptr;
    char* start = cursor;
    while (*cursor != '\0' && *cursor != '\n') ++cursor;
    if (*cursor == '\n') *cursor++ = '\0';
    return start;
  };

  const char* version = nextLine();
  if (version == nullptr || atoi(version) != kSaveVersion) {
    LOG_INF("CHESS", "Ignoring save with unknown version");
    return false;
  }
  const char* fen = nextLine();
  const char* base = nextLine();
  chess::Position restored;
  if (fen == nullptr || base == nullptr || !chess::parseFen(fen, restored)) {
    LOG_ERR("CHESS", "Corrupt save, starting fresh");
    return false;
  }

  position = restored;
  historyBase = atoi(base);
  historyCount = 0;
  for (const char* line = nextLine(); line != nullptr && historyCount < kMaxHistory; line = nextLine()) {
    if (*line == '\0') continue;
    snprintf(history[historyCount], sizeof(history[0]), "%s", line);
    ++historyCount;
  }

  selectedSquare = -1;
  engineThinking = false;
  // The plies just read back are notation only: the save stores the position
  // and the move sheet, never the undo data. Neither take-back nor repetition
  // may look behind this point.
  undoableFrom = historyCount;
  // Repetition starts again from the resumed position. The keys of the
  // positions before it are not recoverable from the save, and counting a
  // repetition against keys we do not have is the one outcome worth ruling
  // out: it would end a live game in a draw that never happened.
  resetRepetition();
  refreshLegalMoves();
  LOG_INF("CHESS", "Resumed saved game (%d plies)", historyCount);
  return true;
}

void ChessActivity::applyMove(const chess::Move& move) {
  {
    // The renderer runs on its own task and draws these squares directly, so
    // the board has to change under the same lock the render task holds.
    // Without it a repaint landing inside makeMove() shows the moving piece on
    // both squares or on neither.
    RenderLock lock;
    // recordMove appends and may shift the whole buffer down first, so the slot
    // to fill is only known afterwards.
    recordMove(move);
    chess::Undo undo;
    chess::makeMove(position, move, undo);
    historyMove[historyCount - 1] = move;
    historyUndo[historyCount - 1] = undo;
    // After the move, so the key is the position this ply produced. Before
    // refreshLegalMoves, which is what reads the window back.
    pushRepetitionKey();
    refreshLegalMoves();
  }
  // The move is on our board; put it on theirs. Only after refreshLegalMoves,
  // so a move that ends the game travels with the position that proves it.
  if (linkRequested() && linkYourTurn()) sendPosition();
}

bool ChessActivity::canTakeBack() const {
  // Not against a remote opponent: their board has the move too, and taking it
  // back here would need their agreement, which is a conversation this design
  // deliberately has no way to have.
  if (linkPlaying()) return false;
  // undoableFrom rather than 0: a resumed game has notation for its earlier
  // plies but no way to unmake them.
  return historyCount > undoableFrom && !engineThinking;
}

void ChessActivity::takeBack() {
  if (!canTakeBack()) return;
  // Undo back to your own turn, which is one ply if the engine has not replied
  // yet and two if it has. Anything else hands the move straight back to it.
  // Sharing the device, one ply is a whole turn and the board simply hands back
  // to the other player. Against the engine, undoing one ply would just let it
  // move again, so it takes back the pair.
  const int plies = vsComputer() ? (engineToMove() ? 1 : 2) : 1;
  {
    RenderLock lock;
    for (int i = 0; i < plies && historyCount > undoableFrom; ++i) {
      --historyCount;
      chess::unmakeMove(position, historyMove[historyCount], historyUndo[historyCount]);
      // One key per ply applied, so one key per ply undone. Never below the
      // position the window started from.
      if (repCount > 1) --repCount;
    }
    selectedSquare = -1;
    // gameOver is refreshLegalMoves()'s to set now that a draw can end the
    // game: clearing it here would have been overwritten a line later anyway.
    refreshLegalMoves();
  }
  saveGame();
  requestUpdate();
}

void ChessActivity::recordMove(const chess::Move& move) {
  if (historyCount == kMaxHistory) {
    // Drop the oldest ply and remember that we did, so printed move numbers
    // stay correct rather than restarting from 1.
    for (int i = 1; i < kMaxHistory; ++i) {
      std::memcpy(history[i - 1], history[i], sizeof(history[0]));
      historyMove[i - 1] = historyMove[i];
      historyUndo[i - 1] = historyUndo[i];
    }
    --historyCount;
    ++historyBase;
    if (undoableFrom > 0) --undoableFrom;
  }
  chess::moveToSan(position, move, history[historyCount]);
  ++historyCount;
}

ChessActivity::BoardGeometry ChessActivity::boardGeometry() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int bandTop = toybox::kHeaderHeight;
  const int bandHeight = screenHeight - bandTop;
  const int statusBlock = toybox::kGutter * 2 + toybox::kPillHeight;
  const int strips = 2 * (chessart::kSmallPieceSize + toybox::kGutter);
  // Side margins: the board was running to within 4px of the bezel, which is
  // what made the whole screen feel cramped next to the mockup.
  const int available = std::min(screenWidth - 2 * toybox::kMargin - 2 * toybox::kBoardFrame,
                                 bandHeight - strips - statusBlock - 2 * toybox::kFrame - toybox::kGutter);

  BoardGeometry geometry{};
  geometry.squareSize = std::max(8, available / 8);
  geometry.originX = (screenWidth - geometry.squareSize * 8) / 2;
  // Centre the board-and-status group in the band under the header. The board
  // is width-constrained on this screen, so without this the whole composition
  // hangs from the top and leaves a dead third of the page below it. On a
  // display that sits showing the same frame for hours, that reads as unfinished.
  const int groupHeight = geometry.squareSize * 8 + 2 * toybox::kFrame + statusBlock + strips;
  // Board sits close under the header. Mario's note was that there was too much
  // air above Black's captures and below White's; the fix is not to spread the
  // slack evenly but to spend it on something worth reading, so the top is tight
  // and everything left over becomes the move list above the capsule.
  (void)groupHeight;
  const int stripBand = chessart::kSmallPieceSize + toybox::kGutter;
  geometry.originY = bandTop + toybox::kGutter + stripBand + toybox::kBoardFrame;
  return geometry;
}

bool ChessActivity::whiteAtBottom() const {
  // In a link match your colour is fixed for the game, so the board never turns
  // under you.
  if (linkPlaying()) return humanPlaysWhite;
  switch (opponent) {
    case chessui::Opponent::Computer:
      // The same person holds it all game, so it stays put.
      return humanPlaysWhite;
    case chessui::Opponent::PassAndPlay:
      // The device changes hands, so it turns to face whoever is about to move
      // and nobody reads their own position upside down.
      return position.whiteToMove;
    case chessui::Opponent::FaceToFace:
      // The device does not move, so neither does the board. Black reads it
      // upside down, which is what sitting opposite somebody means.
      return true;
  }
  return true;
}

int ChessActivity::screenColumnOf(const int square) const {
  return whiteAtBottom() ? fileOf(square) : 7 - fileOf(square);
}

int ChessActivity::screenRowOf(const int square) const { return whiteAtBottom() ? 7 - rankOf(square) : rankOf(square); }

bool ChessActivity::engineToMove() const { return vsComputer() && position.whiteToMove != humanPlaysWhite; }

int ChessActivity::squareAtPoint(const int x, const int y) const {
  const BoardGeometry geometry = boardGeometry();
  const int boardSize = geometry.squareSize * 8;
  if (x < geometry.originX || x >= geometry.originX + boardSize) return -1;
  if (y < geometry.originY || y >= geometry.originY + boardSize) return -1;
  const int column = (x - geometry.originX) / geometry.squareSize;
  const int row = (y - geometry.originY) / geometry.squareSize;
  // Inverse of screenColumnOf/screenRowOf, so the hit test follows the board
  // when it is flipped for playing Black.
  const int file = whiteAtBottom() ? column : 7 - column;
  const int rank = whiteAtBottom() ? 7 - row : row;
  return rank * 8 + file;
}

void ChessActivity::refreshLegalMoves() {
  chess::generateLegalMoves(position, legalMoves);
  drawReason = DrawReason::None;
  // Mate and stalemate first. Mate on the hundredth halfmove is mate, and a
  // position repeated for the third time with no move to make is stalemate --
  // in both cases the side to move having no move is the older, stronger fact.
  if (legalMoves.count == 0) {
    gameOver = true;
    return;
  }
  if (currentRepetitionCount() >= 3) {
    drawReason = DrawReason::Repetition;
  } else if (position.halfmoveClock >= 100) {
    // 100 halfmoves, not 50: the rule counts moves by both players.
    drawReason = DrawReason::FiftyMove;
  }
  gameOver = drawReason != DrawReason::None;
}

void ChessActivity::resetRepetition() {
  repCount = 0;
  pushRepetitionKey();
}

void ChessActivity::pushRepetitionKey() {
  const uint64_t key = chess::positionKey(position);
  // Two consecutive positions can never share a key: the side to move flips
  // every ply, and it is hashed. So an equal key is the same state offered
  // twice -- a retransmit from the other device -- and counting it would
  // manufacture a repetition out of the radio.
  if (repCount > 0 && repKeys[repCount - 1] == key) return;
  if (repCount == kRepWindow) {
    // Unreachable while the fifty-move draw above fires at 100 halfmoves, and
    // here anyway: dropping the oldest key is the one safe way to lose
    // information, because a shorter window can only miss a repetition.
    for (int i = 1; i < kRepWindow; ++i) repKeys[i - 1] = repKeys[i];
    --repCount;
  }
  repKeys[repCount++] = key;
}

int ChessActivity::currentRepetitionCount() const {
  return chess::repetitionCount(repKeys, repCount, position.halfmoveClock);
}

bool ChessActivity::isLegalDestination(const int square) const {
  if (selectedSquare < 0) return false;
  for (int i = 0; i < legalMoves.count; ++i) {
    if (legalMoves.moves[i].from == selectedSquare && legalMoves.moves[i].to == square) return true;
  }
  return false;
}

bool ChessActivity::tryMove(const int from, const int to) {
  for (int i = 0; i < legalMoves.count; ++i) {
    const chess::Move& move = legalMoves.moves[i];
    if (move.from != from || move.to != to) continue;
    // Several entries share from/to when promoting; take the queen.
    if ((move.flags & chess::FlagPromotion) != 0 && move.promotion != chess::Queen) continue;
    applyMove(move);
    return true;
  }
  return false;
}

void ChessActivity::handleSquareActivated(const int square) {
  if (engineThinking) return;
  // Not our turn in a link match: until they move, the board is a picture of
  // their game rather than something to touch. Without this a tap would change
  // the position locally and never be sent -- sendPosition refuses out of turn
  // -- so the two boards would silently diverge. That is the exact failure the
  // link layer makes unrepresentable, reintroduced one level up by the game.
  if (linkPlaying() && !linkYourTurn()) return;
  if (!gameOver && engineToMove()) return;
  // Confirm still restarts without aiming, for the button-only devices.
  if (gameOver) {
    resetGame();
    return;
  }
  if (square < 0) return;

  if (selectedSquare >= 0 && tryMove(selectedSquare, square)) {
    selectedSquare = -1;
    // Hand off to the engine on the next pass rather than searching here, so
    // this repaint shows the played move and "Thinking" before the search runs.
    // engineToMove() is the guard, not just !gameOver: without it the engine
    // answered in two-player mode too, which is what the first friend-mode
    // screenshot caught.
    if (!gameOver && engineToMove()) engineThinking = true;
    requestUpdate();
    return;
  }

  // Tapping your own piece selects it (and re-tapping deselects); tapping
  // anything else with a selection active clears it.
  const uint8_t piece = position.squares[square];
  if (chess::isColour(piece, position.whiteToMove)) {
    selectedSquare = selectedSquare == square ? -1 : square;
  } else {
    selectedSquare = -1;
  }
  requestUpdate();
}

void ChessActivity::playEngineMove() {
  engineThinking = false;
  // Depth 3 with a node cap. The cap is the safety net: if a position blows up
  // combinatorially the search stops early and still returns its best move so
  // far, rather than stalling the loop and tripping the watchdog.
  static constexpr int kDepthForLevel[3] = {1, 3, 4};
  constexpr uint32_t kNodeBudget = 60000;
  // A COPY, and this is the whole of the flicker fix. search() makes and
  // unmakes thousands of moves on whatever position it is handed, and the
  // render task reads `position` straight out of this object while the main
  // task is inside the search. Handed the live board, a repaint drew the
  // middle of the search: a wrong position on screen for as long as the search
  // took, then the real one when the move landed.
  //
  // The one-pass deferral in handleSquareActivated() was written to prevent
  // exactly this and cannot, because requestUpdate() only NOTIFIES the render
  // task. The repaint it asks for is still in flight when the search starts,
  // so deferring by a pass moved the race rather than closing it.
  chess::Position scratch = position;
  const chess::SearchResult result = chess::search(scratch, kDepthForLevel[level], searchBuffers, kNodeBudget);
  LOG_DBG("CHESS", "engine: %u nodes, score %d%s", static_cast<unsigned>(result.nodes), result.score,
          result.budgetExhausted ? " (budget hit)" : "");
  if (!result.hasMove) {
    gameOver = true;
    requestUpdate();
    return;
  }
  applyMove(result.best);
  // Save on the completed pair rather than on every ply: it is the natural
  // resume point, and auto-sleep never runs onExit().
  if (gameOver) {
    Storage.remove(kSavePath);
  } else {
    saveGame();
  }
  requestUpdate();
}

const char* ChessActivity::resultText() const {
  if (!gameOver) return "";
  // Named rather than a bare "DRAW". A game that stops on its own needs to say
  // why, or it reads as the app having given up: both of these arrive with no
  // move played and nothing on the board to point at.
  if (drawReason == DrawReason::Repetition) return "DRAW: REPETITION";
  if (drawReason == DrawReason::FiftyMove) return "DRAW: 50 MOVES";
  if (chess::isInCheck(position)) return position.whiteToMove ? "BLACK WINS" : "WHITE WINS";
  return "STALEMATE";
}

const char* ChessActivity::statusText() const {
  // At game over the capsule stops reporting and starts acting: the result
  // moves to its own line and the capsule becomes the button.
  if (gameOver) return "PLAY AGAIN";
  if (engineThinking) return "THINKING";
  // Whose check it is does not need saying: the board already shows the turn.
  if (chess::isInCheck(position)) return "CHECK";
  // Naming them matters most here. You learn who you are playing on the
  // searching screen and then, until now, spent the whole game being told only
  // that it was somebody else's move.
  if (linkPlaying()) return turnLabel;
  if (!vsComputer()) return position.whiteToMove ? "WHITE TO MOVE" : "BLACK TO MOVE";
  return engineToMove() ? "THINKING" : "YOUR MOVE";
}

void ChessActivity::gameLoop() {
  if (showingMenu) {
    routeStartMenu();
    return;
  }

  if (showingSettings) {
    routeSettings();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    // In a match this tells the opponent and shuts the radio down. It no longer
    // has to remember not to save -- saveGame() refuses on its own now.
    leaveBoard();
    return;
  }

  // Deferred by one pass from the move that triggered it, so "THINKING" is on
  // screen before the search starts.
  if (engineThinking) {
    playEngineMove();
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    // Chrome first, from the table the last paint filled in. The board's own
    // squares are not components: 64 of them would be 64 interactions, and
    // squareAtPoint() already derives them from the geometry that drew them.
    if (interactionsReady) {
      freeink::ui::InputSnapshot tap;
      tap.touchReleased = true;
      tap.touchX = static_cast<int16_t>(touchX);
      tap.touchY = static_cast<int16_t>(touchY);
      const freeink::ui::ActionEvent event = interactions.route(tap);
      if (event.action == chessui::ActionOpenSettings) {
        showingSettings = true;
        settingsFrom = SettingsFrom::Board;
        menuIndex = 0;
        requestUpdate();
        return;
      }
      if (event.action == chessui::ActionPlayAgain) {
        // Guarded locally rather than relying on driveLink() diverting to the
        // rematch screen before this is reachable. Three separate doors have
        // now tried to reset a board the other device is also holding, and the
        // pattern is that each was safe until something upstream moved.
        requestNewGame();
        return;
      }
    }
    // Everything below the chrome is the board. A stray tap on a finished game
    // does nothing, because only the button restarts it.
    if (!gameOver) {
      const int square = squareAtPoint(touchX, touchY);
      if (square >= 0) {
        handleSquareActivated(square);
      }
    }
    return;
  }

  // No button navigation. The board is pointing, and pointing is fingers.
  //
  // There was a D-pad cursor here, moved by Up/Down/Left/Right and committed
  // with Confirm. On the X4 Pro three of those four buttons do not exist --
  // left, right and confirm are PIN_UNASSIGNED in the board profile and are
  // never configured as inputs -- so the cursor could move up and down a file
  // and could never be acted on.
  //
  // It was worse than the shelf's version, which at least drew itself: this one
  // was guarded by `!mappedInput.hasTouch()`, so on a touch device it was
  // INVISIBLE, and Up/Down still moved it and still called requestUpdate(). A
  // guaranteed full-panel repaint that changed nothing on screen, which is
  // exactly what a bug looks like on e-ink.
  //
  // See docs/buttons.md. Up and Down are the two side page keys and they page;
  // they do not push things around a board.
}

void ChessActivity::drawSquareContents(const BoardGeometry& geometry, const int square) const {
  const int x = geometry.originX + screenColumnOf(square) * geometry.squareSize;
  const int y = geometry.originY + screenRowOf(square) * geometry.squareSize;
  const int size = geometry.squareSize;

  // Dark squares are dithered, never solid. The board repaints on every move,
  // and solid fills at this area are exactly what makes e-ink ghost. This is
  // the ink-budget rule from Toybox.h in its most literal form.
  // The light dither, not the heavy one: at 50% the squares carry nearly as much
  // ink as the pieces, and the board reads as texture competing with the
  // position instead of a surface it sits on.
  const bool darkSquare = ((fileOf(square) + rankOf(square)) & 1) == 0;
  if (darkSquare) renderer.fillRectDither(x, y, size, size, LightGray);

  const uint8_t piece = position.squares[square];
  if (piece != chess::Empty) {
    const int inset = (size - chessart::kPieceSize) / 2;
    // Face to face, the far player is reading the screen from the other end, so
    // their pieces are turned to be upright from where they are sitting. Only
    // there: every other mode has one person looking at it at a time.
    const bool turned = opponent == chessui::Opponent::FaceToFace && chess::isBlack(piece);
    drawPieceAt(renderer, piece, chessart::kPieceSize, x + inset, y + inset, turned);
  }

  if (showHints && isLegalDestination(square)) {
    if (piece == chess::Empty) {
      // Empty square: a solid dot in the middle. Nothing to hide behind it.
      const int diameter = std::max(8, size / 4);
      renderer.fillRoundedRect(x + (size - diameter) / 2, y + (size - diameter) / 2, diameter, diameter, diameter / 2,
                               Black);
    } else {
      // Occupied square: mark the corners instead. A dot in the middle of a
      // capture sits on top of the piece, where it vanishes into a solid black
      // silhouette and looks like a blemish on a light one. The corners are
      // free of piece art at every size, so the mark reads on either colour and
      // you can still see what you are taking.
      toybox::cornerMarks(renderer, Rect{x, y, size, size}, std::max(10, size / 4), toybox::kFrame);
    }
  }
}

void ChessActivity::drawCapturedStrip(const int y, const bool capturedFromBlack) const {
  int x = toybox::kMargin;
  for (const uint8_t type : kTrackedTypes) {
    int present = 0;
    for (int square = 0; square < 64; ++square) {
      const uint8_t piece = position.squares[square];
      if (piece != chess::Empty && chess::pieceType(piece) == type && chess::isBlack(piece) == capturedFromBlack) {
        ++present;
      }
    }
    if (type == chess::King) continue;
    const uint8_t piece = static_cast<uint8_t>(type | (capturedFromBlack ? chess::BlackFlag : 0));
    // Pack by real ink width, not by bitmap width. Every small bitmap is 24px
    // but the ink inside ranges from 13px (pawn) to 22px (queen), so a fixed
    // stride that suited pawns made a queen collide with the rook beside it.
    const chessart::SmallInk ink = chessart::kSmallInk[type];
    for (int i = kStartingCount[type] - present; i > 0; --i) {
      drawPieceAt(renderer, piece, chessart::kSmallPieceSize, x - ink.left, y);
      x += ink.width + kCapturedGap;
    }
  }
}

int ChessActivity::statusPillY() const { return renderer.getScreenHeight() - toybox::kMargin - toybox::kPillHeight; }

void ChessActivity::drawMoveList(const int top, const int height) const {
  const toybox::FontMetrics metrics = toybox::metricsFor(toybox::kUiFontId);
  const int lineHeight = metrics.capHeight + 16;
  const int lines = height / lineHeight;
  if (lines <= 0) return;

  if (gameOver) {
    // At game over the one thing you need to know is how to start again, and
    // that matters more than reviewing the score sheet.
    toybox::drawCapsCentered(renderer, toybox::kUiFontId,
                             (renderer.getScreenWidth() - renderer.getTextWidth(toybox::kUiFontId, resultText())) / 2,
                             top + height - lineHeight, lineHeight, resultText(), true);
    if (historyCount == 0) return;
  }

  const int usable = gameOver ? lines - 1 : lines;
  if (usable <= 0 || historyCount == 0) return;

  // Show the most recent full moves that fit, starting on a White ply so the
  // columns line up.
  int firstPly = historyCount - usable * 2;
  if (firstPly < 0) firstPly = 0;
  firstPly -= (firstPly + historyBase) % 2;
  if (firstPly < 0) firstPly = 0;

  for (int line = 0; line < usable; ++line) {
    const int ply = firstPly + line * 2;
    if (ply >= historyCount) break;
    char text[40];
    const int moveNumber = (historyBase + ply) / 2 + 1;
    if (ply + 1 < historyCount) {
      snprintf(text, sizeof(text), "%d. %s  %s", moveNumber, history[ply], history[ply + 1]);
    } else {
      snprintf(text, sizeof(text), "%d. %s", moveNumber, history[ply]);
    }
    const int width = renderer.getTextWidth(toybox::kUiFontId, text);
    toybox::drawCapsCentered(renderer, toybox::kUiFontId, (renderer.getScreenWidth() - width) / 2,
                             top + line * lineHeight, lineHeight, text, true);
  }
}

Rect ChessActivity::gearRect() const {
  const int size = 40;
  return Rect{renderer.getScreenWidth() - toybox::kMargin - size, (toybox::kHeaderHeight - size) / 2, size, size};
}

void ChessActivity::activateMenuRow(const MenuRow row) {
  switch (row) {
    case MenuRow::NewGame:
      showingSettings = false;
      requestNewGame();
      return;
    case MenuRow::TakeBack:
      if (!canTakeBack()) return;
      showingSettings = false;
      takeBack();
      return;
    case MenuRow::Level:
      level = static_cast<uint8_t>((level + 1) % 3);
      break;
    case MenuRow::PlayAs:
      // Edited in place. Closing the overlay is what starts the new game, and
      // the button down there says so, because a tap that silently threw away
      // your game and jumped back to the board left you wondering whether
      // anything had happened at all.
      humanPlaysWhite = !humanPlaysWhite;
      break;
    case MenuRow::Opponent:
      // Three values, cycling. Multiplayer is not one of them: making it a saved
      // preference is what made chess resume a search on every entry.
      opponent = static_cast<chessui::Opponent>((static_cast<int>(opponent) + 1) %
                                                (static_cast<int>(chessui::Opponent::FaceToFace) + 1));
      // The visible row set shrinks against a person, so a selection sitting on
      // a row that just disappeared has to come back into range.
      if (menuIndex >= visibleMenuRows()) menuIndex = visibleMenuRows() - 1;
      break;
    case MenuRow::Hints:
      showHints = !showHints;
      break;
    default:
      return;
  }
  saveSettings();
  requestUpdate();
}

void ChessActivity::routeSettings() {
  namespace fui = freeink::ui;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeSettings();
    return;
  }

  // One snapshot, one table, one dispatch: touch, focus and Confirm all arrive
  // as the same semantic action, so a row cannot behave differently depending
  // on which input reached it.
  fui::InputSnapshot input;
  int tapX = 0;
  int tapY = 0;
  if (mappedInput.wasScreenTapped(tapX, tapY)) {
    input.touchReleased = true;
    input.touchX = static_cast<int16_t>(tapX);
    input.touchY = static_cast<int16_t>(tapY);
  }
  // No focusNext/focusPrev/confirm. Rows are tapped. Confirm does not exist on
  // this device, so a row selection only a button could open was a dead end.

  if (!input.touchReleased || !interactionsReady) return;

  const fui::ActionEvent event = interactions.route(input);
  if (event.action == chessui::ActionMenuRow) {
    menuIndex = event.value;
    activateMenuRow(menuRowAt(event.value));
    return;
  }
  // The close button, and any tap that hit nothing: off-target taps close, the
  // same as they did before there was a button to hit.
  closeSettings();
}

bool ChessActivity::restartPending() const {
  // Never during a link match. The board is shared, so resetting it here would
  // wipe the other device's game without telling it -- the same defect as the
  // old NEW GAME, arriving by a different door. Restarting a shared game is a
  // question, and NEW GAME is where it gets asked.
  if (linkPlaying()) return false;
  return opponent != startedOpponent || (vsComputer() && humanPlaysWhite != startedHumanWhite);
}

void ChessActivity::closeSettings() {
  showingSettings = false;
  // A setting that cannot apply mid-game restarts it, which is what the close
  // button has been saying it would do.
  if (restartPending()) requestNewGame();
  if (settingsFrom == SettingsFrom::Menu) {
    goToMenu();
    return;
  }
  requestUpdate();
}

chessui::SettingsModel ChessActivity::settingsModel() const {
  chessui::SettingsModel model;
  model.fromMenu = settingsFrom == SettingsFrom::Menu;
  model.opponent = opponent;
  model.humanPlaysWhite = humanPlaysWhite;
  model.showHints = showHints;
  model.level = level;
  model.canTakeBack = canTakeBack();
  model.restartPending = restartPending();
  model.selected = menuIndex;
  return model;
}

void ChessActivity::drawSettings() {
  namespace fui = freeink::ui;

  renderer.clearScreen();
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);
  chessui::buildSettings(screen, settingsModel());
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Chess settings");
}

// --- the link ---------------------------------------------------------------
//
// What is left of it. LinkActivity owns the searching screen, the rematch
// conversation, the seat states, the note vocabulary, the tick and the sleep
// suppression; these are the parts only chess can answer.

void ChessActivity::onMatchStart(const bool goesFirst) {
  // Whoever the coin toss put first plays White. Both devices reach the same
  // answer, so there is nothing to agree on and nothing to show.
  humanPlaysWhite = goesFirst;
  refreshTurnLabel();
  resetGame();
}

bool ChessActivity::takeOpponentState() {
  ChessWire incoming;
  if (!link.takeOpponent(incoming)) return false;
  adoptRemote(incoming);
  return true;
}

void ChessActivity::onRematch() {
  // The side holding the turn plays White. After a mate that is the player who
  // was mated, which is the courtesy anyway, and it means colours alternate
  // across a session with nobody negotiating anything. It also has to be this
  // rule rather than a fresh toss: the session's turn carries on from the last
  // game, so White must be whoever it already says may move.
  humanPlaysWhite = linkYourTurn();
  // The overlay may still be open underneath: the game can end while it is up,
  // because the link is driven before the settings branch. Landing back in
  // settings after accepting a rematch is not where anybody was going.
  showingSettings = false;
  resetGame();
}

// Puts the single-player game back and returns to the menu rather than dropping
// the player out to the apps hub, which is where they came from two taps ago.
void ChessActivity::onLinkEnded() {
  showingMenu = true;
  startIndex = 0;
  loadSettings();
  hasSavedGame = loadGame();
  if (!hasSavedGame) resetGame();
  goToMenu();
}

const char* ChessActivity::linkHeadline() const {
  if (linkPhase() == linkplay::PlayBase::Phase::Searching) return "LOOKING FOR A PLAYER";
  if (!gameOver) return "ANOTHER GAME?";
  if (legalMoves.count == 0 && chess::isInCheck(position)) {
    // The board is off screen by now, so the headline has to say who won rather
    // than only that it is over.
    return position.whiteToMove == humanPlaysWhite ? "YOU LOST" : "YOU WON";
  }
  return "DRAW";
}

void ChessActivity::sendPosition() {
  ChessWire wire = {};
  chess::positionToFen(position, wire.fen);
  if (historyCount > 0) {
    const char* san = history[historyCount - 1];
    const size_t length = strnlen(san, sizeof(wire.lastMove) - 1);
    memcpy(wire.lastMove, san, length);
    wire.lastMove[length] = '\0';
  }
  // Refused if it is not our turn, which cannot happen here: the board ignores
  // input unless it is. Logged rather than asserted because a lost move would
  // be invisible otherwise.
  if (!link.play(wire)) LOG_ERR("CHESS", "link refused a move that the board allowed");
}

void ChessActivity::adoptRemote(const ChessWire& wire) {
  chess::Position next;
  if (!chess::parseFen(wire.fen, next)) {
    // Two builds that disagree about the position format. GameId carries a
    // layout version so this should be unreachable; say so if it is not.
    LOG_ERR("CHESS", "could not read the position the other device sent");
    return;
  }
  {
    // Same reason as applyMove(): the whole board changes here, one struct
    // assignment wide, and the render task reads it from another core.
    RenderLock lock;
    position = next;
    if (wire.lastMove[0] != '\0') recordRemoteMove(wire.lastMove);
    selectedSquare = -1;
    // Their move is a position transition like any other, so the window has to
    // see it. Skipping it would leave repetitionCount() comparing a key that is
    // no longer the board -- and in a match every second ply arrives this way,
    // so the window would otherwise be half the game.
    pushRepetitionKey();
    refreshLegalMoves();
  }
}

void ChessActivity::recordRemoteMove(const char* san) {
  // The score sheet only: a position says nothing about how it was reached, and
  // there is no Move or Undo to store because take-back is off in a link game.
  if (historyCount >= kMaxHistory) {
    for (int i = 1; i < kMaxHistory; ++i) memcpy(history[i - 1], history[i], sizeof(history[0]));
    historyCount = kMaxHistory - 1;
    historyBase++;
  }
  const size_t length = strnlen(san, sizeof(history[0]) - 1);
  memcpy(history[historyCount], san, length);
  history[historyCount][length] = '\0';
  historyCount++;
}

void ChessActivity::gameRender() {
  if (showingMenu) {
    drawStartMenu();
    renderer.displayBuffer();
    return;
  }

  if (showingSettings) {
    drawSettings();
    renderer.displayBuffer();
    return;
  }

  renderer.clearScreen();

  namespace fui = freeink::ui;
  fui::GfxRendererTarget target = toybox::makeTarget(renderer);
  const fui::InputSnapshot noInput{};
  interactionsReady = false;
  toybox::Frame frame(target, target.deviceContext(), noInput, interactions);
  toybox::Screen screen(frame);

  chessui::BoardModel chrome;
  chrome.status = statusText();
  chrome.gameOver = gameOver;
  // Only in a match. The engine has no face, and inMatch() is what the layer
  // already uses everywhere else to mean "there is somebody there".
  chrome.theirName = inMatch() ? opponentName() : nullptr;
  // Everything the SDK can own: the header band, the status capsule, and the
  // rect left over. The board itself is drawn into that rect below, which is
  // the split FreeInkUI documents for app-specific surfaces.
  chessui::buildBoardChrome(screen, chrome);

  // The move number used to live here, but it is already in the move list, and
  // a way into settings is worth more than repeating it. The gear stays
  // procedural (no asset to keep in step with the piece set), so it draws
  // itself and registers its own target from the same rect: a 40px gear is
  // smaller than a fingertip, so the target is that rect grown to the corner,
  // not the icon inflated.
  const Rect gear = gearRect();
  toybox::gear(renderer, gear, false);
  const int gearHitX = gear.x - toybox::kGutter;
  frame.hit(fui::makeRect(gearHitX, 0, renderer.getScreenWidth() - gearHitX, toybox::kHeaderHeight),
            chessui::ActionOpenSettings);

  const BoardGeometry geometry = boardGeometry();
  const int boardSize = geometry.squareSize * 8;
  for (int square = 0; square < 64; ++square) {
    drawSquareContents(geometry, square);
  }
  {
    // Corner brackets: the cartridge look is doubled lines and hard corners.
    const int outer = toybox::kBoardFrame + 8;
    const int arm = 34;
    const int bx = geometry.originX - outer;
    const int by = geometry.originY - outer;
    const int bw = boardSize + 2 * outer;
    for (int corner = 0; corner < 4; ++corner) {
      const int cx = (corner & 1) ? bx + bw - arm : bx;
      const int cy = (corner & 2) ? by + bw - 3 : by;
      renderer.fillRect(cx, cy, arm, 3, true);
      renderer.fillRect((corner & 1) ? bx + bw - 3 : bx, (corner & 2) ? by + bw - arm : by, 3, arm, true);
    }
  }

  // The selection frame is drawn OUTSIDE its square, so it overlaps the
  // neighbours. That means it has to come after every square is painted:
  // drawn inside drawSquareContents it was overdrawn by whichever adjacent
  // square happened to be rendered later, leaving a broken rectangle.
  if (selectedSquare >= 0) {
    const int sx = geometry.originX + screenColumnOf(selectedSquare) * geometry.squareSize;
    const int sy = geometry.originY + screenRowOf(selectedSquare) * geometry.squareSize;
    renderer.drawRect(sx - toybox::kFrame, sy - toybox::kFrame, geometry.squareSize + 2 * toybox::kFrame,
                      geometry.squareSize + 2 * toybox::kFrame, toybox::kFrame, true);
  }

  // The frame sits outside the squares so it never eats a rank.
  renderer.drawRect(geometry.originX - toybox::kBoardFrame, geometry.originY - toybox::kBoardFrame,
                    boardSize + 2 * toybox::kBoardFrame, boardSize + 2 * toybox::kBoardFrame, toybox::kBoardFrame,
                    true);

  // Black's losses sit above the board and White's below, so each side's spoils
  // appear on the side that took them.
  drawCapturedStrip(geometry.originY - toybox::kBoardFrame - toybox::kGutter - chessart::kSmallPieceSize, true);
  drawCapturedStrip(geometry.originY + boardSize + toybox::kBoardFrame + toybox::kGutter, false);

  // Solid only when the game is over: that transition warrants a full refresh
  // anyway, so the ink is free and the contrast is the payoff.
  const int statusY = statusPillY();
  const int listTop =
      geometry.originY + boardSize + toybox::kFrame + toybox::kGutter + chessart::kSmallPieceSize + toybox::kGutter;
  drawMoveList(listTop, statusY - listTop - toybox::kGutter);
  interactionsReady = true;
  toybox::reportOverflow(interactions, "Chess board");

  const auto labels = mappedInput.mapLabels("Back", "Select", "Up", "Down");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
