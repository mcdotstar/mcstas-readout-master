# Python API {#python-api}

The `mcstas_readout` package drives replay and combine operations from Python.
It is pure Python: it locates the installed libreadout and calls its C API
(readout_capi.h) through ctypes, so the same package works against a pip
wheel, a conda installation, or a development build tree. It exists to lift
integrations like mccode-plumber from shelling out to `readout-replay` to
controlling replay in-process — including implementing the
[ParameterPublisher](../ParameterPublisher.md) in Python.

## Locating the library

`import mcstas_readout` finds libreadout by trying, in order:

1. the `READOUT_LIB` environment variable — a path to the library file itself
   (used by the test suite to select a build tree);
2. the wheel layout, through the `_readout_core` shim package bundled in the
   `mcstas-readout-master` wheel;
3. `readout-config --show libdir` / `--show libname` for any installation with
   the CLI tools on PATH (a conda environment, a system prefix, or a build
   tree) — readout-config is self-locating, so this always matches the
   installed library;
4. `ctypes.util.find_library("readout")`.

The load fails with `ReadoutError` when the library's C API generation
(`readout_capi_abi_version()`) differs from the one the wrapper was written
against. `mcstas_readout.lib_path()` and `lib_version()` report what was loaded.

## Replay

```python
import mcstas_readout as ro

class EpicsPublisher(ro.ParameterPublisher):
    def publish(self, point, name, value, unit):
        ...  # e.g. a pvAccess put for the PV mapped to `name`
    def point_ready(self, point):
        ...  # block until the writes have propagated

config = ro.ReplayConfig(counting_time=2.0, seed=42,
                         default_address="127.0.0.1", default_port=9000)
completed = ro.replay("scan.h5", config, EpicsPublisher())
```

`ReplayConfig` mirrors the C++ struct in replay.h field for field
(`counting_time`, `seed`, `random_order`, `subset`, `pulse_rate`, `fold_tof`,
...); explicit EFU routing is given as `senders_json`, the same JSON document
`readout-replay --config` accepts. The publisher contract — per-point
`publish` calls in name order, then `point_ready`, all before the point's
events are sent, blocking supported, exceptions abort the run — is identical
to the C++ interface and documented in [ParameterPublisher](../ParameterPublisher.md).

### Cancellation and threading

`Replay` is the resumable form of `replay()`:

```python
job = ro.Replay("scan.h5", config, publisher)
threading.Thread(target=job.run).start()
...
job.cancel()   # thread-safe; run() returns False at the next boundary
```

ctypes releases the GIL for the duration of `run()` and re-acquires it for
each publisher callback, so `cancel()` from another Python thread always
takes effect — at the next scan-point boundary, at the next `chunk_size`
readouts within a point, and (checked specially) between a point's parameter
publication and its pulse start, in which case none of that point's events
are sent. Ctrl-C is only serviced while Python code runs (once per point, in
a callback), so interactive use should run `run()` on a worker thread and
cancel from the main thread.

## Combine

```python
n = ro.validate_collector_file("point_0.h5")            # scan points, or raises
ro.append_collector_files("sum.h5", files)               # identical parameters
ro.concatenate_collector_files("scan.h5", files)         # consistent parameters
ro.combine_collector_files("out.h5", files)              # choose automatically
```

These mirror the `readout-combine` subcommands (see [Command-line tools](cli.md));
failures raise `ReadoutError`. The underlying C++ routines print their detailed
diagnostics to stderr.

## Example

`examples/replay_publisher.py` is the Python twin of step 3 of
`examples/run_example.sh`: it validates a combined file, replays it with a
stdout publisher (`StreamParameterPublisher`, the `--verbose` equivalent), and
optionally demonstrates timed cancellation via `--cancel-after`.

## For other languages

The same functionality is reachable from any FFI through the
`extern "C"` surface in readout_capi.h: an opaque configuration handle with
setters, `readout_replay_run` with publish/point-ready function pointers,
`readout_replay_request_stop` for cancellation, and string-array combine
wrappers. No exception crosses the boundary; failures return
`READOUT_ERROR` and are described by `readout_last_error()`.
