#include <catch2/catch_test_macros.hpp>

#include <iostream>
#include <sstream>
#include <string>

#include "ReadoutClass.h"
#include "Sender.h"
#include "efu_time.h"

namespace {
/// Capture what is written to std::cerr for the lifetime of this object.
class CaptureCerr {
public:
  CaptureCerr(): old(std::cerr.rdbuf(buffer.rdbuf())) {}
  ~CaptureCerr() { std::cerr.rdbuf(old); }
  std::string str() const { return buffer.str(); }
private:
  std::ostringstream buffer;
  std::streambuf * old;
};

// Nothing listens here; UDP sends to a closed port fail silently, and the event
// time under test is recorded before anything is sent.
constexpr int nobody{47999};

bool contains(const std::string & text, const std::string & part) {
  return text.find(part) != std::string::npos;
}

// The time an event was stamped at, measured from the pulse it was stamped against.
template<class T>
efu_time stamped(const T & sender) {
  return efu_time(sender.lastEventTime()) - efu_time(sender.lastPulseTime());
}
}

TEST_CASE("Readout folds a long time-of-flight into the pulse frame, and says so", "[readout][fold]") {
  const efu_time period(1.0 / 14.0);
  const efu_time pulse(1'700'000'000u, 0u);
  const double tof{0.165};  // BIFROST, source to detector: more than two frames
  CAEN_readout_t data{};

  SECTION("folded") {
    CaptureCerr err;
    {
      Readout readout("127.0.0.1", nobody, 0, 0x34, period, pulse);
      readout.fold_tof(true);
      readout.addReadout(0, 0, tof, 1.0, &data);
      readout.addReadout(0, 0, tof, 1.0, &data);
      CHECK(stamped(readout) == efu_time(tof) % period);
      CHECK(stamped(readout) < period);
      CHECK(readout.long_tof_count() == 2);
    }
    const auto text = err.str();
    CHECK(contains(text, "a readout has a time-of-flight of at least one pulse period"));
    CHECK(contains(text, "2 readout(s)"));
    CHECK(contains(text, "folded"));
  }

  SECTION("unfolded, as before") {
    CaptureCerr err;
    {
      Readout readout("127.0.0.1", nobody, 0, 0x34, period, pulse);
      readout.addReadout(0, 0, tof, 1.0, &data);
      CHECK(stamped(readout) == efu_time(tof));
      CHECK(readout.long_tof_count() == 1);
    }
    CHECK(contains(err.str(), "MaxTOFNS"));
  }

  SECTION("a time within the frame is not remarked on") {
    CaptureCerr err;
    {
      Readout readout("127.0.0.1", nobody, 0, 0x34, period, pulse);
      readout.fold_tof(true);
      readout.addReadout(0, 0, 0.01, 1.0, &data);
      CHECK(stamped(readout) == efu_time(0.01));
      CHECK(readout.long_tof_count() == 0);
    }
    CHECK(err.str().empty());
  }

  SECTION("silent means silent") {
    CaptureCerr err;
    {
      Readout readout("127.0.0.1", nobody, 0, 0x34, period, pulse);
      readout.verbose(Verbosity::silent);
      readout.addReadout(0, 0, tof, 1.0, &data);
      CHECK(readout.long_tof_count() == 1);
    }
    CHECK(err.str().empty());
  }
}

TEST_CASE("Sender counts and reports a long time-of-flight", "[sender][fold]") {
  const efu_time period(1.0 / 14.0);
  const double tof{0.165};
  CAEN_readout_t data{};

  for (const bool fold: {false, true}) {
    CaptureCerr err;
    {
      Sender sender("127.0.0.1", nobody, 0, DetectorType::BIFROST, ReadoutType::CAEN, period);
      sender.fold_tof(fold);
      sender.addReadout(0, 0, tof, 1.0, &data);
      sender.addReadout(0, 0, 0.01, 1.0, &data);
      CHECK(sender.long_tof_count() == 1);
    }
    const auto text = err.str();
    CHECK(contains(text, "1 readout(s)"));
    CHECK(contains(text, fold ? "folded" : "MaxTOFNS"));
  }
}
