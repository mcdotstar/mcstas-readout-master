# Collector files: layout and lifecycle

Collector output is cue-based and point-aware. It is not the legacy flat
`events` dataset layout.

## File layout

At HDF5 root:

- one group per Collector component (for example `caen_bank0`, `monitor_a`)
- optional root `parameters` group (scan/instrument metadata)

Each collector group contains:

- `readouts` (compound records)
- `cues` (point end offsets into `readouts`)
- `weights` (sum of rate-weights per point)
- `normalizations` (simulated-particle count per point)

Record weights are stored with the source-term scaling removed (`p * ncount`),
so the physical rate of a record — and of a point's summed `weights` entry — is
its stored weight divided by the point's `normalizations` entry. Appending
files sums both records and normalizations, which keeps that ratio (and hence
any replayed intensity) the combined ray-count-weighted estimate rather than
the sum of the runs.

Group attributes may include:

- detector identity
- optional EFU routing (`efu_address`, `efu_port`)

EFU-sendability is decided by exact datatype match against the readout registry,
not by attributes.

Collector files are ordinary HDF5: for analysis or ad-hoc inspection, read
them directly with h5py (`h5py.File(...)["caen_bank0/readouts"][...]` yields a
structured NumPy array of the compound records). The `mcstas_readout` Python
package drives replay and combine but deliberately does not wrap record
access — the file format needs no wrapper.

## Validate files

```bash
readout-combine validate run_001.h5 run_002.h5
```

## Combine files

Append same-point files (identical parameters):

```bash
readout-combine append --output combined.h5 pointA_1.h5 pointA_2.h5
```

Concatenate different-point files (consistent parameter schema):

```bash
readout-combine concatenate --output scan.h5 point1.h5 point2.h5 point3.h5
```

## Replay files

Replay all stored readouts exactly once:

```bash
readout-replay --addr 127.0.0.1 --port 9000 scan.h5
```

Replay with counting-time sampling (`n ~ Poisson(w / normalization * counting_time)`):

```bash
readout-replay --time 1.5 --seed 1234 scan.h5
```

Replay endpoint resolution precedence:

1. explicit config (`--config`)
2. file-embedded group attributes
3. default `--addr`/`--port` (or built-in defaults)
