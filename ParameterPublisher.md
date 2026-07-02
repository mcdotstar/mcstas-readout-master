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
3. Only then are the point's readouts sampled and sent to the EFU(s).

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
   the crude-but-effective baseline.
4. **The integration shape: link against libreadout.** The recommended structure is an
   external tool (mccode-plumber) that links libreadout, constructs its EPICS-backed
   `ParameterPublisher`, builds a `ReplayConfig`, and calls
   `replay(filename, config, publisher)`. Building an EPICS backend *into* libreadout
   behind a build flag was considered and rejected: it drags EPICS client libraries
   into every consumer.
5. **Run bracketing (outside the interface).** DAQ runs are started/stopped by run
   control (NICOS / file-writer commands). Replay has no run-control hooks; the
   implementing tool should bracket the whole `replay()` call (or individual points,
   using `point_ready` and its knowledge of `ReaderSource::points()`) with its own
   start/stop commands.

## Known limitations to plan around

- **Events are not paced.** A point's events are sent as fast as UDP framing allows,
  not spread over the counting time; parameter validity intervals in the written file
  will be much shorter than the simulated counting time. If the downstream consumers
  need realistic wall-clock pacing, that is a replay feature to add (see below), not a
  publisher concern.
- **Pulse times are not exposed.** Sender pulse timestamps advance at point boundaries
  but are not passed to the publisher; if parameter timestamps must correlate with
  event pulse times rather than wall clock, the interface needs extending.
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
