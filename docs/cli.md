# Command-line tools

Three executables are installed alongside the library. All of them print full
usage with `--help`.

## readout-config

Self-locating configuration query tool, used by the McStas components'
`DEPENDENCY` and `SEARCH SHELL` lines to find the installed library, headers,
and component files:

```bash
readout-config --show compdir    # McStas component directory
readout-config --show libdir     # shared library directory
readout-config --show includedir # header directory
```

## readout-combine

Combine or validate collector HDF5 files.

```bash
readout-combine validate FILE [FILE ...]
readout-combine append --output OUTPUT FILE FILE [FILE ...]
readout-combine concatenate --output OUTPUT FILE FILE [FILE ...]
```

- **validate** — check each file and print a summary: point count, collector
  group names, and parameter names. Exits non-zero if any file is invalid.
- **append** — merge *same-point* files (identical parameter values, e.g.
  repeated runs of one configuration) by appending their readouts. Requires
  `--output`; at least two inputs; the output must not already exist.
- **concatenate** — build one *multi-point* cue-based file from
  *different-point* files (consistent but not identical parameters, e.g. a
  scan). Same requirements as append.

## readout-replay

Replay stored records from a (possibly multi-point) collector file to EFUs.

```bash
readout-replay [options] FILE
```

| Flag | Description |
|------|-------------|
| `-t`, `--time TIME` | Counting time in seconds: each stored readout with rate-weight w is sent n ~ Poisson(w × TIME) times; without TIME every stored readout is sent exactly once |
| `--seed SEED` | Random seed for sampling and shuffling (0 or absent: non-deterministic) |
| `-s`, `--sequential` / `-r`, `--random` | Replay events in stored order, or in random order within each point (mutually exclusive) |
| `-n`, `--count COUNT` | Replay only COUNT stored readouts |
| `-f`, `--first FIRST` | First stored readout of the subset (with `--count`) |
| `-e`, `--every EVERY` | Take every EVERYth stored readout (with `--count`) |
| `--pulse-rate RATE` | Pulse (reference time) repetition rate in Hz; packet pulse times march forward on this grid (default 14, the ESS source frequency) |
| `--fold-tof` | Stamp each event at pulse + (tof mod pulse period) instead of pulse + tof, wrapping long-time-of-flight events into the frame they would be detected in |
| `-a`, `--addr ADDR` | Default EFU IP address |
| `-p`, `--port PORT` | Default EFU UDP port |
| `-c`, `--config CONFIG` | JSON file with per-(detector, readout) EFU endpoints |
| `-v`, `--verbose` | Print additional information, including per-point parameters |

EFU endpoints are resolved per collector group in precedence order: the
`--config` JSON, attributes embedded in the file by the Collector component
(`efu_address`/`efu_port`), then the `--addr`/`--port` defaults.

Reference times mimic a continuously pulsed source: the pulse time in each
packet header is the latest 1/RATE grid tick the wall clock has passed, so
consecutive packets share a pulse time until the reference clock ticks, and the
previous pulse time is always exactly one period earlier. At each point
boundary the replay first publishes the point's parameters, then waits for the
next grid tick — every reference time of a point's events is therefore a
wall-clock instant *after* its parameters were published, preserving the causal
order the downstream file writer relies on.

### Sender configuration JSON

```json
{
  "senders": [
    {
      "detector_type": "BIFROST",
      "readout_type": "CAEN",
      "ip_address": "bifrost-efu.example.org",
      "udp_port": 9000,
      "tcp_port": 10800
    }
  ]
}
```

`detector_type` and `readout_type` accept bare (`BIFROST`) or qualified
(`DetectorType::BIFROST`) enum names; the pair must belong together, and
duplicate (detector, readout) pairs are rejected.
