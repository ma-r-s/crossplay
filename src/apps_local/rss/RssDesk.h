#pragma once

// Between the phone page and the app: what the phone asked for, what the app
// has done about it, and the list of feeds the page shows.
//
// ---------------------------------------------------------------------------
// The web server never touches a feed. It writes requests here and reads
// answers from here, and the app -- which owns the feeds, the card and the
// radio -- takes the requests one at a time from its own loop.
//
// Two reasons, either of which would be enough. Adding a feed means fetching
// it, which takes seconds, and a handler that fetches holds the page's request
// open and the panel frozen for all of them; this way the reader paints
// "ADDING" first and the page polls for the answer. And the simulator serves
// on a thread of its own, so a handler that edited the feed list would race
// the loop that draws it. Everything here is behind one mutex, and nothing
// here does any work.
//
// Freestanding: host-tests/rss/ drives it, including the JSON the page reads.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "RssCore.h"

namespace rss {

struct Request {
  enum class Kind : uint8_t { Add, Remove };
  Kind kind = Kind::Add;
  int job = 0;
  // An address a person typed (Add), or a feed's url exactly as the page was
  // given it (Remove).
  std::string text;
};

class Desk {
 public:
  // How many requests may wait at once. A page cannot queue a hundred adds
  // behind a slow one.
  static constexpr size_t kMaxQueued = 4;
  // What the page may send. An address, not a document.
  static constexpr size_t kMaxRequestBytes = 2048;

  // --- The page's side ---------------------------------------------------
  // Each returns the job number the page polls for, or -1 when it was refused
  // (empty, too long, too many waiting).
  int requestAdd(std::string_view address);
  int requestRemove(std::string_view url);
  // Everything the page draws, as JSON:
  //   {"max":30,"feeds":[{"url":..,"title":..,"failed":false,"posts":20}],
  //    "jobs":[{"id":3,"state":"working","text":..,"message":..}]}
  std::string stateJson() const;

  // --- The app's side ----------------------------------------------------
  void publish(const std::vector<Feed>& feeds, const std::vector<Post>& posts);
  // The next request, marked as being worked on. False when there is none.
  bool take(Request& out);
  void finish(int job, bool ok, std::string_view message);
  void clear();

 private:
  struct Job {
    int id = 0;
    std::string text;
    std::string message;
    enum class State : uint8_t { Waiting, Working, Done, Failed } state = State::Waiting;
  };
  struct Row {
    std::string url;
    std::string title;
    bool failed = false;
    int posts = 0;
  };
  int enqueue(Request::Kind kind, std::string_view text);

  mutable std::mutex mutex_;
  std::vector<Row> feeds_;
  std::deque<Request> queue_;
  // The last few jobs, so a page that polls a second late still learns how its
  // request ended.
  std::deque<Job> jobs_;
  int nextJob_ = 1;
};

// A string as a JSON string literal, quotes included. Control characters are
// escaped; everything else, UTF-8 included, passes through.
std::string jsonString(std::string_view text);

}  // namespace rss
