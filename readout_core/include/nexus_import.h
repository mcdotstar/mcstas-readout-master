// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Import readout records from a McStas NeXus output file into a collector file
///
/// Collector* components run with `--format=NeXus` (sink "auto" or "nexus") store
/// their records in the McStas NeXus output file of the run, one typed dataset per
/// record field under `entryN/instrument/components/NNNN_name/<group>` (an
/// NXcollection with attribute `type="Readouts"`). This importer rebuilds the
/// cue-based collector file layout from that, so the result can be validated,
/// combined and replayed like a file written by the HDF5 sink.
///
//===----------------------------------------------------------------------===//
#pragma once

#include <string>

#ifdef WIN32
    #ifdef READOUT_SHARED
        #ifdef READOUT_EXPORT
            #define RL_API __declspec(dllexport)
        #else
            #define RL_API __declspec(dllimport)
        #endif
    #else
        #define RL_API
    #endif
#else
#define RL_API
#endif

/// \brief Convert the readout groups of one McStas NeXus file into a new collector file.
///
/// Every readout group becomes a collector group (named after its NeXus group; prefixed
/// with the component name when two components used the same name) with its record
/// description, ESS detector type, normalization and optional EFU routing attributes.
/// The instrument parameters of the run, typed from the instrument's "Parameters"
/// attribute, become the collector file's parameters, and every CollectorDiskChopper
/// with a `tdc_pv` adds the `<name>_chopper_tdc` parameter, as the component does
/// with the HDF5 sink.
///
/// \param out_filename   collector file to create (must not exist)
/// \param nexus_filename McStas NeXus output file (mccode.h5)
/// \param verbose        print what is imported
/// \returns the number of collector groups written, or -1 on error
RL_API int import_mccode_nexus(const std::string & out_filename, const std::string & nexus_filename, bool verbose);
