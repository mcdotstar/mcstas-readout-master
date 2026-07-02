// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Canonical C-struct description strings for the EFU-sendable readout types
///
/// These strings are the single source of truth for the record layout that a
/// description-based collector must use to remain EFU-sendable: parsing one with
/// TypeDescriptionParser and building the HDF5 compound type must produce a type
/// exactly equal to hdf_compound_type(readout) — a unit test enforces this, so the
/// strings cannot drift from the C++ event structs in hdf_interface.h.
///
//===----------------------------------------------------------------------===//
#pragma once

#include "enums.h"

#ifdef WIN32
// Export symbols if compile flags "READOUT_SHARED" and "READOUT_EXPORT" are set on Windows.
    #ifdef READOUT_SHARED
        #ifdef READOUT_EXPORT
            #define RL_API __declspec(dllexport)
        #else
            #define RL_API __declspec(dllimport)
        #endif
    #else
        // Disable definition if linking statically.
        #define RL_API
    #endif
#else
// Disable definition for non-Win32 systems.
#define RL_API
#endif

/// \brief The canonical struct description for a sendable readout type
///
/// \returns a parseable C-struct field list whose compound type equals hdf_compound_type(readout),
///          or nullptr for types without a canonical description
RL_API const char * readout_type_description(ReadoutType readout);
