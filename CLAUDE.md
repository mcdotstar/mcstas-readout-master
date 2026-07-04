# 2026-07-04 Status

This project is the interface between the simulation world (McStas based) and the ESS data
acquisition world (EPICS, Kafka and the EFU). The redevelopment from runtime event streaming
to decoupled collect → combine → replay is **complete**; see AGENT.md for the architecture
and PLAN.md for how it got here.

In one paragraph: `Collector{ReadoutType}.comp` components (readout_core/components/) store
weighted-ray records into cue-based multi-point HDF5 files through a description-based engine
(a C-struct description string parsed to an HDF5 compound datatype at runtime; canonical
strings in readout_core/include/readout_type_descriptions.h). Files are combined with the
`readout-combine` CLI (validate/append/concatenate) and replayed with `readout-replay`, which
steps through points, publishes each point's parameters through the ParameterPublisher
interface (EPICS transport is external — see ParameterPublisher.md), draws
`n ~ Poisson(w * counting_time)` events per stored readout, and routes them to EFUs resolved
from explicit config > file-embedded attributes > defaults. EFU-sendability of a collector
group is decided by exact datatype comparison against the registry (`hdf_compound_type`),
never by attributes, and an anti-drift test pins the canonical descriptions to the C++ event
structs.

## Layout

- `readout_core/` — the installed package: `include/` (public headers), `src/`, `components/`
  (McStas `.comp` files plus lib-readout.c/h), `app_config/`, `app_combine/`, `app_replay/`.
- `test/` — C++ unit tests (repository root, mcpl-style, not installed).
- `tests/` — pytest McStas-toolchain tests (mccode-antlr compile/run).
- `docs/` — Doxygen-based site: guide pages, `Doxyfile.in`, `extract_comp_docs.py` (generates
  per-component markdown from the McDoc headers in the `.comp` files), `theme/`
  (vendored doxygen-awesome-css). Published via `.github/workflows/docs.yml` → GitHub Pages.
- `examples/` — runnable collect → combine → replay walkthrough.

## Building and testing

- The `build/` and `build-dev/` directories are configured with the conan **Debug** generators
  and `-DREADOUT_DEVELOPMENT_MODE=ON` (see DEVELOPMENT.md). Do not reconfigure with different
  flags; if needed: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DREADOUT_DEVELOPMENT_MODE=ON`.
- C++ tests: `cmake --build build -j8 && cd build && ctest`.
- McStas toolchain tests: `python3 -m pytest tests/` — these prefer `build-dev/` over `build/`
  (tests/conftest.py), so rebuild **both** directories first or you will test stale binaries.
- After changing a `.comp` file, delete any stale `readout_core/components/*.comp.json` caches
  so mccode-antlr re-parses.
- Documentation: `cmake -S . -B <scratch> -DREADOUT_DOCS_ONLY=ON && cmake --build <scratch>
  --target docs` (needs doxygen; no conan, no compiled targets).

## Remaining / external work

1. EPICS implementation of ParameterPublisher — requirements in ParameterPublisher.md; the
   transport lives in mccode-plumber, not here.
2. Optional: fixed-count replay mode ("exactly N events") on the retained WRSWR reservoir
   sampler (readout_core/src/IndexSampler.h, readout_core/src/ctream), and paced replay
   (events spread over the counting time) if downstream consumers need realistic wall-clock
   intervals.
