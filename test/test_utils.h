#pragma once
#include <atomic>
#include <cstdint>

class UDPStats {
public:
  std::atomic<int> packets{0};
  std::atomic<int> readouts{0};
  // packets whose ESS cookie or detector-type byte did not match expectations:
  // a real EFU would reject these, so tests must assert bad == 0
  std::atomic<int> bad{0};
};

/// The ESS packet header starts with CookieAndType = (detector_type << 24) + 'ESS':
/// return true when the cookie is right and the type byte matches the expected detector.
inline bool ess_header_ok(const uint32_t cookie_and_type, const int expected_detector_type) {
  const auto type = cookie_and_type >> 24;
  const auto cookie = cookie_and_type - (type << 24);
  return cookie == 0x535345 && static_cast<int>(type) == expected_detector_type;
}
