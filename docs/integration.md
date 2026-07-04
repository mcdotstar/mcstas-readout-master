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
