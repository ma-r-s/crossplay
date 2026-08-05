#pragma once

// What travels between two devices playing chess.
//
// The whole position, not the move. A chess position is 90 bytes as FEN, so the
// entire game fits in one packet with room to spare, and two boards cannot
// disagree because there is no move log to replay: a lost packet is a stale
// frame that the next one corrects.
//
// The last move rides along only so the receiving side's score sheet stays
// honest. A position says nothing about how it was reached.
//
// Freestanding on purpose, so host-tests/link/ can play a whole game through it.

struct ChessWire {
  char fen[90];
  char lastMove[10];
};
