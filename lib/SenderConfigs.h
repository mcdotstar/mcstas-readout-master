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

struct RL_API SenderConfig {
  DetectorType detector_type{DetectorType::Reserved};
  ReadoutType readout_type{ReadoutType::CAEN};
  std::string ip_address{};
  uint16_t udp_port{0};
  uint16_t tcp_port{0};

  auto operator<=>(const SenderConfig &) const = default;
};

class RL_API SenderConfigs {
public:
  SenderConfigs() = default;
  explicit SenderConfigs(const std::string & json_text);

  [[nodiscard]] static SenderConfigs from_json(const std::string & json_text);
  [[nodiscard]] static SenderConfigs from_file(const std::filesystem::path & path);

  void parse(const std::string & json_text);

  [[nodiscard]] bool empty() const noexcept { return configs.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return configs.size(); }
  [[nodiscard]] bool contains(DetectorType detector_type, ReadoutType readout_type) const;
  [[nodiscard]] std::optional<SenderConfig> find(DetectorType detector_type, ReadoutType readout_type) const;
  [[nodiscard]] const SenderConfig & at(DetectorType detector_type, ReadoutType readout_type) const;

private:
  using key_type = std::pair<DetectorType, ReadoutType>;

  std::map<key_type, SenderConfig> configs{};
};
