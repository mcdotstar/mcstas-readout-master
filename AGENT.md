# Description
This repository contains library functionality for collecting simulated readouts from a McStas
neutron raytracing runtime and replaying them, statistically correctly, to ESS Event Formation
Units (EFUs) after the fact — decoupling the simulation runtime from the data-acquisition
pipeline.

Collection happens through the `Collector{ReadoutType}.comp` component family (CAEN, TTLMonitor,
CDT, VMM3, BM0, BM2, BMI in share/Readout/). Each component stores whole records through the
description-based collector engine: the C-struct layout of one record is described by a canonical
string (lib/readout_type_descriptions.h) parsed at runtime into an HDF5 compound datatype.
Multiple components write independent named groups into one file, each with the cue-based layout
`readouts`, `cues`, `weights`, `normalizations`, managed by the singleton CollectorSink
(lib/CollectorClass.h), which also records instrument parameters and optional per-group EFU
address/port attributes.

Users can collect arbitrary additional data by passing their own struct-description string to the
same engine (`Collector(filename, name, description)` in C++, `collector_star_new` from C); such
groups are readable and combinable but not EFU-sendable. EFU-sendability is decided by exact
datatype comparison against the registry (`hdf_compound_type`), never by attributes — an
attribute cannot lie about the record layout, and an anti-drift unit test pins the canonical
description strings to the C++ event structs.

Files from repeated or scanned simulation points are combined with the `readout-combine` CLI
(validate / append / concatenate) into one multi-point file. The Reader/ReaderSource classes
(lib/reader.h) provide access to points, records, weights, and parameters. `readout-replay`
(lib/replay.h) steps through the points in a file, publishes each point's parameters through the
ParameterPublisher interface (EPICS transport plugs in from outside the library), draws
`n ~ Poisson(w * counting_time)` events per stored readout, and routes them to per-(detector,
readout) EFU endpoints resolved from explicit configuration, file-embedded attributes, or
defaults — in that precedence order.

# Current state
All of the above is implemented and tested (ctest plus mccode-antlr run tests per component).
The legacy per-ray Readout broadcasting components (ReadoutCAEN, ReadoutTTLMonitor,
ReadoutDiscreteCAEN) remain for in-simulation streaming use cases.

# Remaining work
1. EPICS implementation of ParameterPublisher (lives outside this repository; mccode-plumber).
2. Consider a fixed-count replay mode ("exactly N events") using the retained WRSWR reservoir
   sampler (lib/IndexSampler.h, lib/ctream).
3. architecture.md still describes the pre-redesign state and could be refreshed or retired.
