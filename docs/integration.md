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

`ReadoutCAEN.comp`, `ReadoutTTLMonitor.comp`, and `ReadoutDiscreteCAEN.comp`
remain available for in-simulation runtime event streaming use-cases.

## 6. Running under MPI

Collector components are MPI-aware: each node accumulates its own records,
the records are gathered to the master node at the end of the run, and the
master writes a single HDF5 file with the normalization scaled by the node
count. No per-node files are produced and no manual merge step is needed.

The legacy streaming components behave differently:

- `ReadoutCAEN` and `ReadoutTTLMonitor` require every node to have network
  access to the EFU host. When HDF5 output is enabled (`filename=...`), each
  node writes `filename.node_N.h5`; the master merges them into one file in
  `FINALLY` unless `merge_mpi=0`, deleting the per-node files unless
  `keep_mpi_unmerged=1`. This merge supports the legacy flat layout with
  exactly one collector group per file.
- `ReadoutDiscreteCAEN` has no file output; its exact-count draw is made
  per node.
