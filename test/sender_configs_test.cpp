#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <SenderConfigs.h>

#include <filesystem>
#include <fstream>

TEST_CASE("SenderConfigs parses and looks up sender endpoints", "[sender-configs]") {
  const auto json_text = R"JSON(
{
  "senders": [
    {
      "detector_type": "BIFROST",
      "readout_type": "CAEN",
      "ip_address": "127.0.0.1",
      "udp_port": 9000,
      "tcp_port": 8888
    },
    {
      "detector_type": "DetectorType::NMX",
      "readout_type": "ReadoutType::VMM3",
      "ip_address": "192.168.1.44",
      "udp_port": 9010,
      "tcp_port": 8890
    }
  ]
}
)JSON";

  const auto configs = SenderConfigs::from_json(json_text);

  REQUIRE(configs.size() == 2);
  REQUIRE(configs.contains(DetectorType::BIFROST, ReadoutType::CAEN));
  REQUIRE_FALSE(configs.contains(DetectorType::BIFROST, ReadoutType::VMM3));

  const auto bifrost = configs.at(DetectorType::BIFROST, ReadoutType::CAEN);
  CHECK(bifrost.ip_address == "127.0.0.1");
  CHECK(bifrost.udp_port == 9000);
  CHECK(bifrost.tcp_port == 8888);

  const auto maybe_nmx = configs.find(DetectorType::NMX, ReadoutType::VMM3);
  REQUIRE(maybe_nmx.has_value());
  CHECK(maybe_nmx->ip_address == "192.168.1.44");
  CHECK(maybe_nmx->udp_port == 9010);
  CHECK(maybe_nmx->tcp_port == 8890);

  REQUIRE_FALSE(configs.find(DetectorType::NMX, ReadoutType::CAEN).has_value());
  REQUIRE_THROWS_WITH(
      configs.at(DetectorType::NMX, ReadoutType::CAEN),
      Catch::Matchers::ContainsSubstring("No Sender configuration")
  );
}

TEST_CASE("SenderConfigs loads from a JSON file", "[sender-configs]") {
  const auto temp_path = std::filesystem::temp_directory_path() / "sender-configs-test.json";
  std::ofstream output(temp_path);
  REQUIRE(output.good());
  output << R"JSON(
{
  "senders": [
    {
      "detector_type": "TTLMonitor",
      "readout_type": "TTLMonitor",
      "ip_address": "10.0.0.5",
      "udp_port": 7777,
      "tcp_port": 8889
    }
  ]
}
)JSON";
  output.close();

  const auto cleanup = [&temp_path]() {
    std::error_code error;
    std::filesystem::remove(temp_path, error);
  };

  const auto configs = SenderConfigs::from_file(temp_path);
  cleanup();

  REQUIRE(configs.contains(DetectorType::TTLMonitor, ReadoutType::TTLMonitor));
  const auto monitor = configs.at(DetectorType::TTLMonitor, ReadoutType::TTLMonitor);
  CHECK(monitor.ip_address == "10.0.0.5");
  CHECK(monitor.udp_port == 7777);
  CHECK(monitor.tcp_port == 8889);
}

TEST_CASE("SenderConfigs rejects invalid configuration entries", "[sender-configs]") {
  SECTION("missing required field") {
    const auto json_text = R"JSON(
{
  "senders": [
    {
      "detector_type": "BIFROST",
      "readout_type": "CAEN",
      "udp_port": 9000,
      "tcp_port": 8888
    }
  ]
}
)JSON";

    REQUIRE_THROWS_WITH(
        SenderConfigs::from_json(json_text),
        Catch::Matchers::ContainsSubstring("missing required field 'ip_address'")
    );
  }

  SECTION("duplicate detector and readout pair") {
    const auto json_text = R"JSON(
{
  "senders": [
    {
      "detector_type": "BIFROST",
      "readout_type": "CAEN",
      "ip_address": "127.0.0.1",
      "udp_port": 9000,
      "tcp_port": 8888
    },
    {
      "detector_type": "DetectorType::BIFROST",
      "readout_type": "ReadoutType::CAEN",
      "ip_address": "127.0.0.2",
      "udp_port": 9001,
      "tcp_port": 8889
    }
  ]
}
)JSON";

    REQUIRE_THROWS_WITH(
        SenderConfigs::from_json(json_text),
        Catch::Matchers::ContainsSubstring("duplicate configuration")
    );
  }

  SECTION("unknown enum names") {
    const auto json_text = R"JSON(
{
  "senders": [
    {
      "detector_type": "MadeUpDetector",
      "readout_type": "CAEN",
      "ip_address": "127.0.0.1",
      "udp_port": 9000,
      "tcp_port": 8888
    }
  ]
}
)JSON";

    REQUIRE_THROWS_WITH(
        SenderConfigs::from_json(json_text),
        Catch::Matchers::ContainsSubstring("not a known DetectorType")
    );
  }

  SECTION("mismatched detector and readout types") {
    const auto json_text = R"JSON(
{
  "senders": [
    {
      "detector_type": "BIFROST",
      "readout_type": "VMM3",
      "ip_address": "127.0.0.1",
      "udp_port": 9000,
      "tcp_port": 8888
    }
  ]
}
)JSON";

    REQUIRE_THROWS_WITH(
        SenderConfigs::from_json(json_text),
        Catch::Matchers::ContainsSubstring("does not match detector_type")
    );
  }
}
