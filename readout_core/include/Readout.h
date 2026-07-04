#pragma once

/** \file Readout.h
 * \brief Umbrella header for the C API used by the McStas components.
 *
 * Pulls in the three C interfaces: the original runtime-streaming readout
 * (readout_orig.h), the description-based HDF5 collector (readout_collector.h),
 * and the discrete fixed-count sampler (readout_discrete.h).
 */

#include "readout_orig.h"
#include "readout_collector.h"
#include "readout_discrete.h"