# Implementation Plan: Decoupled Simulation ↔ DAQ Pipeline

Date: 2026-07-02, branch `exp-ref-reader`.

## Current state (verified by build + test, corrects stale docs)

The build is clean and **84 of 85 tests pass**. AGENT.md's claim that the Reader
is out of date and `collector_test.cpp` fails is no longer true — the Reader
understands the multi-collector layout, points (cues), and parameters, and its
tests pass.

What actually works today:
- **Collector / CollectorSink** write the cue-based multi-point HDF5 layout
  (`readouts`, `cues`, `weights`, `normalizations` per group, plus a
  `parameters` group and file-level provenance attributes).
- **Reader / ReaderSource** (lib/reader.h) read that layout, with point
  offsets/sizes derived from cues. Tests pass.
- **Merge/concat/append functions** exist in lib/CollectorClass.cpp
  (`merge_collector_files`, `concatenate_collector_files`,
  `append_collector_files`, `validate_collector_file`) — but only the MPI merge
  is reachable from a McStas run; none are exposed as a CLI.
- **Sampling machinery** exists and is tested: ctream WRSWR-SKIP reservoir
  sampler, `IndexSampler`, and Poisson weight→event conversion
  (test/weight_to_event_test.cpp).
- **Sender + SenderConfigs** (JSON-loadable per-(detector, readout) EFU
  endpoints) exist and are tested.

The real gaps:
1. **Integration test fails** because the test instruments in
   test/test_integration.sh and tests/test_run_components.py still pass
   ReadoutCAEN-era parameters (`event_mode`, `ip`, `port`) to `CollectCAEN.comp`,
   whose interface is now `point`/`total_points`/`dataset_name`/`filename`/….
2. **replay.cpp is architecturally wrong** — by its own comment. It loads every
   readout from every point into memory, ignores points and parameters, sends
   *all* rays as events (no Poisson/WRSWR sampling, so no statistical
   correctness — the entire motivation for the redesign), uses the legacy
   `Readout` class with a single hard-coded endpoint, and refuses mixed
   detector types.
3. **streamer.h is stale**: it has the *right* structure (per-point loop,
   per-dataset sampling, SenderConfigs-driven sender map) but targets the *old*
   file layout (`total_weight`/`scan_point` attributes the Collector no longer
   writes) and is not compiled anywhere.
4. **No EPICS/parameter publishing** during replay (only a comment).
5. **No EFU configuration stored in the file** (requirement in CLAUDE.md).
6. **CollectorStar** has no merge/concat, no Reader, no replay path.

## Definition of "usable decoupled state"

Run McStas with `CollectCAEN` and **no EFU running** → per-point/per-node HDF5
files → combine with a CLI tool → `readout-replay <file.h5>` steps through the
points, publishes each point's parameters, samples a statistically correct
number of events per collector group, and sends them to the EFU(s) named in
the file (or overridden on the command line).

Phases 0–3 below reach that state. Phase 4 (CollectorStar) is the flexibility
follow-up and deliberately comes after.

## Phase 0 — Repair the simulation-side path (small, immediate)

1. Update the instruments embedded in `test/test_integration.sh` and
   `tests/test_run_components.py` to the current `CollectCAEN.comp` parameter
   set. A green integration test verifies the McStas → HDF5 leg end-to-end.
2. Commit the currently untracked docs (CLAUDE.md, AGENT.md, architecture.md,
   DEVELOPMENT.md) and correct the stale claims in AGENT.md so the docs match
   reality.

Acceptance: `ctest` 85/85.

## Phase 1 — Statistically correct, point-aware replay (the core deliverable)

Rewrite lib/replay.cpp to the design already written in its comment block,
building on **Reader** (not by resurrecting Streamer):

1. `ReplayConfig`: counting time (or weight fraction), RNG seed, ordering
   (sequential/random), and EFU endpoints via `SenderConfigs`.
