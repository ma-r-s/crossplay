// The network, canned.
//
// The browser build has no sockets, and the two apps that want the internet --
// Hacker News and the Connections daily -- are the two that look most broken
// without it: one shows NO LUCK, the other offers a download that cannot
// happen. Faking them is honest here in a way it would not be on the device,
// because the page's claim is "this is the firmware", not "this is online".
//
// So this replaces HttpDownloader for the browser build only. Every request is
// answered from files under /canned on the preloaded card, captured from the
// real endpoints on the day the card was built. Nothing in src/ or lib/
// changes; build.py skips the real HttpDownloader.cpp and compiles this
// instead.
//
// A URL with no canned answer fails exactly as an unreachable host would, and
// logs the URL so filling the gap is mechanical rather than detective work.

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <string>

#include "../../../src/network/HttpDownloader.h"

namespace {

constexpr size_t kChunk = 1024;

bool startsWith(const std::string& value, const char* prefix) { return value.rfind(prefix, 0) == 0; }

// Prefix match rather than a hash of the URL: the files stay legible on the
// card, and one canned item can answer every story id.
std::string cannedPathFor(const std::string& url) {
  if (startsWith(url, "https://hn.algolia.com/api/v1/search")) return "/canned/hn-front.json";
  if (startsWith(url, "https://hn.algolia.com/api/v1/items/")) return "/canned/hn-item.json";
  // r.jina.ai fronts an arbitrary article; one piece of prose answers for all
  // of them, which is what a demo needs and all it needs.
  if (startsWith(url, "https://r.jina.ai/")) return "/canned/hn-article.txt";
  if (url.find("NYT-Connections-Answers") != std::string::npos) return "/canned/connections.json";
  return "";
}

// One reader for all four entry points, so a URL that works through one of them
// works through every one.
bool readCanned(const std::string& url, const HttpDownloader::DataCallback& onData) {
  const std::string path = cannedPathFor(url);
  if (path.empty()) {
    LOG_ERR("HTTPCAN", "no canned answer for %s", url.c_str());
    return false;
  }
  HalFile file;
  if (!Storage.openFileForRead("HTTPCAN", path.c_str(), file)) {
    LOG_ERR("HTTPCAN", "%s is missing from the card", path.c_str());
    return false;
  }
  uint8_t buffer[kChunk];
  while (true) {
    const int read = file.read(buffer, sizeof(buffer));
    if (read <= 0) break;
    if (!onData(buffer, static_cast<size_t>(read))) return false;
  }
  LOG_INF("HTTPCAN", "served %s from %s", url.c_str(), path.c_str());
  return true;
}

}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string&, const std::string&) {
  outContent.clear();
  return readCanned(url, [&outContent](const uint8_t* data, const size_t len) {
    outContent.append(reinterpret_cast<const char*>(data), len);
    return true;
  });
}

bool HttpDownloader::fetchUrl(const std::string& url, Stream& stream, const std::string&, const std::string&) {
  return readCanned(url, [&stream](const uint8_t* data, const size_t len) {
    stream.write(data, len);
    return true;
  });
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string&,
                              const std::string&) {
  return readCanned(url, onData);
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string&, const std::string&) {
  HalFile out;
  if (!Storage.openFileForWrite("HTTPCAN", destPath.c_str(), out)) return FILE_ERROR;
  size_t written = 0;
  bool aborted = false;
  const bool ok = readCanned(url, [&](const uint8_t* data, const size_t len) {
    if (cancelFlag != nullptr && *cancelFlag) {
      aborted = true;
      return false;
    }
    out.write(data, len);
    written += len;
    if (progress) progress(written, 0);
    return true;
  });
  if (aborted) return ABORTED;
  return ok ? OK : HTTP_ERROR;
}
