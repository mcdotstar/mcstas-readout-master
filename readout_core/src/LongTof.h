#pragma once
// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Reporting readouts whose time-of-flight is at least one pulse period
///
/// Shared by Readout and Sender, which stamp an event at pulse + tof, or at
/// pulse + (tof % period) when folding. A time-of-flight of a period or more
/// means the event belongs to a later frame than the pulse it is stamped
/// against; an EFU rejects such times beyond its MaxTOFNS configuration value,
/// so they are worth saying something about either way.
///
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <iostream>

#include "efu_time.h"

namespace long_tof {

/// Whether a time-of-flight reaches into a later frame than the pulse it follows.
inline bool is_long(const double tof, const efu_time & period) {
  return tof >= static_cast<double>(period.total_nanoseconds()) * 1e-9;
}

/// Say what happened to `count` long-time-of-flight readouts, if there were any.
/// `first` reports the first of them as it happens; otherwise this is the summary.
inline void report(const char * who, const uint64_t count, const efu_time & period,
                   const bool folded, const bool first) {
  if (count == 0) return;
  const double ms = static_cast<double>(period.total_nanoseconds()) * 1e-6;
  std::cerr << who << ": ";
  if (first) {
    std::cerr << "a readout has a time-of-flight of at least one pulse period (" << ms << " ms)";
  } else {
    std::cerr << count << " readout(s) had a time-of-flight of at least one pulse period ("
              << ms << " ms)";
  }
  if (folded) {
    std::cerr << "; folded to (tof mod period), the frame it would be detected in. "
                 "Report times from the pulse the neutron arrives in to avoid this.";
  } else {
    std::cerr << "; sent unfolded. An EFU rejects times beyond its MaxTOFNS "
                 "configuration value.";
  }
  std::cerr << std::endl;
}

}  // namespace long_tof