2. Per point, per collector group: read the point's slice of `readouts` in
   chunks and, for each ray, draw `nᵢ ~ Poisson(wᵢ × counting_time)` and emit
   the readout `nᵢ` times. By the Poisson-multinomial decomposition this is
   *exactly* equivalent to drawing `N ~ Poisson(point_weight × time)` and
   selecting rays with probability `wᵢ/W` — so no reservoir sampler, no
   up-front total weight, no whole-file vectors in memory (the stored
   `weights` value remains a sanity check / count pre-estimate). Send through
   a per-(detector, readout) `Sender` map — this removes the single-detector
   restriction. The WRSWR/`IndexSampler` machinery stays in the tree as the
   right tool for a future fixed-count mode ("exactly N events"), but is off
   the critical path.
3. Parameter publishing hook: a small `ParameterPublisher` interface with
   no-op and stdout implementations now; an EPICS implementation stays out of
   this repo (mccode-plumber territory) and plugs in later. Replay calls it
   with each point's parameter values before sending that point's events.
4. Harvest what is useful from streamer.h (sender-map structure), then delete
   it — it targets a file layout that no longer exists.
5. Update the `readout-replay` CLI (src/replay/main.cpp): add
   `--time`/`--fraction`, `--seed`, `--config <senders.json>`; keep
   `--sequential`/`--random`.
6. Tests: generate a multi-point file, replay against a mock UDP receiver (or
   a network-disabled Sender), assert per-point event counts are consistent
   with the Poisson mean and that parameters are published once per point.
   The open TODO from architecture.md (whether WRSWR samples need
   normalization) is resolved by the per-ray Poisson approach: the question
   no longer arises on the replay path.

Acceptance: replay of a 2-point, 2-group file produces per-point, per-group
event streams with counts ≈ `weight × time`, no EFU required to *create* the
data, mixed readout types allowed.

## Phase 2 — EFU configuration in the file

1. Add optional EFU parameters to `CollectCAEN.comp` (address/port or a
   logical EFU name) which the Collector writes as attributes on the
   collector group, next to the existing `detector`/`readout` attributes.
2. Reader exposes them; replay uses them to build its default `SenderConfigs`,
   with CLI/JSON overrides taking precedence.
3. Test: one file with two groups aimed at two different (mock) EFUs; replay
   routes each group correctly.

## Phase 3 — File combination CLI

1. New binary `readout-combine` (src/combine/, args.hxx like the other tools)
   exposing the existing library functions: `validate`, `merge` (MPI-node
   files of one point), `concatenate` (multiple points → cue-based layout),
   `append`. Cover both cases from CLAUDE.md: repeats of one simulation point
   and sequences of different points.
2. Round-trip tests: N single-point files → concatenate → Reader/replay see N
   points with correct per-point weights and parameters; also merge-then-
   concatenate (MPI × scan) ordering.

## Phase 4 — CollectorStar parity (the "switch")

Do this only once phases 0–3 are green, so there is always one working path.

1. Generalize the merge/concat/append functions to be datatype-agnostic
   (HighFive can copy compound data by the dataset's own datatype — most of
   the Collector implementation should generalize rather than be duplicated).
2. Reader support for user-defined compound types (a `ReaderStar` or a
   generalized Reader that surfaces raw structs + the parsed
   TypeDescription).
3. Design decision to resolve here: how replay maps an arbitrary user struct
   onto an EFU readout payload. Simplest viable rule: the group's
   `readout` attribute names a known readout type and the stored struct must
   be layout-compatible (verified by size/field check at replay time).
4. Port the CLI and the phase 1–3 tests to CollectorStar; only then deprecate
   the fixed-type Collector per CLAUDE.md's "Switch to CollectorStar".

## Known risks / open questions

- **Duplicate events**: a heavy ray can emit several identical events (same
  timestamp/ADC) under any statistically correct sampling scheme; the
  EFU-facing side must tolerate duplicates. Optional timestamp jitter is a
  possible later refinement.
- **`cues` are uint32**: overflows at ~4.3e9 readouts per group; acceptable
  for now, worth a guard in the Collector destructor.
- **EPICS**: kept behind an interface; actual EPICS transport lives outside
  this repo, so replay here stays dependency-light.
- **readout-replay-arrow / writer.h / Array**: apparently an experimental
  Arrow-based I/O path; not on the critical path, ignore until phases 0–3 are
  done.
