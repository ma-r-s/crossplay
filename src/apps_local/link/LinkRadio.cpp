#include "LinkRadio.h"

#include <cstdlib>
#include <cstring>

// Logging belongs to the firmware, and it reaches Arduino.h. Outside a firmware
// build (host-tests/link/) it compiles away, so the socket transport below can
// be tested for real rather than mocked.
#if defined(ARDUINO_ARCH_ESP32) || defined(SIMULATOR)
#include <Logging.h>
#else
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#endif

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace linkplay {
namespace {

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)

// The ESP-NOW receive callback is a bare C function pointer with nowhere to
// hang user data, so the active radio has to be reachable from file scope.
// Only one match runs at a time, so one slot is enough.
Radio* activeRadio = nullptr;

void onEspNowReceive(const esp_now_recv_info_t* info, const uint8_t* data, const int length) {
  if (activeRadio != nullptr && info != nullptr) activeRadio->enqueueFromCallback(info->src_addr, data, length);
}

#else

// Two simulator instances on one Mac need to find each other with no broker and
// no configuration. Each binds the first free port in this range, and the port
// becomes the low half of its address -- so host election ("lower address
// wins") falls out as "whoever started first", which is deterministic and
// needs no negotiation, exactly as it will be from MACs on the device.
constexpr uint16_t kDefaultBasePort = 45700;
constexpr int kSlots = 8;

// One range per worktree, because the range is the discovery mechanism.
//
// Several trees now build and test at once, and this transport is real UDP on
// real loopback: a broadcast from one tree's test run is delivered to another
// tree's, so their radios find each other and the suites fail on whichever
// assertion the stray datagram happened to break. That reads as a flaky link
// test rather than as interference -- three concurrent runs failed on three
// different lines, and every one of them passed when run alone.
//
// LINKPLAY_BASE_PORT gives each tree its own slice. check.sh sets it from the
// tree path; unset, the behaviour is exactly as before, which is what two
// simulator windows in one tree still need in order to see each other.
uint16_t basePort() {
  static const uint16_t port = [] {
    const char* override = std::getenv("LINKPLAY_BASE_PORT");
    if (override == nullptr) return kDefaultBasePort;
    const int parsed = std::atoi(override);
    // A range that would collide with the ephemeral ports, or wrap past 65535
    // with kSlots to spare, is worse than ignoring the override.
    if (parsed < 1024 || parsed > 65535 - kSlots) return kDefaultBasePort;
    return static_cast<uint16_t>(parsed);
  }();
  return port;
}

Address addressForPort(const uint16_t port) {
  Address address;
  address.bytes[0] = 0x02;  // locally administered, same as a soft MAC
  address.bytes[4] = static_cast<uint8_t>(port >> 8);
  address.bytes[5] = static_cast<uint8_t>(port & 0xFF);
  return address;
}

uint16_t portForAddress(const Address& address) {
  return static_cast<uint16_t>((static_cast<uint16_t>(address.bytes[4]) << 8) | address.bytes[5]);
}

#endif

}  // namespace

Radio::~Radio() { end(); }

// ---------------------------------------------------------------------------
// Shared: the receive ring.
// ---------------------------------------------------------------------------

void Radio::enqueueFromCallback(const uint8_t* sourceMac, const uint8_t* data, const int length) {
  if (sourceMac == nullptr || data == nullptr) return;
  if (length <= 0 || static_cast<size_t>(length) > kMaxPacketBytes) return;

  const uint8_t tail = tail_.load(std::memory_order_relaxed);
  const uint8_t next = static_cast<uint8_t>((tail + 1) % kQueueDepth);
  if (next == head_.load(std::memory_order_acquire)) {
    // Dropping costs latency, not state: the session retransmits until it is
    // acknowledged. Recorded so a saturated ring is diagnosable rather than
    // silently slow.
    overflowed_.store(true, std::memory_order_relaxed);
    return;
  }

  Datagram& slot = ring_[tail];
  memcpy(slot.from.bytes, sourceMac, sizeof(slot.from.bytes));
  memcpy(slot.data, data, static_cast<size_t>(length));
  slot.length = static_cast<uint16_t>(length);
  tail_.store(next, std::memory_order_release);
}

