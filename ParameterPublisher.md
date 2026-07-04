# ParameterPublisher: what an implementation needs

`ParameterPublisher` (lib/replay.h) is the hook through which replay hands each scan
point's instrument-parameter values to the outside world — in production, to EPICS so
that the downstream pipeline (Forwarder → Kafka → file writer) records the parameters
alongside the replayed events. The EPICS transport deliberately lives *outside* this
library (mccode-plumber territory) so libreadout stays dependency-light.

## The contract replay provides today

For each point `p` in file order, `replay()` calls, synchronously on the caller's thread:

1. `publish(p, name, value, unit)` once per parameter, in parameter-name-sorted order.
   `value` is the stored value rendered to a string with default `std::ostream`
   formatting; `unit` is the parameter's `unit` attribute when present.
2. `point_ready(p)` once, after all of the point's parameters.
3. Only then does replay wait for the next pulse-grid tick (`ReplayConfig::pulse_rate`,
   default 14 Hz) and start sending the point's readouts — so every reference (pulse)
   time carried by the point's events is a wall-clock instant *after* `point_ready`
   returned.

Guarantees and properties an implementation may rely on:

- **Blocking is supported by design.** The calls run on the replay thread; an
  implementation may block inside `publish` or `point_ready` (e.g. waiting for a
  channel-access completion callback or a settle time) and replay will not send the
  point's events until it returns.
- **Every point is published**, even when a `ReplaySubset` stops producing events
  before the last point, and even for files with no parameters (`point_ready` still
  fires per point).
- **Exceptions propagate** out of `replay()` and abort the replay. Throw if a failed
  parameter update must stop the run; swallow and log if it must not.
- Existing implementations: `NullParameterPublisher` (default), `StreamParameterPublisher`
  (prints `point N: name = value [unit]`; used by `readout-replay --verbose`), and the
  `RecordingPublisher` test pattern (test/replay_test.cpp).

## What an EPICS implementation needs

1. **A PV-name mapping.** Files store bare parameter names (e.g. `chopper_speed`); EPICS
   needs fully qualified PV names. Provide either a prefix rule (`<instrument>:` +
   name) or an explicit JSON map in the style of SenderConfigs. This configuration
   belongs to the implementing tool, not the file format.
2. **Typed values, or string re-parsing.** `publish` delivers strings. EPICS PVs are
   typed, so the implementation must either re-parse (`stod`/`stoll` per PV type) or —
   cleaner — hold the `ReaderSource` itself and use its typed accessors
   (`parameter_is_double`/`parameter_double_value`, etc.) keyed by the `name` argument,
   treating the `publish` value only as a fallback. Both work today; if string
   round-tripping proves lossy for doubles, see "candidate extensions".
3. **Propagation settling.** The DAQ pipeline must observe the new parameter values
   *before* the point's events arrive (or at least with timestamps that order
   correctly). Block in `point_ready` until the writes complete (CA/pvAccess put
   callbacks) plus any Forwarder/Kafka latency margin. A fixed configurable delay is
   the crude-but-effective baseline. Blocking here is exactly what preserves causal
   ordering end-to-end: replay only picks the point's first pulse time — and hence the
   earliest possible event timestamp — after `point_ready` returns.
4. **The integration shape: drive libreadout, in C++ or Python.** The recommended
   structure is an external tool (mccode-plumber) that constructs its EPICS-backed
   `ParameterPublisher`, builds a `ReplayConfig`, and calls
   `replay(filename, config, publisher)` — either by linking libreadout from C++, or
   through the `mcstas_readout` Python package (see below), which lets the EPICS
   transport be a Python implementation (e.g. p4p) with no compiled code. Building an
   EPICS backend *into* libreadout behind a build flag was considered and rejected: it
   drags EPICS client libraries into every consumer.
5. **Run bracketing (outside the interface).** DAQ runs are started/stopped by run
   control (NICOS / file-writer commands). Replay has no run-control hooks; the
   implementing tool should bracket the whole `replay()` call (or individual points,
   using `point_ready` and its knowledge of `ReaderSource::points()`) with its own
   start/stop commands.

## Implementing the publisher in Python

The `mcstas_readout` package (shipped in the same distribution as libreadout) exposes
the identical contract through the C API in `readout_capi.h`: subclass
`mcstas_readout.ParameterPublisher`, implement `publish(point, name, value, unit)` and
optionally `point_ready(point)`, and call `mcstas_readout.replay(filename, config,
publisher)`. The callbacks run synchronously on the replaying thread with the same
guarantees as the C++ interface — blocking in `point_ready` still gates the point's
pulse start, and an exception raised in a callback stops the replay and re-raises from
`replay()`.

Python additionally gets cooperative cancellation: `Replay.cancel()` (thread-safe,
callable while `run()` blocks) stops the replay at the next point or chunk boundary and
makes `run()` return `False`. A stop that lands between a point's parameter publication
and its pulse start suppresses *all* of that point's events, so a publisher that
cancels on a failed parameter write never lets mistimed events reach the EFUs. The same
mechanism is available to C++ callers through `ReplayConfig::stop` (a caller-owned
`std::atomic<bool>`; `replay()` returns `false` when stopped) and to C callers through
`readout_replay_request_stop()`.

## Known limitations to plan around

- **Events are not paced.** A point's events are sent as fast as UDP framing allows,
  not spread over the counting time; parameter validity intervals in the written file
  will be much shorter than the simulated counting time. (The reference *clock* is
  paced — pulse times march on the configured `pulse_rate` grid as the wall clock
  passes each tick — but the events themselves are not.) If the downstream consumers
  need realistic wall-clock pacing, that is a replay feature to add (see below), not a
  publisher concern.
- **Pulse times are not passed to the publisher.** The causal ordering (parameters
  first, then strictly-later reference times) is guaranteed by replay itself, so an
  implementation timestamping parameters at publication orders correctly. A publisher
  wanting the exact pulse-time base (e.g. to back-date timestamps onto the pulse grid)
  would need the interface extended.
- **Descriptions are not passed.** The file stores an optional description attribute
  per parameter (visible via `ReaderSource::parameter_views()`), but `publish` only
  receives the unit. An EPICS implementation wanting to fill `DESC` fields should hold
  the `ReaderSource`.

## Candidate interface extensions (add when a consumer needs them, not before)

- Typed `publish` overloads (or passing `const ReaderSource &` + point) to avoid
  string round-trips.
- `run_begin()` / `run_end()` virtuals so the publisher can drive run control itself.
- A paced-replay mode (`events spread over counting_time`) with per-point wall-clock
  scheduling.
- Passing the point's pulse-time base to `point_ready`.
