# McStas Readout Master for ESS detectors

Components and tools linking [McStas 3.3+](https://mcstas.org) ray-tracing neutron
simulations to the ESS data pipeline via the
[Event Formation Unit](https://github.com/ess-dmsc/event-formation-unit) (EFU).

Simulated readout records are **collected** to HDF5 files while the simulation runs,
**combined** across repeated runs or scan points, and **replayed** to EFUs afterwards,
statistically correctly — the data-acquisition stack does not need to be running (or
reachable) during the simulation.

## Documentation

Guides, per-component parameter references, CLI documentation, and the C/C++ API
reference are published at

**https://g5t.github.io/mcstas-readout-master/**

To build the site locally (requires Doxygen):

```bash
cmake -S . -B build-docs -DREADOUT_DOCS_ONLY=ON
cmake --build build-docs --target docs
# open build-docs/docs/html/index.html
```

## Quickstart

### 1. Install

```bash
git clone https://github.com/g5t/mcstas-readout-master.git
cmake -S mcstas-readout-master -B mcstas-readout-master-build -DCMAKE_INSTALL_PREFIX=~/.local
cmake --build mcstas-readout-master-build --target install
```

This installs `libreadout`, the `readout-config`, `readout-combine`, and
`readout-replay` tools, and the McStas component files.

### 2. Collect

Add a `Collector*` component matching your detector electronics
(CAEN, TTLMonitor, CDT, VMM3, BM0, BM2, BMI) to the end of your instrument's
`TRACE` section. Identifying values are read *by name* from particle variables
(typically `USER_VARS` set in an `EXTEND` block):

```instr
TRACE
  ...
  SEARCH SHELL "readout-config --show compdir"
  COMPONENT collect = CollectorCAEN(
    ring="ring_id", tube="tube_id", a_name="left", b_name="right",
    filename="run", dataset_name="caen_bank0")
  AT (0, 0, 0) ABSOLUTE
```

Each run produces a cue-based HDF5 file; several collectors can share one file,
each writing its own named group.

### 3. Combine

```bash
readout-combine validate run_*.h5
readout-combine append      --output merged.h5 run_a.h5 run_b.h5   # identical parameters
readout-combine concatenate --output scan.h5   point_*.h5          # scan points
```

### 4. Replay

```bash
readout-replay --time 2.0 --seed 42 --addr my-efu.example.org --port 9000 scan.h5
```

Replay steps through the stored points, publishes each point's parameters, draws
`n ~ Poisson(w * counting_time)` events per stored readout, and sends them to EFUs
resolved from explicit configuration, file-embedded attributes, or the command-line
defaults — in that order. See the
[CLI reference](https://g5t.github.io/mcstas-readout-master/) for all options.

## Legacy runtime streaming

The original per-ray streaming components remain available for use cases where the
EFU runs alongside the simulation: `ReadoutCAEN` and `ReadoutTTLMonitor` make a
Poisson-distributed Monte Carlo choice per traced neutron and send packets
immediately, and `ReadoutDiscreteCAEN` sends an exact number of weighted-sampled
events at the end of the run. Their parameters are documented on the site and in
the component headers (`mcdoc`-compatible).

## MPI support

The `Collector*` components gather records to the master node, which writes one
HDF5 file. For the streaming `Readout*` components, all nodes need network access
to the EFU host.

## Development

See [DEVELOPMENT.md](DEVELOPMENT.md) for the development-mode build
(`-DREADOUT_DEVELOPMENT_MODE=ON`), test suites, and the reasoning behind the
self-locating `readout-config`.

## Use with McStas 3.2

The `SEARCH SHELL` automatic path modification used above requires McStas 3.3+.
With McStas 3.2, copy the component files into one of the McStas component search
directories and omit the `SEARCH SHELL` line.