// ---------------------------------------------------------------------------

#if defined(ARDUINO_ARCH_ESP32) && !defined(SIMULATOR)

bool Radio::begin() {
  end();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);

  if (esp_wifi_set_channel(kChannel, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    LOG_ERR("LINK", "could not set channel %u", kChannel);
    end();
    return false;
  }
  // ESP-NOW has exactly one global receive callback, so only one thing on the
  // device may own the radio at a time. Nothing else does today. The FreeInk
  // SDK ships `NearbyTransfer`, an ESP-NOW file-transfer library that neither
  // CrossMux nor CrossPoint currently consumes; if either adopts it, whichever
  // registered last silently wins and this link goes deaf. See
  // docs/crosspoint-migration.md.
  if (esp_now_init() != ESP_OK) {
    LOG_ERR("LINK", "esp_now_init failed");
    end();
    return false;
  }

  activeRadio = this;
  if (esp_now_register_recv_cb(&onEspNowReceive) != ESP_OK) {
    LOG_ERR("LINK", "could not register receive callback");
    end();
    return false;
  }

  // Discovery is a broadcast, and ESP-NOW requires even the broadcast address
  // to be a registered peer before anything will go out.
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, kBroadcast.bytes, sizeof(kBroadcast.bytes));
  peer.channel = kChannel;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  const esp_err_t added = esp_now_add_peer(&peer);
  if (added != ESP_OK && added != ESP_ERR_ESPNOW_EXIST) {
    LOG_ERR("LINK", "could not add broadcast peer (%d)", static_cast<int>(added));
    end();
    return false;
  }

  // Sleep between wake windows rather than listening flat out. The device
  // cannot auto-sleep during a match (that would end it), so the radio is not
  // the dominant draw -- but there is no reason to pay for RX we do not use.
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  esp_now_set_wake_window(kWakeWindowMs);

  if (esp_wifi_get_mac(WIFI_IF_STA, local_.bytes) != ESP_OK) {
    LOG_ERR("LINK", "could not read local MAC");
    end();
    return false;
  }

  started_ = true;
  LOG_INF("LINK", "radio up on channel %u as %02X:%02X:%02X:%02X:%02X:%02X", kChannel, local_.bytes[0], local_.bytes[1],
          local_.bytes[2], local_.bytes[3], local_.bytes[4], local_.bytes[5]);
  return true;
}

void Radio::end() {
  if (started_) {
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    esp_wifi_set_ps(WIFI_PS_NONE);
  }
  if (activeRadio == this) activeRadio = nullptr;
  started_ = false;
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  overflowed_.store(false, std::memory_order_relaxed);
  WiFi.mode(WIFI_OFF);
}

bool Radio::send(const Address& to, const uint8_t* data, const size_t length) {
  if (!started_ || data == nullptr || length == 0 || length > kMaxPacketBytes) return false;

  if (!esp_now_is_peer_exist(to.bytes)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, to.bytes, sizeof(to.bytes));
    peer.channel = kChannel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    const esp_err_t added = esp_now_add_peer(&peer);
    // A full peer table means somebody is running a very crowded room. Failing
    // the send is right: the session retransmits, and by then the table may
    // have room.
    if (added != ESP_OK && added != ESP_ERR_ESPNOW_EXIST) return false;
  }
  return esp_now_send(to.bytes, data, length) == ESP_OK;
}

