#pragma once

#include "enums.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

/// One Event Formation Unit endpoint for a (detector, readout) pair.
struct RL_API SenderConfig {
  DetectorType detector_type{DetectorType::Reserved};
  ReadoutType readout_type{ReadoutType::CAEN};
  std::string ip_address{};
  uint16_t udp_port{0}; ///< event-packet port
  uint16_t tcp_port{0}; ///< command port

  auto operator<=>(const SenderConfig &) const = default;
};

/** \brief Explicit EFU endpoint configuration for replay, parsed from JSON.
 *
 * Replay resolves the EFU for each collector group from, in precedence
 * order: this explicit configuration, attributes embedded in the collector
 * file, then command-line defaults. The JSON layout is an object with one
 * `senders` array; each entry requires `detector_type` and `readout_type`
 * (bare or qualified enum names), `ip_address`, `udp_port`, and `tcp_port`.
 * Entries whose detector and readout do not belong together, and duplicate
 * (detector, readout) pairs, are rejected.
 */
class RL_API SenderConfigs {
public:
  SenderConfigs() = default;
  /// Equivalent to from_json().
  explicit SenderConfigs(const std::string & json_text);

  /// Parse a JSON document; throws std::runtime_error describing the first invalid entry.
  [[nodiscard]] static SenderConfigs from_json(const std::string & json_text);
  /// Read and parse a JSON file; throws std::runtime_error if unreadable or invalid.
  [[nodiscard]] static SenderConfigs from_file(const std::filesystem::path & path);

  /// Replace the held configurations with those parsed from json_text.
  void parse(const std::string & json_text);

  [[nodiscard]] bool empty() const noexcept { return configs.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return configs.size(); }
  /// True when an endpoint is configured for the pair.
  [[nodiscard]] bool contains(DetectorType detector_type, ReadoutType readout_type) const;
  /// The endpoint for the pair, or std::nullopt when not configured.
  [[nodiscard]] std::optional<SenderConfig> find(DetectorType detector_type, ReadoutType readout_type) const;
  /// The endpoint for the pair; throws std::out_of_range when not configured.
  [[nodiscard]] const SenderConfig & at(DetectorType detector_type, ReadoutType readout_type) const;

private:
  using key_type = std::pair<DetectorType, ReadoutType>;

  std::map<key_type, SenderConfig> configs{};
};
