# Architecture

This library decouples McStas ray-tracing simulations from the ESS data-acquisition
pipeline: readout records are **collected** to HDF5 while the simulation runs,
**combined** across runs or scan points, and **replayed** to Event Formation Units
(EFUs) afterwards, statistically correctly, without the EFUs having to be reachable
during the simulation.

## Collection

Collection happens through the `Collector{ReadoutType}` component family
(CAEN, TTLMonitor, CDT, VMM3, BM0, BM2, BMI — see the [component pages](@ref components)).
Each component stores whole records through the description-based collector engine:
the C-struct layout of one record is described by a canonical string
(`readout_core/include/readout_type_descriptions.h`) parsed at runtime into an HDF5
compound datatype.

Multiple components write independent named groups into one file, each with the
cue-based layout `readouts`, `cues`, `weights`, `normalizations`, managed by the
singleton CollectorSink (`readout_core/include/CollectorClass.h`), which also records
instrument parameters. Routing information lives as group attributes: the detector
identity (from the component's `ess_type` — it becomes the ESS packet-type byte that
EFUs filter on at replay) and optional EFU address and port. The record layout itself
carries no attributes: it is the dataset's own compound datatype.

Users can collect arbitrary additional data by passing their own struct-description
string to the same engine (`Collector(filename, name, description)` in C++,
`collector_star_new` from C); such groups are readable and combinable but not
EFU-sendable.

### EFU-sendability

Whether a collector group can be sent to an EFU is decided by **exact datatype
comparison** against the readout registry (`hdf_compound_type`), never by attributes —
an attribute cannot lie about the record layout, and a unit test pins the canonical
description strings to the C++ event structs so they cannot drift apart.

## Combination

Files from repeated or scanned simulation points are combined with the
`readout-combine` CLI (see the [CLI reference](cli.md)):

- `validate` checks files and summarizes their collector groups and parameters,
- `append` merges same-point files (identical parameters) by appending readouts,
- `concatenate` builds one multi-point file from different-point files.

The Reader/ReaderSource classes (`readout_core/include/reader.h`) provide point-aware
access to records, weights, and parameters.

## Replay

`readout-replay` (`readout_core/include/replay.h`) steps through the points in a file
and, for each point:

1. publishes the point's parameters through the ParameterPublisher interface
   (the EPICS transport plugs in from outside this library — see
   [ParameterPublisher](../ParameterPublisher.md)),
2. draws `n ~ Poisson(w * counting_time)` events per stored readout (or exactly one
   per readout when no counting time is given),
3. routes the events to per-(detector, readout) EFU endpoints resolved from, in
   precedence order: explicit JSON configuration, file-embedded attributes,
   command-line defaults.

## Legacy runtime streaming

The per-ray broadcasting components (`ReadoutCAEN`, `ReadoutTTLMonitor`,
`ReadoutDiscreteCAEN`) remain for in-simulation streaming use cases where a running
EFU is reachable during the simulation. They draw the Poisson sample per traced
neutron at runtime instead of storing records for later replay.
