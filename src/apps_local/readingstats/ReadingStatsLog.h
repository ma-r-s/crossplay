#pragma once

// The card side of Reading Stats: writing a session as the reader closes it,
// and reading both files back for the app. The rules are in ReadingStatsCore.h;
// this is the HalStorage and clock glue around them.
//
// recordSession() is the reader's only contact with the fork. It is called from
// ReaderActivity::flushReaderSession() (upstream's file, one fork-local call),
// which runs when the reader exits, before the device sleeps, and before a
// KOReader sync -- every point a session ends. flushReaderSession() resets the
// session straight after, so a session is handed over once.

#include <cstdint>
#include <string>

class ReaderSession;

namespace readingstats {

class Tally;

constexpr const char* kSessionsPath = "/.crosspoint/reading-sessions.dat";
constexpr const char* kFinishedPath = "/.crosspoint/reading-finished.dat";

// Writes the session if the reader counted it (ReaderSession::isEmitWorthy:
// ten seconds of reading against a trusted clock), and a finished record the
// first time this year a session reached the end of the book. `endPercent` is
// the reader's own end-of-page progress (ScreenshotInfo::endProgressPercent):
// 100 on the book's last page and on its end-of-book screen.
void recordSession(const std::string& bookPath, const std::string& title, const ReaderSession& session, int endPercent);

// Today's local day, or false while the clock is not trusted -- the same clock
// the reader stamps sessions with, so the screen and the records agree on what
// "today" means.
bool localToday(int32_t& day);

// Both files into `tally`. Missing files are an empty history, not an error.
void loadTally(Tally& tally);

}  // namespace readingstats