bool Radio::receive(Address& from, uint8_t* buffer, const size_t capacity, size_t& length) {
  if (!started_ || buffer == nullptr) return false;
  while (true) {
    const uint8_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) return false;

    const Datagram& slot = ring_[head];
    const bool fits = slot.length <= capacity;
    if (fits) {
      from = slot.from;
      memcpy(buffer, slot.data, slot.length);
      length = slot.length;
    }
    head_.store(static_cast<uint8_t>((head + 1) % kQueueDepth), std::memory_order_release);
    // A packet too big for the caller is dropped and the loop moves on, rather
    // than being truncated or stalling the drain behind it.
    if (fits) return true;
  }
}

#else

// ---------------------------------------------------------------------------
// Simulator: UDP on localhost, so two instances on one Mac can play.
// ---------------------------------------------------------------------------

bool Radio::begin() {
  end();

  socket_ = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socket_ < 0) {
    LOG_ERR("LINK", "could not open socket");
    return false;
  }

  // Deliberately no SO_REUSEADDR: a bind that fails is how we discover that
  // another instance already owns that slot.
  int slot = 0;
  for (; slot < kSlots; ++slot) {
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(basePort() + slot));
    if (::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) break;
  }
  if (slot == kSlots) {
    LOG_ERR("LINK", "no free port in %u..%u", basePort(), basePort() + kSlots - 1);
    end();
    return false;
  }

  const int flags = ::fcntl(socket_, F_GETFL, 0);
  if (flags < 0 || ::fcntl(socket_, F_SETFL, flags | O_NONBLOCK) < 0) {
    LOG_ERR("LINK", "could not set non-blocking");
    end();
    return false;
  }

  local_ = addressForPort(static_cast<uint16_t>(basePort() + slot));
  started_ = true;
  LOG_INF("LINK", "radio up on udp/%d", basePort() + slot);
  return true;
}

void Radio::end() {
  if (socket_ >= 0) ::close(socket_);
  socket_ = -1;
  started_ = false;
  head_.store(0, std::memory_order_relaxed);
  tail_.store(0, std::memory_order_relaxed);
  overflowed_.store(false, std::memory_order_relaxed);
}

bool Radio::send(const Address& to, const uint8_t* data, const size_t length) {
  if (!started_ || data == nullptr || length == 0 || length > kMaxPacketBytes) return false;

  sockaddr_in destination = {};
  destination.sin_family = AF_INET;
  destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (to == kBroadcast) {
    // There is no loopback broadcast worth relying on, so a broadcast is a send
    // to every slot but our own. With eight slots that is cheap, and it gives
    // the same semantics the device gets for free.
    const uint16_t mine = portForAddress(local_);
    for (int slot = 0; slot < kSlots; ++slot) {
      const uint16_t port = static_cast<uint16_t>(basePort() + slot);
      if (port == mine) continue;
      destination.sin_port = htons(port);
      ::sendto(socket_, data, length, 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    }
    return true;
  }

  destination.sin_port = htons(portForAddress(to));
  return ::sendto(socket_, data, length, 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination)) ==
         static_cast<ssize_t>(length);
}

bool Radio::receive(Address& from, uint8_t* buffer, const size_t capacity, size_t& length) {
  if (!started_ || buffer == nullptr) return false;
  while (true) {
    sockaddr_in source = {};
    socklen_t sourceLength = sizeof(source);
    uint8_t scratch[kMaxPacketBytes];
    const ssize_t received =
        ::recvfrom(socket_, scratch, sizeof(scratch), 0, reinterpret_cast<sockaddr*>(&source), &sourceLength);
    if (received <= 0) return false;
    if (static_cast<size_t>(received) > capacity) continue;  // drop, never truncate

    // The sender's address comes from the socket rather than the packet, so a
    // instance cannot claim to be somebody else by writing it into the payload.
    from = addressForPort(ntohs(source.sin_port));
    memcpy(buffer, scratch, static_cast<size_t>(received));
    length = static_cast<size_t>(received);
    return true;
  }
}

#endif

}  // namespace linkplay
