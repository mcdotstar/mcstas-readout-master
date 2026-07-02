# Description
This repository contains library functionality for collecting simulated readouts from a McStas neutron raytracing runtime via a custom component, CollectCAEN.comp. When the simulation runs that component holds a pointer to a Collector object (lib/CollectorClass.h) that provides a way to record any number of weighted ray readouts to a named HDF5 group, which statically defined dataset members, 'readouts', 'cue', 'weights', 'normalizations'. 
If there is more than one CollectCAEN.comp, they will record readouts to different named groups independently.
The Collector objects utilize a singleton CollectorSink to manage their common HDF5 file, which also defines static names of groups, datasets, attributes, and attribute values for a 'validated' Collector HDF5 file.
The CollectorSink can also record simulation parameters to the file.

After some number of points are simulated, they will be concatenated via `concatenate_collector_files`, 
which will produce one multi-point simulation output ready for replaying/reprocessing.
The class Reader (lib/reader.h) is intended to provide an interface to the recorded simulation file.
The functionality in lib/replay.h could then read and broadcast the collected readouts at a later time.

# Current state
The Collector/CollectorSink, Reader/ReaderSource, and their tests are all working correctly.
The Reader understands the multi-collector cue-based layout, points, and parameters, and the collector tests pass (84 of 85 tests pass; the one failing test is the integration test, which exercises the McStas component layer).

# Remaining work
1. **Replay rewrite**: `lib/replay.cpp` loads all readouts into memory, ignores points and parameters, sends every ray as an event without Poisson/WRSWR sampling (statistically incorrect), and is hard-coded to a single EFU endpoint.  It needs to be rewritten to use the Reader, step through points, draw per-ray Poisson event counts, and route through a per-(detector, readout) Sender map.
2. **EFU configuration in the file**: `CollectCAEN.comp` should optionally store EFU address/port as attributes on the collector group so that replay can discover endpoints from the file rather than requiring external configuration.
3. **File combination CLI**: expose the existing library functions (`merge_collector_files`, `concatenate_collector_files`, `append_collector_files`, `validate_collector_file`) as a `readout-combine` binary, covering both the MPI-node-merge and the multi-point-concatenate use cases.
4. **CollectorStar parity**: once the above three items are complete, extend the merge/concat/append/Reader/replay path to work with the user-defined-struct CollectorStar, and provide equivalent CLI tools and tests.
