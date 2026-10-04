#include "StudyBridgeFile.h"

#include <HalStorage.h>
#include <Logging.h>

namespace study {

bool writeBridgeStateFile(const std::string& raw) {
  if (!Storage.ensureDirectoryExists(kStudyDir)) {
    LOG_ERR("STUDYSYNC", "cannot create %s", kStudyDir);
    return false;
  }
  // Write beside it and rename. Opening the real path truncates first, so a
  // power cut mid-write left an unparseable .bridge, which reads exactly like
  // a device that was never paired: the next sync walked the user through
  // pairing again for no reason they could see.
  const std::string tempPath = std::string(kBridgeStatePath) + ".part";
  {
    HalFile file;
    if (!Storage.openFileForWrite("STUDYSYNC", tempPath.c_str(), file)) {
      LOG_ERR("STUDYSYNC", "cannot write %s", tempPath.c_str());
      return false;
    }
    if (file.write(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()) != raw.size()) {
      LOG_ERR("STUDYSYNC", "short write to %s", tempPath.c_str());
      return false;
    }
  }
  Storage.remove(kBridgeStatePath);
  if (!Storage.rename(tempPath.c_str(), kBridgeStatePath)) {
    LOG_ERR("STUDYSYNC", "cannot rename %s into place", tempPath.c_str());
    return false;
  }
  return true;
}

}  // namespace study
