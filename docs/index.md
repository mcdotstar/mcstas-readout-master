# McStas Readout Master documentation

This documentation targets expert McStas users who need to move from simulation
to ESS DAQ-compatible event replay using the collect -> combine -> replay flow.

## Start here

1. [Installation](install.md) for local builds and install prefixes.
2. [Component integration](integration.md) for instrument wiring and parameter mapping.
3. [Collector files](collector-files.md) for HDF5 layout, validation, combining, and replay.
4. [Examples](examples.md) for complete snippets and command sequences.

## API reference

Public API pages are generated from in-code comments in:

- `readout_core/include/` (collector/reader/replay classes and C API headers)
- `readout_core/app_replay/` and `readout_core/app_combine/` (CLI behavior)
- `readout_core/components/*.comp` (Collector component behavior and parameters)

## Architecture notes

For architectural context and current-state constraints, see `AGENT.md` in the
repository root.
