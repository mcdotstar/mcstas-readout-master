#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include "enums.h"

// ---- DetectorType conversions ----

TEST_CASE("detectorType_from_int round-trips all known types", "[enums]") {
  const std::vector<std::pair<int, DetectorType>> known = {
    {0x00, Reserved},   {0x10, TTLMonitor}, {0x30, LOKI},
    {0x32, TBL3H3},     {0x34, BIFROST},    {0x38, MIRACLES},
    {0x3c, CSPEC},       {0x40, TREX},       {0x42, NMX},
    {0x48, FREIA},       {0x49, TBLVMM},     {0x4c, ESTIA},
    {0x50, BEER},        {0x60, DREAM},       {0x64, MAGIC},
    {0x68, HEIMDAL},     {0xf0, CBM0},        {0xf1, CBM1},
    {0xf2, CBM2},        {0xfa, CBMI},
  };
  for (const auto& [val, expected] : known) {
    CHECK(detectorType_from_int(val) == expected);
  }
}

TEST_CASE("detectorType_from_int rejects unknown values", "[enums]") {
  CHECK_THROWS_AS(detectorType_from_int(0xFF), std::runtime_error);
  CHECK_THROWS_AS(detectorType_from_int(-1), std::runtime_error);
}

// ---- ReadoutType from DetectorType ----

TEST_CASE("readoutType_from_detectorType maps CAEN detectors", "[enums]") {
  for (auto dt : {LOKI, TBL3H3, BIFROST, MIRACLES, CSPEC}) {
    CHECK(readoutType_from_detectorType(dt) == ReadoutType::CAEN);
  }
}

TEST_CASE("readoutType_from_detectorType maps VMM3 detectors", "[enums]") {
  for (auto dt : {TREX, NMX, FREIA, TBLVMM, ESTIA}) {
    CHECK(readoutType_from_detectorType(dt) == ReadoutType::VMM3);
  }
}

TEST_CASE("readoutType_from_detectorType maps CDT detectors", "[enums]") {
  for (auto dt : {DREAM, MAGIC, HEIMDAL}) {
    CHECK(readoutType_from_detectorType(dt) == ReadoutType::CDT);
  }
}

TEST_CASE("readoutType_from_detectorType maps beam monitors", "[enums]") {
  CHECK(readoutType_from_detectorType(CBM0) == ReadoutType::BM0);
  CHECK(readoutType_from_detectorType(CBMI) == ReadoutType::BMI);
  for (auto dt : {CBM1, CBM2, BEER}) {
    CHECK(readoutType_from_detectorType(dt) == ReadoutType::BM2);
  }
}

TEST_CASE("readoutType_from_detectorType maps TTLMonitor", "[enums]") {
  CHECK(readoutType_from_detectorType(TTLMonitor) == ReadoutType::TTLMonitor);
}

TEST_CASE("readoutType_from_int composes correctly", "[enums]") {
  CHECK(readoutType_from_int(0x34) == ReadoutType::CAEN);
  CHECK(readoutType_from_int(0x10) == ReadoutType::TTLMonitor);
  CHECK(readoutType_from_int(0x60) == ReadoutType::CDT);
}

// ---- Name conversions ----

TEST_CASE("detectorType_name round-trips with detectorType_from_name", "[enums]") {
  const std::vector<DetectorType> all_types = {
    TTLMonitor, LOKI, TBL3H3, BIFROST, MIRACLES, CSPEC, TREX, NMX,
    FREIA, TBLVMM, ESTIA, BEER, DREAM, MAGIC, HEIMDAL, CBM0, CBM1, CBM2, CBMI,
  };
  for (auto dt : all_types) {
    auto name = detectorType_name(dt);
    CHECK(detectorType_from_name(name) == dt);
  }
}

TEST_CASE("readoutType_name round-trips with readoutType_from_name", "[enums]") {
  const std::vector<ReadoutType> all_types = {
    ReadoutType::TTLMonitor, ReadoutType::CAEN, ReadoutType::VMM3,
    ReadoutType::CDT, ReadoutType::BM0, ReadoutType::BM2, ReadoutType::BMI,
  };
  for (auto rt : all_types) {
    auto name = readoutType_name(rt);
    CHECK(readoutType_from_name(name) == rt);
  }
}

TEST_CASE("detectorType_from_name rejects unknown names", "[enums]") {
  CHECK_THROWS_WITH(
    detectorType_from_name("DetectorType::FakeDetector"),
    Catch::Matchers::ContainsSubstring("not a known DetectorType")
  );
}

TEST_CASE("readoutType_from_name rejects unknown names", "[enums]") {
  CHECK_THROWS_WITH(
    readoutType_from_name("ReadoutType::FakeReadout"),
    Catch::Matchers::ContainsSubstring("not a known ReadoutType")
  );
}
