#include "QrUtils.h"

#include <Memory.h>
#include <Utf8.h>
#include <qrcode.h>

#include <algorithm>

#include "Logging.h"

namespace {
constexpr uint8_t versionForBytes(const size_t len) {
  return len <= 78 ? 4 : len <= 271 ? 10 : len <= 858 ? 20 : len <= 1732 ? 30 : 40;
}
static_assert(versionForBytes(78) == 4 && versionForBytes(79) == 10);
static_assert(versionForBytes(271) == 10 && versionForBytes(272) == 20);
static_assert(versionForBytes(858) == 20 && versionForBytes(859) == 30);
static_assert(versionForBytes(1732) == 30 && versionForBytes(1733) == 40);
}  // namespace

void QrUtils::drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload) {
  // Choose by byte-mode capacity: payloads may contain lowercase or UTF-8.
  size_t len = textPayload.length();

  // Truncate to max QR capacity at a UTF-8 safe boundary to avoid splitting multi-byte sequences
  static constexpr size_t MAX_QR_CAPACITY = 2953;  // Version 40, ECC_LOW, byte mode
  std::string truncated;
  const char* payload = textPayload.c_str();
  if (len > MAX_QR_CAPACITY) {
    len = utf8SafeTruncateBuffer(textPayload.c_str(), static_cast<int>(MAX_QR_CAPACITY));
    truncated = textPayload.substr(0, len);
    payload = truncated.c_str();
  }

  const uint8_t version = versionForBytes(len);

  // Make sure we have a large enough buffer on the heap to avoid blowing the stack
  uint32_t bufferSize = qrcode_getBufferSize(version);
  auto qrcodeBytes = makeUniqueNoThrow<uint8_t[]>(bufferSize);
  if (!qrcodeBytes) {
    LOG_ERR("QR", "OOM: %u bytes", static_cast<unsigned>(bufferSize));
    return;
  }

  QRCode qrcode;
  // Initialize the QR code. We use ECC_LOW for max capacity.
  int8_t res = qrcode_initBytes(&qrcode, qrcodeBytes.get(), version, ECC_LOW,
                                reinterpret_cast<uint8_t*>(const_cast<char*>(payload)), static_cast<uint16_t>(len));

  if (res == 0) {
    // Determine the optimal pixel size.
    const int maxDim = std::min(bounds.width, bounds.height);

    int px = maxDim / qrcode.size;
    if (px < 1) px = 1;

    // Calculate centering X and Y
    const int qrDisplaySize = qrcode.size * px;
    const int xOff = bounds.x + (bounds.width - qrDisplaySize) / 2;
    const int yOff = bounds.y + (bounds.height - qrDisplaySize) / 2;

    // Draw the QR Code
    for (uint8_t cy = 0; cy < qrcode.size; cy++) {
      for (uint8_t cx = 0; cx < qrcode.size; cx++) {
        if (qrcode_getModule(&qrcode, cx, cy)) {
          renderer.fillRect(xOff + px * cx, yOff + px * cy, px, px, true);
        }
      }
    }
  } else {
    // If it fails (e.g. text too large), log an error
    LOG_ERR("QR", "Text too large for QR Code version %d", version);
  }
}
