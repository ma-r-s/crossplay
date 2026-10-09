// TRMNL's rules, checked without a radio.
//
// The parts that decide what the reader sends and how long it waits: the
// settings file, the address a person types on the phone, the JSON a server
// answers with, and the backoff.

#include <cstdio>
#include <string>

#include "TrmnlCore.h"

using namespace trmnl;

static int checks = 0;
static int failures = 0;

#define CHECK(cond)                                               \
  do {                                                            \
    ++checks;                                                     \
    if (!(cond)) {                                                \
      ++failures;                                                 \
      std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                             \
  } while (0)

namespace {

void testConfigRoundTrip() {
  Config config;
  config.server = "http://192.168.1.20:2300";
  config.deviceId = "AA:BB:CC:00:11:22";
  config.apiKey = "abc123";
  config.refreshMinutes = 30;
  config.orientation = Orientation::Portrait;
  config.cleanEvery = 5;
  config.wifiOff = false;
  config.keepAwake = false;
  const Config back = parseConfig(formatConfig(config));
  CHECK(back.server == config.server);
  CHECK(back.deviceId == config.deviceId);
  CHECK(back.apiKey == config.apiKey);
  CHECK(back.refreshMinutes == 30);
  CHECK(back.orientation == Orientation::Portrait);
  CHECK(back.cleanEvery == 5);
  CHECK(!back.wifiOff);
  CHECK(!back.keepAwake);
}

void testConfigDefaultsAndClamps() {
  const Config empty = parseConfig("");
  CHECK(empty.server == kDefaultServer);
  CHECK(empty.deviceId.empty());
  CHECK(empty.apiKey.empty());
  CHECK(empty.refreshMinutes == 0);
  CHECK(empty.orientation == Orientation::Landscape);
  CHECK(empty.cleanEvery == 1);
  CHECK(empty.wifiOff);
  CHECK(empty.keepAwake);

  const Config wild = parseConfig(
      "server=   \n"
      "refresh_minutes=-4\n"
      "orientation=9\n"
      "clean_every=0\n"
      "nonsense=1\n"
      "api_key=  key with spaces  \r\n");
  CHECK(wild.server == kDefaultServer);  // blank falls back, never empty
  CHECK(wild.refreshMinutes == 0);
  CHECK(wild.orientation == Orientation::PortraitFlipped);
  CHECK(wild.cleanEvery == 1);
  CHECK(wild.apiKey == "key with spaces");

  CHECK(parseConfig("refresh_minutes=99999").refreshMinutes == kMaxRefreshMinutes);
  // A value can never smuggle a second line into the file.
  Config sneaky;
  sneaky.apiKey = "a\nserver=http://evil";
  CHECK(parseConfig(formatConfig(sneaky)).server == kDefaultServer);
}

void testState() {
  State state;
  state.friendlyId = "917F0B";
  state.filename = "plugin-2025-10-06T14:05:00";
  state.refreshRate = 900;
  state.message = "The server said: Device not found.";
  state.keyFor = "AA:BB:CC:00:11:22";
  const State back = parseState(formatState(state));
  CHECK(back.keyFor == state.keyFor);
  CHECK(back.friendlyId == state.friendlyId);
  CHECK(back.filename == state.filename);
  CHECK(back.refreshRate == 900);
  CHECK(back.message == state.message);
  CHECK(parseState("refresh_rate=-3").refreshRate == 0);
}

void testNormalizeServer() {
  CHECK(normalizeServer("trmnl.app") == "https://trmnl.app");
  CHECK(normalizeServer(" https://trmnl.app/ ") == "https://trmnl.app");
  CHECK(normalizeServer("https://trmnl.app/api/") == "https://trmnl.app");
  CHECK(normalizeServer("HTTP://192.168.1.20:2300") == "http://192.168.1.20:2300");
  CHECK(normalizeServer("http://pi.local:4567/trmnl") == "http://pi.local:4567/trmnl");
  CHECK(normalizeServer("").empty());
  CHECK(normalizeServer("https://").empty());
  CHECK(normalizeServer("ftp://x").empty());
  CHECK(normalizeServer("my server").empty());
  CHECK(serverLabel("https://trmnl.app") == "trmnl.app");
  CHECK(serverLabel("http://192.168.1.20:2300") == "192.168.1.20:2300");
  CHECK(apiUrl("https://trmnl.app", "/api/display") == "https://trmnl.app/api/display");
}

void testResolveUrl() {
  CHECK(resolveUrl("https://trmnl.app", "https://cdn.example/x.bmp") == "https://cdn.example/x.bmp");
  CHECK(resolveUrl("http://10.0.0.2:2300", "/assets/screens/a.png") == "http://10.0.0.2:2300/assets/screens/a.png");
  CHECK(resolveUrl("http://10.0.0.2:2300/sub", "/a.png") == "http://10.0.0.2:2300/a.png");
  CHECK(resolveUrl("http://10.0.0.2:2300/sub", "a.png") == "http://10.0.0.2:2300/sub/a.png");
  CHECK(resolveUrl("http://10.0.0.2", "//cdn.example/a.png") == "http://cdn.example/a.png");
}

void testDeviceId() {
  const uint8_t mac[6] = {0xde, 0xad, 0x0b, 0xee, 0xf0, 0x01};
  CHECK(formatMac(mac) == "DE:AD:0B:EE:F0:01");
  Config config;
  CHECK(effectiveDeviceId(config, "11:22") == "11:22");
  config.deviceId = "MINE";
  CHECK(effectiveDeviceId(config, "11:22") == "MINE");
  CHECK(requestWidth(Orientation::Landscape) == 800 && requestHeight(Orientation::Landscape) == 480);
  CHECK(requestWidth(Orientation::PortraitFlipped) == 480 && requestHeight(Orientation::PortraitFlipped) == 800);
}

void testKeysFollowTheDevice() {
  CHECK(!usableDeviceId(""));
  CHECK(!usableDeviceId("00:00:00:00:00:00"));
  CHECK(usableDeviceId("DC:DA:0C:12:34:56"));
  CHECK(usableDeviceId("my-reader"));

  Config config;
  config.apiKey = "k";
  State state;
  // A key saved before the reader recorded whose it was is not trusted.
  CHECK(!keyBelongsTo(config, state, "DC:DA:0C:12:34:56"));
  state.keyFor = "00:00:00:00:00:00";
  CHECK(!keyBelongsTo(config, state, "DC:DA:0C:12:34:56"));
  state.keyFor = "DC:DA:0C:12:34:56";
  CHECK(keyBelongsTo(config, state, "DC:DA:0C:12:34:56"));

  const std::string mac = "DC:DA:0C:12:34:56";
  // A new ID with the old key: the key goes, and so does the old device's name.
  {
    Config before;
    before.apiKey = "peters";
    Config after = before;
    after.deviceId = "AA:AA:AA:AA:AA:AA";
    State s;
    s.friendlyId = "PETER1";
    s.keyFor = mac;
    s.filename = "x";
    CHECK(applyPhoneSave(before, after, s, mac));
    CHECK(after.apiKey.empty());
    CHECK(s.friendlyId.empty() && s.filename.empty());
  }
  // A new ID with a new key: the key is taken as the new ID's.
  {
    Config before;
    before.apiKey = "old";
    Config after = before;
    after.deviceId = "AA:AA:AA:AA:AA:AA";
    after.apiKey = "new";
    State s;
    CHECK(!applyPhoneSave(before, after, s, mac));
    CHECK(after.apiKey == "new");
    CHECK(s.keyFor == "AA:AA:AA:AA:AA:AA");
  }
  // A new key alone belongs to the reader's own ID.
  {
    Config before;
    Config after = before;
    after.apiKey = "typed";
    State s;
    applyPhoneSave(before, after, s, mac);
    CHECK(s.keyFor == mac);
  }
  // Nothing about the device changed: the key stays.
  {
    Config before;
    before.apiKey = "k";
    Config after = before;
    after.refreshMinutes = 5;
    State s;
    s.keyFor = mac;
    s.filename = "keep";
    CHECK(!applyPhoneSave(before, after, s, mac));
    CHECK(after.apiKey == "k" && s.keyFor == mac && s.filename == "keep");
  }
}

void testDisplayReply() {
  // TRMNL's own shape.
  const DisplayReply cloud =
      parseDisplay(R"({"status":0,"image_url":"https:\/\/trmnl.s3.us-east-2.amazonaws.com\/plugin-2025.bmp",)"
                   R"("filename":"plugin-2025","refresh_rate":900,"reset_firmware":false,"update_firmware":false,)"
                   R"("firmware_url":null,"special_function":"sleep"})");
  CHECK(cloud.ok);
  CHECK(cloud.status == 0);
  CHECK(cloud.imageUrl == "https://trmnl.s3.us-east-2.amazonaws.com/plugin-2025.bmp");
  CHECK(cloud.filename == "plugin-2025");
  CHECK(cloud.refreshRate == 900);
  CHECK(!cloud.resetCredentials);

  // A self-hosted server: status 200, a quoted rate, a path for the image,
  // nested values in front of the ones we want.
  const DisplayReply local =
      parseDisplay(R"({ "meta": {"image_url": "wrong", "list": [1, {"x": "]"}]}, "status": 200,)"
                   R"( "refresh_rate": "300", "image_url": "/assets/screens/A1B2.png", "filename": "A1B2.png" })");
  CHECK(local.ok);
  CHECK(local.imageUrl == "/assets/screens/A1B2.png");
  CHECK(local.refreshRate == 300);

  // A refusal carries its own sentence, and no image.
  const DisplayReply refused = parseDisplay(R"({"status":500,"error":"Device not found","reset_firmware":true})");
  CHECK(!refused.ok);
  CHECK(refused.status == 500);
  CHECK(refused.message == "Device not found");
  CHECK(refused.resetCredentials);

  CHECK(!parseDisplay("").ok);
  CHECK(!parseDisplay("<html>502 Bad Gateway</html>").ok);
  CHECK(!parseDisplay(R"({"status":0,"image_url":)").ok);

  std::string text;
  CHECK(jsonString(R"({"a":"café \"q\" \\"})", "a", text) && text == "caf\xc3\xa9 \"q\" \\");
  long long number = 0;
  CHECK(!jsonNumber(R"({"a":"x"})", "a", number));
  bool flag = true;
  CHECK(jsonBool(R"({"a" : false})", "a", flag) && !flag);
}

