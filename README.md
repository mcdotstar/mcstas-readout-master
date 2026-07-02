# McStas Readout Master for ESS detectors
The Readout [McStas 3.3+](https://mcstas.org) component is the link between ray-tracing neutron simulations
and the ESS data pipeline via the [Event Formation Unit](https://github.com/ess-dmsc/event-formation-unit) (EFU).

## Overview
Different McStas components are provided to support different types of detectors used with the EFU;
since each detector type produces different information per neutron event.
At present only detectors using CAEN digitizers and simple TTL-based signals are supported, but extending
the system to support other detector types is straightforward.

Each component makes a Poisson distributed Monte Carlo choice for every neutron
that it is passed based on the weight, `_particle->p`, or the weight squared (user configurable).
If the random choice is finite event-identifying parameters are read from the `_particle` struct's `USER_VARS`
(detailed below) which must have been set earlier in the simulation.
If the random choice is zero the component can, optionally, produce random noise values for all require parameters
or alternatively discard the neutron.
If not discarded, the information is added to a network packet through the C interface `Readout`
and ultimately sent to the EFU by the C++ object `ReadoutClass`.

The exact details of information required by the Event Formation Unit depend on the type of detector and
are explained in a number of 'Detector Interface Control Documents', 
produced by ECDC at the ESS.

## Common McStas component parameters
| Parameter      | Type   | Description                                                                              |
|----------------|--------|------------------------------------------------------------------------------------------|
| `ring`         | named  | identifies the Readout Ring physical fibre                                               |
| `fen`          | named  | identifies the Front End Node                                                            |
| `tof`          | named  | time-of-flight of the neutron, default: `_particle->t`, any `USER_VARS` value is valid   |
| `ip`           | string | the resolvable domain name or IP address of the EFU which will receive the packets       |
| `port`         | int    | the EFU event-packet UDP port, 9000 by default to match the EFU default                  |
| `command_port` | int    | the EFU command TCP port, 10800 by default to match the EFU default                      |
| `broadcast`    | int    | flag to control if event packets are sent, on by default                                 |
| `pulse_rate`   | double | the reference time pulse rate, used in calculating EFU-required time stamps              |
| `noisy`        | int    | flag to control if otherwise-dropped events should be replaced by noise, off by default  |
| `noise_level`  | double | if `noisy`, *how* noisy &mdash; fractional probability of noise: 0.1 by default          |
| `event_mode`   | string | `"p"` for `rand_poisson(_particle->p)`, `"pp"` for p<sup>2</sup>                         |
| `verbose`      | int    | controls `STDOUT` printing: -1=silent, 0=errors, 1=warnings, 2=info, 3=details           |
| `ess_type`     | int    | identifies simulated ESS readout &mdash; BIFROST: 0x34 (dec 52), CSPEC: 0x40 (dec 64)    |
| `filename`     | string | if present, neutron ray data provided to the broadcaster will be stored to HDF5 filename | 


## Common Event Formation Unit parameters
The event formation unit requires a number of parameters to identify the detector element and time of flight which
comprise the events it forwards to Kafka.
These parameters must be stored in the `USER_VARS` contained in the `_particle` `struct` which *should* be set
appropriately earlier in the McStas instrument.
The component identifies the `struct` parameters *by name* and requires the names as input component parameters.
The component parameter names common to all readout components are the Ring identifier, Front End Node identifier,
and time of flight.
Finally, the EFU requires the event time, the most-recent reference time, and the previous reference time to determine
the *relative* event time which is ultimately stored in a NeXus file.
These times must be encoded as two integers: the `high` time, which is the number of whole seconds since the UNIX epoch,
`1970-01-01T00:00Z`, and the `low` time, which is the number of `88052499 Hz` clock ticks since the last whole second.

The reference times are determined automatically based on the component parameter `pulse_rate` and the running time of
the McStas simulation.
The event time is set to the most-recent reference time plus neutron time-of-flight, which is read from `_particle->t`
by default but can be overridden by setting the component parameter `tof`.

| Named parameter | EFU parameter         |
|-----------------|-----------------------|
| `ring`          | `FibreID`             |
| `fen`           | `FENId`               |
| `tof`           | `HighTime`, `LowTime` |


# `ReadoutCAEN.comp`: instruments with CAEN electronics

At least four ESS instruments will use CAEN digitizers to read out the detector signals.
Of these, most will use position-sensitive <sup>3</sup>He tubes, which use two digitizers per tube; 
and at least one will use bundles of position-sensitive <sup>10</sup>B straws, which use four digitizers per tube.
To support these instruments, and the common data format they will produce, the `ReadoutCAEN.comp` component is provided.

This component is intended to be used with the full ECDC readout stack,
which makes use of an EFU, a Kafka broker and independent file writers; all managed externally. 

## Component specific parameters
| Parameter   | Type  | Description                                                 |
|-------------|-------|-------------------------------------------------------------|
| `tube`      | named | identifies the group of digitizers connected to the channel |
| `a_name`    | named | is the integrated voltage output of the first digitizer     |
| `b_name`    | named | is the integrated voltage output of the second digitizer    |
| `c_name`    | named | is the integrated voltage output of the third digitizer     |
| `d_name`    | named | is the integrated voltage output of the fourth digitizer    |
| `fen_value` | int   | used for `FENId` if no named `fen` parameter                |
| `a_value`   | int   | used when `a` is not named; default 0                       |
| `b_value`   | int   | used when `b` is not named; default 0                       |
| `c_value`   | int   | used when `c` is not named; default 0                       |
| `d_value`   | int   | used when `d` is not named; default 0                       |


To identify in which channel an event occurred, the CAEN variant of the EFU requires an additional integer `GreoupId`.
This identifies the group of physical channels in the CAEN digitizer which produced an event.
For <sup>3</sup>He tubes, this is the tube number, and the position between the two end of the detecting tube is 
reconstructed using the integrated amplitudes output as part of the event message by the CAEN digitizer, 
which are therefore required as input to the CAEN EFU  as `AmpA` and `AmpB`.

| Named parameter | EFU parameter |
|-----------------|---------------|
| `tube`          | `GroupId`     |
| `a_name`        | `AmpA`        |
| `b_name`        | `AmpB`        |
| `c_name`        | `AmpC`        |
| `d_name`        | `AmpD`        |


# `ReadoutTTLMonitor.comp`: simple TTL based beam monitors

Like `ReadoutCAEN.comp` this component can not control the EFU it will communicate with.
It is intended to work with _one_ simulated TTL beam monitor, since it can only handle a single time of flight.

An instrument with multiple beam monitors would require multiple `ReadoutTTLMonitor.comp` instances
in order to output all of their events.
To support this operation for 0-D monitors, it is possible to define a static `Ring`, `FEN`, `Pos` and `Channel`,
and then place this component *directly* after each `EXTEND`ed monitor which defines an appropriate `ADC` value.

```instr
...
USER_VARS %{
  ...
  int monitor_signal;
%}
TRACE
...
monitor0 = Monitor(xwidth=x, yheight=y, restore_neutron=1) AT (...) EXTEND %{
  monitor_signal = (SCATTER) ? 500 + (int)(rand01() * 1024) : 0;
%}
output0 = ReadoutTTLMonitor(channel_value=0, fen_value=100, adc="monitor_signal", ip="ttl_monitor_efu", port=9001);
...
monitor1 = Monitor(xwidth=x, yheight=y, restore_neutron=1) AT (...) EXTEND %{
  monitor_signal = (SCATTER) ? 500 + (int)(rand01() * 1024) : 0;
%}
output1 = ReadoutTTLMonitor(channel_value=1, fen_value=100, adc="monitor_signal", ip="ttl_monitor_efu", port=9001);
...
monitor2 = Monitor(xwidth=x, yheight=y, restore_neutron=1) AT (...) EXTEND %{
  monitor_signal = (SCATTER) ? 500 + (int)(rand01() * 1024) : 0;
%}
output2 = ReadoutTTLMonitor(channel_value=2, fen_value=100, adc="monitor_signal", ip="ttl_monitor_efu", port=9001);
...
```

## Additional component parameters
| Parameter        | Type  | Description                                         |
|------------------|-------|-----------------------------------------------------|
| `position`       | named | detection position in monitor                       |
| `identity`       | named | identifies the monitor                              |
| `output`         | named | the integrated voltage output of the digitizer      |
| `ring_value`     | int   | used for `FibreId` if no named `ring` parameter     |
| `fen_value`      | int   | used for `FENId` if no named `fen` parameter        |
| `pos_value`      | int   | used for `Pos` if no named `position` parameter     |
| `identity_value` | int   | used for `Channel` if no named `identity` parameter |


# Installation

After cloning this repository, use CMake to configure, build, and install the shared C library.

To install the library and configuration-query tool within the current user's home directory, e.g., execute 
```bash
git clone https://github.com/g5t/mcstas-readout-master.git
cmake -S mcstas-readout-master -B mcstas-readout-master-build -DCMAKE_INSTALL_PREFIX=~/.local
cmake --build mcstas-readout-master-build --target install
```

## MPI Support
McStas can be run on any number of MPI workers. If any of the `Readout` components are run in MPI all nodes should have network
access to the host running the EFU(s).
Saving weighted ray data to HDF5 files is currently not supported in MPI mode.

# Development

## Local Development Workflow

For development and testing without system-wide installation, use the development mode build:

```bash
cmake -S . -B build-dev -DREADOUT_DEVELOPMENT_MODE=ON
cmake --build build-dev
```

This creates a self-contained build directory with:
- `build-dev/readout-config` - Configuration query tool
- `build-dev/lib/libreadout.so` - Shared library
- `build-dev/include/` - Header files
- `build-dev/share/Readout/` - McStas component files

### Testing

Use the provided test wrapper script to run tests with the correct environment:

```bash
./run_tests.sh build-dev
```

The wrapper script:
- Sets up PATH so `readout-config` is found first
- Configures LD_LIBRARY_PATH for the local library
- Creates symlinks to resolve component paths
- Runs tests in isolation from any system installations

### Why Development Mode?

Development mode solves the PATH variable inheritance problem in McStas tools:
- When `mcstas-antlr` spawns subprocesses, they may not inherit the modified PATH
- System-installed versions of `readout-config` can interfere with local development
- Development mode embeds relative paths at build time, making it independent of PATH

# Use
Once installed as above, you can include the readout component in an exising McStas instrument by placing something
similar to the following in the TRACE section of an instrument file (likely at the end)
```instr
TRACE
  ...
  SEARCH SHELL "readout-config --show compdir"
  COMPONENT readout = Readout(ring="RING", fen="FEN", channel="TUBE", a="left", b="right", ...)
  AT (0, 0, 0) ABSOLUTE
  ...
```

## Collector file layout and replay

Collector output is organized as **groups per collector component** at the HDF5 root (for example `collector`), not as a single flat `events` dataset.

Each collector group contains:

- `readouts` (compound event records)
- `cues` (point end offsets into `readouts`)
- `weights`
- `normalizations`

Optional scan/instrument metadata is stored in a root `parameters` group as 1-D datasets matching the point dimension.

Replay now reads this collector-group layout directly. Legacy flat files are not supported by the collector reader/replay path.

If multiple Readout components are included in an instrument, the `SEARCH SHELL` command is only needed once.
Consult the McStas 3.3+ documentation for details of its use.

## Use with McStas 3.2
The `SEARCH SHELL` automatic path modifications used above to find the included components do not work prior to the
version 3.3 release of McStas.
It is possible to use the components with McStas version 3.2 if you copy the component files to one of the McStas
component search directories and then omit the `SEARCH SHELL` line(s) in the instrument file.