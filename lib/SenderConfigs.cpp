#include "SenderConfigs.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace {

using json = nlohmann::json;

std::string entry_prefix(const std::size_t index) {
  std::ostringstream os;
  os << "senders[" << index << "]: ";
  return os.str();
}

std::string qualify_detector_name(const std::string & name) {
  if (name.rfind("DetectorType::", 0) == 0) {
    return name;
  }
  return "DetectorType::" + name;
}

std::string qualify_readout_name(const std::string & name) {
  if (name.rfind("ReadoutType::", 0) == 0) {
    return name;
  }
  return "ReadoutType::" + name;
}

const json & require_field(const json & object, const char * field_name, const std::size_t index) {
  if (const auto it = object.find(field_name); it != object.end()) {
    return *it;
  }
  throw std::runtime_error(entry_prefix(index) + "missing required field '" + field_name + "'");
}

std::string require_string(const json & object, const char * field_name, const std::size_t index) {
  const auto & value = require_field(object, field_name, index);
  if (!value.is_string()) {
    throw std::runtime_error(entry_prefix(index) + "field '" + field_name + "' must be a string");
  }

  auto result = value.get<std::string>();
  if (result.empty()) {
    throw std::runtime_error(entry_prefix(index) + "field '" + field_name + "' must not be empty");
  }
  return result;
}

uint16_t require_port(const json & object, const char * field_name, const std::size_t index) {
  const auto & value = require_field(object, field_name, index);
  if (!value.is_number_integer()) {
    throw std::runtime_error(entry_prefix(index) + "field '" + field_name + "' must be an integer between 0 and 65535");
  }

  const auto port = value.get<int64_t>();
  if (port < 0 || port > 65535) {
    throw std::runtime_error(entry_prefix(index) + "field '" + field_name + "' must be in the range [0, 65535]");
  }

  return static_cast<uint16_t>(port);
}

DetectorType parse_detector_type(const json & object, const std::size_t index) {
  try {
    return detectorType_from_name(qualify_detector_name(require_string(object, "detector_type", index)));
  } catch (const std::exception & ex) {
    throw std::runtime_error(entry_prefix(index) + ex.what());
  }
}

ReadoutType parse_readout_type(const json & object, const std::size_t index) {
  try {
    return readoutType_from_name(qualify_readout_name(require_string(object, "readout_type", index)));
  } catch (const std::exception & ex) {
    throw std::runtime_error(entry_prefix(index) + ex.what());
  }
}

void validate_detector_readout_pair(const SenderConfig & config, const std::size_t index) {
  const auto expected_readout = readoutType_from_detectorType(config.detector_type);
  if (config.readout_type != expected_readout) {
    throw std::runtime_error(
        entry_prefix(index)
        + "readout_type " + readoutType_name(config.readout_type)
        + " does not match detector_type " + detectorType_name(config.detector_type)
        + "; expected " + readoutType_name(expected_readout)
    );
  }
}

}

SenderConfigs::SenderConfigs(const std::string & json_text) {
  parse(json_text);
}

SenderConfigs SenderConfigs::from_json(const std::string & json_text) {
  return SenderConfigs(json_text);
}

SenderConfigs SenderConfigs::from_file(const std::filesystem::path & path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("Could not open sender configuration file '" + path.string() + "'");
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return SenderConfigs(buffer.str());
}

void SenderConfigs::parse(const std::string & json_text) {
  json root;
  try {
    root = json::parse(json_text);
  } catch (const json::exception & ex) {
    throw std::runtime_error(std::string("Failed to parse sender configuration JSON: ") + ex.what());
  }

  if (!root.is_object()) {
    throw std::runtime_error("Sender configuration JSON must be a JSON object");
  }

  const auto senders_it = root.find("senders");
  if (senders_it == root.end()) {
    throw std::runtime_error("Sender configuration JSON must contain a 'senders' array");
  }
  if (!senders_it->is_array()) {
    throw std::runtime_error("Sender configuration field 'senders' must be an array");
  }

  std::map<key_type, SenderConfig> parsed_configs;
  for (std::size_t index = 0; index < senders_it->size(); ++index) {
    const auto & sender_json = (*senders_it)[index];
    if (!sender_json.is_object()) {
      throw std::runtime_error(entry_prefix(index) + "each sender entry must be a JSON object");
    }

    SenderConfig config;
    config.detector_type = parse_detector_type(sender_json, index);
    config.readout_type = parse_readout_type(sender_json, index);
    config.ip_address = require_string(sender_json, "ip_address", index);
    config.udp_port = require_port(sender_json, "udp_port", index);
    config.tcp_port = require_port(sender_json, "tcp_port", index);
    validate_detector_readout_pair(config, index);

    const key_type key{config.detector_type, config.readout_type};
    if (!parsed_configs.emplace(key, config).second) {
      throw std::runtime_error(
          entry_prefix(index)
          + "duplicate configuration for " + detectorType_name(config.detector_type)
          + " and " + readoutType_name(config.readout_type)
      );
    }
  }

  configs = std::move(parsed_configs);
}

bool SenderConfigs::contains(const DetectorType detector_type, const ReadoutType readout_type) const {
  return configs.contains({detector_type, readout_type});
}

std::optional<SenderConfig> SenderConfigs::find(const DetectorType detector_type, const ReadoutType readout_type) const {
  if (const auto it = configs.find({detector_type, readout_type}); it != configs.end()) {
    return it->second;
  }
  return std::nullopt;
}

const SenderConfig & SenderConfigs::at(const DetectorType detector_type, const ReadoutType readout_type) const {
  if (const auto it = configs.find({detector_type, readout_type}); it != configs.end()) {
    return it->second;
  }

  throw std::runtime_error(
      "No Sender configuration for " + detectorType_name(detector_type)
      + " and " + readoutType_name(readout_type)
  );
}
