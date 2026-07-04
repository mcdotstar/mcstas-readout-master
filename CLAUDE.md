# 2026-07-02 Status

This project is the interface between the simulation world (McStas based) and the ESS data
acquisition world (EPICS, Kafka and the EFU). The redevelopment from runtime event streaming
to decoupled collect → combine → replay is **complete**; see AGENT.md for the architecture
and PLAN.md for how it got here.

In one paragraph: `Collector{ReadoutType}.comp` components (share/Readout/) store weighted-ray
records into cue-based multi-point HDF5 files through a description-based engine (a C-struct
description string parsed to an HDF5 compound datatype at runtime; canonical strings in
lib/readout_type_descriptions.h). Files are combined with the `readout-combine` CLI
(validate/append/concatenate) and replayed with `readout-replay`, which steps through points,
publishes each point's parameters through the ParameterPublisher interface (EPICS transport is
external — see ParameterPublisher.md), draws `n ~ Poisson(w * counting_time)` events per stored
readout, and routes them to EFUs resolved from explicit config > file-embedded attributes >
defaults. EFU-sendability of a collector group is decided by exact datatype comparison against
the registry (`hdf_compound_type`), never by attributes, and an anti-drift test pins the
canonical descriptions to the C++ event structs.

## Building and testing

- The `build/` and `build-dev/` directories are configured with the conan **Debug** generators
  and `-DREADOUT_DEVELOPMENT_MODE=ON` (see DEVELOPMENT.md). Do not reconfigure with different
  flags; if needed: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREADOUT_DEVELOPMENT_MODE=ON`.
- C++ tests: `cmake --build build -j8 && cd build && ctest`.
- McStas toolchain tests: `python3 -m pytest tests/` — these prefer `build-dev/` over `build/`
  (tests/conftest.py), so rebuild **both** directories first or you will test stale binaries.
- After changing a `.comp` file, delete any stale `share/Readout/*.comp.json` caches so
  mccode-antlr re-parses.

## Remaining / external work

1. EPICS implementation of ParameterPublisher — requirements in ParameterPublisher.md; the
   transport lives in mccode-plumber, not here.
2. Optional: fixed-count replay mode ("exactly N events") on the retained WRSWR reservoir
   sampler (lib/IndexSampler.h, lib/ctream), and paced replay (events spread over the counting
   time) if downstream consumers need realistic wall-clock intervals.
