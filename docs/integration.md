# Integrating readout components in McStas instruments

## 1. Make components discoverable

Add this once in `TRACE`:

```instr
SEARCH SHELL "readout-config --show compdir"
```

## 2. Insert collector components

Collectors store weighted-ray records to HDF5 for post-run combine/replay.

```instr
COMPONENT collect = CollectorCAEN(
  filename="run_%03d",
  dataset_name="caen_bank0",
  ring="ring_id",
  fen="fen_id",
  tube="tube_id",
  a_name="amp_a",
  b_name="amp_b",
  tof="t",
  ess_type=52,
  efu_address="efu-caen.example.org",
  efu_port=9000
)
AT (0,0,0) RELATIVE PREVIOUS
```

## 3. Ensure required variables exist

Collector components read named values from `_particle` (typically `USER_VARS`).
Define and fill variables before the Collector component executes.

## 4. Choose collector groups intentionally

- `dataset_name` controls the collector group name in output HDF5.
- Multiple Collector instances can target one file; each becomes its own group.
- Replay routing can be resolved from explicit CLI config, then file attributes,
  then defaults.

## 5. Legacy runtime streaming components

`ReadoutCAEN.comp` and `ReadoutDiscreteCAEN.comp`
remain available for in-simulation runtime event streaming use-cases.

## 6. Running under MPI

Collector components are MPI-aware: each node accumulates its own records,
the records are gathered to the master node at the end of the run, and the
master writes a single file. The normalization is the total number of
simulated rays, whatever the number of nodes, so `weight / normalization` is
the same rate with and without MPI. No per-node files are produced and no
manual merge step is needed.

The legacy streaming components behave differently:

- `ReadoutCAEN` requires every node to have network
  access to the EFU host. Each node sends on its own EFU output queue (rank
  modulo 12, the EFU's queue count), so their packet sequence numbers do not
  collide. When HDF5 output is enabled (`filename=...`), each
  node writes `filename.node_N.h5`; the master merges them into one file in
  `FINALLY` unless `merge_mpi=0`, deleting the per-node files unless
  `keep_mpi_unmerged=1`. This merge supports the legacy flat layout with
  exactly one collector group per file.
- `ReadoutDiscreteCAEN` has no file output; its exact-count draw is made
  per node.

## 7. Classic McStas and NeXus output

The Collector components work with both McStas code generators: mccode-antlr
and the classic `mcstas`/`mcrun` (McStas 3.3+, for `SEARCH SHELL` and `CMD()`
dependencies). Every Collector has a `sink` parameter:

- `sink="auto"` (default): with `mcrun --format=NeXus` the records go into the
  McStas NeXus output file of the run (`mccode.h5`); otherwise into the
  collector HDF5 file named by `filename`.
- `sink="nexus"` / `sink="hdf5"`: force one of the two (`"nexus"` stops with an
  error when the run does not produce NeXus output).

With the NeXus sink no extra file is written. Each Collector stores its records
next to the component's other NeXus data, one typed dataset per record field
(the field names and types of the record description):

```
entry1/instrument/components/NNNN_<instance>/<dataset_name>   NXcollection, type="Readouts"
    ring, FEN, time, weight, channel, ...      1-D, one entry per record
    @description, @ess_type, @detector, @normalization, @records
    @efu_address, @efu_port                    (when given)
entry1/simulation/Param/...                    instrument parameters (written by McStas)
```

`readout-combine import --output run.h5 mccode.h5` turns that into an ordinary
collector file (same record layout as the HDF5 sink, hence EFU-sendable), ready
for `validate`, `append`/`concatenate` and `readout-replay`. A
`CollectorDiskChopper` writes nothing with the NeXus sink: its parameters are
stored with the component in the NeXus file, and the importer adds the
`<name>_chopper_tdc` parameter from its `tdc_pv`.

Whatever the sink, each Collector also reports a 0D monitor (number of records
and their summed rate), so it shows up in `mccode.sim` and `mcplot`.

Classic McStas notes: declare one variable per line in `USERVARS`, and keep in
mind that the time read with `tof="t"` is the ray time at the Collector's
position as the instrument has propagated it -- a Collector placed after a
monitor with `restore_neutron=1` sees the ray as it was before that monitor.
