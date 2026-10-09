#pragma once

// TRMNL's rules, with nothing in them that needs a radio, a card or a panel.
//
// The reader speaks the TRMNL device API, the same three calls a TRMNL panel
// makes, so any server that drives one drives this: trmnl.app, Terminus,
// LaraPaper, or anything else that answers /api/setup and /api/display.
//
//   GET /api/setup    ID: <device id>            -> api_key, friendly_id
//   GET /api/display  ID, Access-Token, ...      -> image_url, filename,
//                                                    refresh_rate
//   GET <image_url>                              -> a BMP or a PNG
//
// Everything here is host-tested in host-tests/trmnl: the settings file, the
// server address a person types, the JSON a server answers with, and how long
// to wait before asking again.

#include <cstddef>
#include <cstdint>
#include <string>

namespace trmnl {

// --- Settings -------------------------------------------------------------

constexpr const char* kDefaultServer = "https://trmnl.app";

// How the panel is held. Landscape is TRMNL's own shape (800x480); portrait
// asks the server for 480x800, which servers that render per device honour.
enum class Orientation : uint8_t { Landscape = 0, LandscapeFlipped = 1, Portrait = 2, PortraitFlipped = 3 };
constexpr int kOrientationCount = 4;

struct Config {
  std::string server = kDefaultServer;
  // What the server knows this reader by. Empty means the reader's own Wi-Fi
  // MAC address, which is what a TRMNL panel sends.
  std::string deviceId;
  // The Access-Token. Empty means "ask /api/setup for one".
  std::string apiKey;
  // Minutes between images; 0 means whatever the server's refresh_rate says.
  int refreshMinutes = 0;
  Orientation orientation = Orientation::Landscape;
  // A clean (flashing) refresh every this many images, fast ones between. 1 is
  // every image, which is what a TRMNL panel does.
  int cleanEvery = 1;
  // Bring Wi-Fi down between images. Joining costs a few seconds per image and
  // saves the radio's draw for the whole wait.
  bool wifiOff = true;
  // Keep the reader awake while the screen is up. Off lets the reader's own
  // sleep timeout end the session like any other app.
  bool keepAwake = true;
};

constexpr int kMaxRefreshMinutes = 24 * 60;
constexpr size_t kMaxFieldBytes = 160;
constexpr size_t kMaxConfigBytes = 1024;

// key=value lines. Unknown keys are ignored and every value is clamped, so a
// file written by hand or by an older build reads as the nearest valid config.
Config parseConfig(const std::string& text);
std::string formatConfig(const Config& config);

// What the reader remembers between visits, beside the settings: what the
// server called it, which picture is on the card, and the server's last word.
struct State {
  std::string friendlyId;
  std::string filename;  // the server's name for the picture on the card
  int refreshRate = 0;   // the server's last refresh_rate, seconds
  std::string message;   // the last failure, for the phone page and the home screen
  // The device ID the API key was issued for. A key only speaks for the device
  // it came with, so a key held under any other ID is dropped before use.
  std::string keyFor;
};
State parseState(const std::string& text);
std::string formatState(const State& state);

// The address a person typed, made into a base URL: trimmed, given https:// if
// it has no scheme, and stripped of trailing slashes and a trailing /api, so
// "trmnl.app/api/" and "https://trmnl.app" are the same server. Empty when
// nothing usable is left.
std::string normalizeServer(const std::string& typed);
// "trmnl.app" or "192.168.1.20:2300": the server as a person reads it.
std::string serverLabel(const std::string& server);
// The base URL joined to an API path ("/api/display").
std::string apiUrl(const std::string& server, const char* path);
// An image_url as the server wrote it, made absolute against the server.
// Self-hosted servers often answer with a path.
std::string resolveUrl(const std::string& server, const std::string& url);

// "AA:BB:CC:DD:EE:FF" from six bytes.
std::string formatMac(const uint8_t mac[6]);
// What the reader sends as ID: the configured one, else the MAC.
std::string effectiveDeviceId(const Config& config, const std::string& mac);
// False for an empty MAC or the all-zero one a radio reports before it starts:
// sending that as ID would hand the reader whichever device first claimed it.
bool usableDeviceId(const std::string& id);

// Whether the saved key may be sent as `deviceId`'s Access-Token.
bool keyBelongsTo(const Config& config, const State& state, const std::string& deviceId);

// What a save from the phone does to the rest. Another server or another ID is
// another device: its key, name and last picture go, unless the same save also
// brought a new key, which is then taken as that device's. True when `after`
// was changed and needs writing back.
bool applyPhoneSave(const Config& before, Config& after, State& state, const std::string& mac);

// Logical width and height to ask the server for.
int requestWidth(Orientation orientation);
int requestHeight(Orientation orientation);
const char* orientationName(Orientation orientation);

// --- What the server says ---------------------------------------------------

struct SetupReply {
  bool ok = false;
  std::string apiKey;
  std::string friendlyId;
  std::string message;
};

struct DisplayReply {
  bool ok = false;  // the JSON parsed and carried an image
  int status = 0;   // the body's own "status"; 0 is fine on TRMNL's API
  std::string imageUrl;
  std::string filename;
  int refreshRate = 0;  // seconds; 0 when absent
  bool resetCredentials = false;
  std::string message;  // "error" or "message", for the screen
};

// A flat JSON object's fields, read without a JSON library so this stays
// freestanding. Nested values are skipped, not read.
SetupReply parseSetup(const std::string& json);
DisplayReply parseDisplay(const std::string& json);

// Exposed for the tests: one top-level field of a flat object. False when the
// key is absent or the body is not an object.
bool jsonString(const std::string& json, const char* key, std::string& out);
bool jsonNumber(const std::string& json, const char* key, long long& out);
bool jsonBool(const std::string& json, const char* key, bool& out);

// --- Images ---------------------------------------------------------------

enum class ImageKind : uint8_t { Unknown, Bmp, Png };
ImageKind sniffImage(const uint8_t* head, size_t len);

// --- Time ---------------------------------------------------------------

constexpr uint32_t kDefaultIntervalSeconds = 15 * 60;
constexpr uint32_t kMinIntervalSeconds = 60;
constexpr uint32_t kMaxIntervalSeconds = 24 * 3600;

// How long to show an image before asking for the next: the setting when it is
// set, else the server's refresh_rate, clamped, else fifteen minutes.
uint32_t intervalSeconds(const Config& config, int serverRefreshRate);

// After `failures` failed attempts in a row (1 after the first): one minute,
// doubling, never past half an hour and never past the interval itself, so a
// server that is down is not asked every minute for a day and a one-minute
// dashboard does not slow to thirty.
uint32_t retrySeconds(int failures, uint32_t interval);

// Whether this image, the `shown`th since the screen was opened (1-based),
// gets a clean refresh.
bool cleanRefresh(int shown, int cleanEvery);

// "every 15 min", "every hour", "every 4 hours", "every day".
std::string everyPhrase(uint32_t seconds);
// "in 3 min", "in under a minute", "in 2 hours".
std::string inPhrase(uint32_t seconds);

}  // namespace trmnl
