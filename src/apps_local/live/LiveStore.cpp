#include "LiveStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

namespace live {

bool load(State& out) {
  out = State{};
  HalFile file;
  if (!Storage.openFileForRead("LIVE", kStatePath, file)) return false;
  std::string raw;
  raw.resize(file.size());
  const bool read = file.read(reinterpret_cast<uint8_t*>(raw.data()), raw.size()) == static_cast<int>(raw.size());
  file.close();
  if (!read) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) != DeserializationError::Ok) {
    LOG_ERR("LIVE", "%s is not readable; treating this reader as unpaired", kStatePath);
    return false;
  }
  out.deviceToken = doc["deviceToken"] | "";
  out.fridgeId = doc["fridgeId"] | "";
  out.etag = doc["etag"] | "";
  out.on = doc["on"] | false;
  // Through the clamp on the way IN as well as on the way out. A file edited by
  // hand, or written by a build whose limits differed, is an input like any
  // other -- and a zero here would busy-wake the radio flat overnight.
  out.intervalSeconds = clampInterval(doc["intervalSeconds"] | static_cast<int64_t>(kDefaultIntervalSeconds));
  // NOT through the clamp unconditionally, unlike the line above.
  // clampInterval(0) is fifteen minutes, so clamping an absent key would turn
  // "this card predates the field" into a cadence nobody chose and print it on
  // the panel. 0 stays 0 and Schedule::cadence answers with the interval.
  const int64_t storedCadence = doc["cadenceSeconds"] | static_cast<int64_t>(0);
  out.cadenceSeconds = storedCadence > 0 ? clampInterval(storedCadence) : 0;
  out.lastAttemptEpoch = doc["lastAttemptEpoch"] | static_cast<int64_t>(0);
  out.lastSuccessEpoch = doc["lastSuccessEpoch"] | static_cast<int64_t>(0);
  out.consecutiveFailures = doc["consecutiveFailures"] | 0;
  if (out.consecutiveFailures < 0) out.consecutiveFailures = 0;
  return true;
}

bool save(const State& state) {
  JsonDocument doc;
  doc["deviceToken"] = state.deviceToken;
  doc["fridgeId"] = state.fridgeId;
  doc["etag"] = state.etag;
  doc["on"] = state.on;
  doc["intervalSeconds"] = state.intervalSeconds;
  doc["cadenceSeconds"] = state.cadenceSeconds;
  doc["lastAttemptEpoch"] = state.lastAttemptEpoch;
  doc["lastSuccessEpoch"] = state.lastSuccessEpoch;
  doc["consecutiveFailures"] = state.consecutiveFailures;
  std::string raw;
  serializeJson(doc, raw);

  const std::string tempPath = std::string(kStatePath) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("LIVE", tempPath.c_str(), file)) {
      LOG_ERR("LIVE", "cannot write %s", tempPath.c_str());
      return false;
    }
    const bool ok =
        file.write(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()) == static_cast<int>(raw.size());
    file.close();
    if (!ok) {
      LOG_ERR("LIVE", "short write to %s", tempPath.c_str());
      return false;
    }
  }
  Storage.remove(kStatePath);
  if (!Storage.rename(tempPath.c_str(), kStatePath)) {
    LOG_ERR("LIVE", "cannot rename %s into place", tempPath.c_str());
    return false;
  }
  return true;
}

void forgetPairing(State& state) {
  state.deviceToken.clear();
  state.fridgeId.clear();
  // The ETag goes too. Keeping it would mean the next pairing's first pull
  // claims to already hold an image belonging to a fridge that no longer
  // exists, and the service would have no reason to disbelieve it -- a brand
  // new setup that shows nothing, with a 304 in the log saying everything is
  // fine.
  state.etag.clear();
  state.lastAttemptEpoch = 0;
  state.lastSuccessEpoch = 0;
  state.consecutiveFailures = 0;
  // `on` is deliberately untouched. A 401 disconnects a reader; it does not
  // mean the user stopped wanting Live, and a toggle that flipped itself off
  // would hide the fact that anything happened.
}

}  // namespace live
