#include "RssDesk.h"

#include <cstdio>

namespace rss {
namespace {

constexpr size_t kJobsKept = 8;

const char* stateName(const int state) {
  switch (state) {
    case 0:
      return "waiting";
    case 1:
      return "working";
    case 2:
      return "done";
    default:
      return "failed";
  }
}

bool blank(const std::string_view text) {
  for (const char c : text) {
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
  }
  return true;
}

}  // namespace

std::string jsonString(const std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char escaped[8];
          std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
          out += escaped;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
  return out;
}

int Desk::enqueue(const Request::Kind kind, const std::string_view text) {
  if (blank(text) || text.size() > kMaxRequestBytes) return -1;
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.size() >= kMaxQueued) return -1;
  Request request;
  request.kind = kind;
  request.job = nextJob_++;
  request.text = std::string(text);
  queue_.push_back(request);

  Job job;
  job.id = request.job;
  job.text = request.text;
  jobs_.push_back(std::move(job));
  while (jobs_.size() > kJobsKept) jobs_.pop_front();
  return request.job;
}

int Desk::requestAdd(const std::string_view address) { return enqueue(Request::Kind::Add, address); }

int Desk::requestRemove(const std::string_view url) { return enqueue(Request::Kind::Remove, url); }

bool Desk::take(Request& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (queue_.empty()) return false;
  out = std::move(queue_.front());
  queue_.pop_front();
  for (Job& job : jobs_) {
    if (job.id == out.job) job.state = Job::State::Working;
  }
  return true;
}

void Desk::finish(const int jobId, const bool ok, const std::string_view message) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (Job& job : jobs_) {
    if (job.id != jobId) continue;
    job.state = ok ? Job::State::Done : Job::State::Failed;
    job.message = std::string(message);
  }
}

void Desk::publish(const std::vector<Feed>& feeds, const std::vector<Post>& posts) {
  std::vector<Row> rows;
  rows.reserve(feeds.size());
  for (const Feed& feed : feeds) {
    Row row;
    row.url = feed.url;
    row.title = feed.title;
    row.failed = feed.failed;
    const std::string key = keyFor(feed.url);
    for (const Post& post : posts) {
      if (post.feed == key) ++row.posts;
    }
    rows.push_back(std::move(row));
  }
  std::lock_guard<std::mutex> lock(mutex_);
  feeds_ = std::move(rows);
}

void Desk::clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  feeds_.clear();
  queue_.clear();
  jobs_.clear();
}

std::string Desk::stateJson() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string out = "{\"max\":";
  out += std::to_string(kMaxFeeds);
  out += ",\"feeds\":[";
  for (size_t i = 0; i < feeds_.size(); ++i) {
    const Row& row = feeds_[i];
    if (i > 0) out.push_back(',');
    out += "{\"url\":";
    out += jsonString(row.url);
    out += ",\"title\":";
    out += jsonString(row.title);
    out += ",\"failed\":";
    out += row.failed ? "true" : "false";
    out += ",\"posts\":";
    out += std::to_string(row.posts);
    out.push_back('}');
  }
  out += "],\"jobs\":[";
  for (size_t i = 0; i < jobs_.size(); ++i) {
    const Job& job = jobs_[i];
    if (i > 0) out.push_back(',');
    out += "{\"id\":";
    out += std::to_string(job.id);
    out += ",\"state\":\"";
    out += stateName(static_cast<int>(job.state));
    out += "\",\"text\":";
    out += jsonString(job.text);
    out += ",\"message\":";
    out += jsonString(job.message);
    out.push_back('}');
  }
  out += "]}";
  return out;
}

}  // namespace rss
