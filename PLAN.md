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

## Phase 4 — CollectorStar as the storage engine, typed components on top

Do this only once phases 0–3 are green, so there is always one working path.

Design (decided 2026-07-02): CollectorStar becomes the *single* storage
engine, and type-fixing moves up into the McStas component layer. A family of
`Collector{ReadoutType}.comp` components (CollectorCAEN, CollectorTTLMonitor,
…) carry fixed struct-definition strings that correspond exactly to the
EFU-known readout types. The generic CollectorStar interface remains available
for users to write their own `Collector*.comp` components, but those are not
EFU-sendable — as is true of the EFU itself, which only accepts well-defined
data.

"Sendable" is a checkable property, not a convention: replay compares the
dataset's stored HDF5 compound datatype (exact on field names, offsets, and
types) against a registry of known readout types — the existing
`hdf_compound_type(ReadoutType)` definitions are that registry. Datasets that
match are sampled and sent; datasets that don't are skipped for EFU replay but
remain fully readable for offline analysis.

Work items:
1. Bring CollectorStar to Collector parity — it is further behind than
   architecture.md claims: its actual interface is `add(const void*)` with
   **no weight parameter**, it buffers everything in RAM and writes once, and
   it has no CollectorSink integration (no points/cues, no parameters, no
   per-point weights/normalizations, no MPI merge). Give it the sink-backed,
   cue-based incremental layout of Collector.
2. Per-ray weight is required for replay sampling (the per-ray Poisson draw
   needs each stored readout's weight). Registry rule: every sendable type
   includes `weight` and time-of-flight fields, as today's `*_event` structs
   already do. The `Collector{ReadoutType}.comp` struct strings mirror those
   event structs.
3. Anti-drift safeguard: a unit test that parses each component's embedded
   struct string through `TypeDescriptionParser` and asserts it produces the
   same compound type as `hdf_compound_type(ReadoutType)`, so the
   component-embedded strings cannot silently diverge from the C++ structs.
4. Generalize the merge/concat/append functions to be datatype-agnostic
   (HighFive can copy compound data by the dataset's own datatype — most of
   the Collector implementation should generalize rather than be duplicated).
5. Reader support for user-defined compound types (a generalized Reader that
   surfaces raw structs plus the parsed TypeDescription; typed access when
   the registry check passes).
6. Port the CLI and the phase 1–3 tests to the new components; only then
   deprecate the fixed-type Collector per CLAUDE.md's "Switch to
   CollectorStar".

### Status (2026-07-02)

Items 1–5 are implemented, with one design refinement: instead of bringing the
old buffering CollectorStar class to parity, the existing Collector/CollectorSink
became the single engine — Collector gained a description-based constructor
(parse → HDF5 compound type) and `addRecord(weight, data)`, so user-described
records get the identical cue-based layout, parameters, EFU attributes, and
combination support. Reader gained `sendable_readout_type()` (exact datatype
comparison against the registry — attributes are informative only and now
optional), `get_raw`, `record_size`, `point_weight`, and `type_description`;
replay dispatches on the datatype-verified type and skips non-sendable groups.
The canonical description strings live in lib/readout_type_descriptions.h with
the anti-drift test in test/star_collector_test.cpp.

Item 6 is done: the full Collector{ReadoutType}.comp family exists
(CAEN, TTLMonitor, CDT, VMM3, BM0, BM2, BMI), each storing whole records
through the star engine with the canonical layout from
`readout_description_for(ess_type)` and a record-size guard, with a
mccode-antlr run test per component. Remaining decisions, not code: when to
deprecate the typed CollectCAEN.comp and the old in-RAM CollectorStar class,
now that the description-based path covers their use cases.

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