void testSetupReply() {
  const SetupReply ok = parseSetup(
      R"({"status":200,"api_key":"2r--SahjsAKCFksVcped2Q","friendly_id":"917F0B","image_url":"x","filename":"empty_state"})");
  CHECK(ok.ok);
  CHECK(ok.apiKey == "2r--SahjsAKCFksVcped2Q");
  CHECK(ok.friendlyId == "917F0B");

  const SetupReply unknown = parseSetup(R"({"status":404,"api_key":null,"message":"MAC Address not registered"})");
  CHECK(!unknown.ok);
  CHECK(unknown.message == "MAC Address not registered");
}

void testSniff() {
  const uint8_t bmp[] = {'B', 'M', 0, 0};
  const uint8_t png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0};
  const uint8_t html[] = {'<', 'h', 't', 'm', 'l'};
  CHECK(sniffImage(bmp, sizeof(bmp)) == ImageKind::Bmp);
  CHECK(sniffImage(png, sizeof(png)) == ImageKind::Png);
  CHECK(sniffImage(html, sizeof(html)) == ImageKind::Unknown);
  CHECK(sniffImage(png, 3) == ImageKind::Unknown);
}

void testTiming() {
  Config config;
  CHECK(intervalSeconds(config, 0) == kDefaultIntervalSeconds);
  CHECK(intervalSeconds(config, 300) == 300);
  CHECK(intervalSeconds(config, 5) == kMinIntervalSeconds);
  CHECK(intervalSeconds(config, 999999) == kMaxIntervalSeconds);
  config.refreshMinutes = 30;
  CHECK(intervalSeconds(config, 300) == 1800);  // the setting wins over the server

  CHECK(retrySeconds(1, 3600) == 60);
  CHECK(retrySeconds(2, 3600) == 120);
  CHECK(retrySeconds(5, 3600) == 960);
  CHECK(retrySeconds(50, 3600) == 1800);
  CHECK(retrySeconds(3, 60) == 60);  // never slower than the dashboard itself
  CHECK(retrySeconds(0, 900) == 900);

  CHECK(cleanRefresh(1, 5));
  CHECK(!cleanRefresh(2, 5));
  CHECK(!cleanRefresh(5, 5));
  CHECK(cleanRefresh(6, 5));
  CHECK(cleanRefresh(7, 1));

  CHECK(everyPhrase(900) == "every 15 min");
  CHECK(everyPhrase(3600) == "every hour");
  CHECK(everyPhrase(4 * 3600) == "every 4 hours");
  CHECK(everyPhrase(86400) == "every day");
  CHECK(everyPhrase(60) == "every minute");
  CHECK(inPhrase(20) == "in under a minute");
  CHECK(inPhrase(14 * 60 + 50) == "in 15 min");
  CHECK(inPhrase(3 * 3600) == "in 3 hours");
}

}  // namespace

int main() {
  testConfigRoundTrip();
  testConfigDefaultsAndClamps();
  testState();
  testNormalizeServer();
  testResolveUrl();
  testDeviceId();
  testKeysFollowTheDevice();
  testDisplayReply();
  testSetupReply();
  testSniff();
  testTiming();
  std::printf("trmnl: %d checks, %d failed\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
