# McStas Readout Master documentation

This documentation targets expert McStas users who need to move from simulation
to ESS DAQ-compatible event replay using the collect -> combine -> replay flow.

## Start here

1. [Installation](install.md) for local builds and install prefixes.
2. [Component integration](integration.md) for instrument wiring and parameter mapping.
3. [Components](@ref components) for per-component parameter references,
   generated from the in-source McDoc headers.
4. [Collector files](collector-files.md) for HDF5 layout, validation, combining, and replay.
5. [Command-line tools](cli.md) for `readout-config`, `readout-combine`, and `readout-replay`.
6. [Python API](python.md) for driving replay and combine from Python (`mcstas_readout`).
7. [Examples](examples.md) for complete snippets and command sequences.

## Going deeper

- [Architecture](architecture.md) — the collection engine, EFU-sendability rules,
  and replay endpoint resolution.
- [ParameterPublisher](../ParameterPublisher.md) — the interface contract for
  publishing per-point parameters (EPICS transport lives outside this repository).

## API reference

Public API pages are generated from in-code comments in `readout_core/include/`:
the collector engine (CollectorClass.h), file access (reader.h), replay
(replay.h), the C API for foreign-function bindings (readout_capi.h), EFU
senders (Sender.h, SenderConfigs.h), the readout type registry (enums.h,
readout_type_descriptions.h), and the legacy streaming classes
(ReadoutClass.h, writer.h).
