"""Python control of collector-file replay.

Mirrors the C++ interface in replay.h through the C API in readout_capi.h:
``ReplayConfig`` configures a run, a ``ParameterPublisher`` subclass receives
each point's instrument parameters before the point's events are sent, and
``Replay.run()`` (or the module-level ``replay()``) performs the blocking replay.

Threading: ctypes releases the GIL for the duration of ``run()`` and re-acquires
it for each publisher callback, so ``cancel()`` may be called from any other
Python thread at any time and takes effect at the next point or chunk boundary.
Ctrl-C in the main thread is only serviced while Python code runs (i.e. inside a
publisher callback, once per point); latency-sensitive interactive use should
call ``run()`` on a worker thread and ``cancel()`` from the main thread.
"""
from __future__ import annotations

import os
from abc import ABC, abstractmethod
from dataclasses import dataclass, field

from . import _lib
from .exceptions import ReadoutError


@dataclass
class ReplaySubset:
    """Restrict replay to stored readouts with global index first + k * every, k in [0, number)."""
    first: int = 0
    number: int = 0
    every: int = 1


@dataclass
class ReplayConfig:
    """Replay configuration, mirroring ReplayConfig in replay.h.

    counting_time: seconds; when set, a stored readout with rate-weight w is sent
        n ~ Poisson(w * counting_time) times, when None every readout is sent once.
    seed: sampling/shuffling generator seed; 0 selects a non-deterministic seed.
    random_order: shuffle events within each (point, collector group) before sending.
    senders_json: explicit per-(detector, readout) EFU endpoints as a JSON document
        (the SenderConfigs layout also accepted by ``readout-replay --config``).
    subset: optional ReplaySubset of the stored readouts.
    pulse_rate: pulse repetition rate in Hz (ESS: 14).
    fold_tof: stamp events at pulse + (tof % period) so long-flight events wrap
        into the frame they would be detected in.
    """
    counting_time: float | None = None
    seed: int = 0
    random_order: bool = False
    senders_json: str | None = None
    default_address: str = "127.0.0.1"
    default_port: int = 9000
    chunk_size: int = 65536
    subset: ReplaySubset | None = None
    pulse_rate: float = 14.0
    fold_tof: bool = False


class ParameterPublisher(ABC):
    """Receives per-point instrument parameters as replay steps through a file.

    ``publish`` is called once per (point, parameter) in name order, then
    ``point_ready`` once, before any of the point's events are sent. Both run
    synchronously on the thread executing ``Replay.run()`` and may block; an
    exception raised here stops the replay and is re-raised from ``run()``.
    """

    @abstractmethod
    def publish(self, point: int, name: str, value: str, unit: str | None) -> None:
        """One parameter value for one point; unit is None when the parameter has no unit."""

    def point_ready(self, point: int) -> None:
        """All of the point's parameters have been published."""


class StreamParameterPublisher(ParameterPublisher):
    """Prints "point N: name = value [unit]" lines, like readout-replay --verbose."""

    def __init__(self, stream=None):
        import sys
        self.stream = stream if stream is not None else sys.stdout

    def publish(self, point: int, name: str, value: str, unit: str | None) -> None:
        suffix = f" [{unit}]" if unit is not None else ""
        print(f"point {point}: {name} = {value}{suffix}", file=self.stream)


class Replay:
    """A replayable collector file with cooperative cancellation.

    Usable as a context manager; the underlying handle is freed by ``close()``
    (or garbage collection). ``run()`` may be called repeatedly — each call
    re-applies the configuration and clears any previous stop request.
    """

    def __init__(self, filename: str | os.PathLike, config: ReplayConfig | None = None,
                 publisher: ParameterPublisher | None = None):
        self._filename = os.fspath(filename)
        self.config = config if config is not None else ReplayConfig()
        self.publisher = publisher
        self._lib = _lib.load()
        self._handle = self._lib.readout_replay_create()
        if not self._handle:
            raise ReadoutError(_lib.last_error() or "failed to create a replay handle")
        self._running = False
        # set by a publisher callback that raised; re-raised after run() returns
        self._pending_exception: BaseException | None = None

    def close(self) -> None:
        """Free the native handle; the object is unusable afterwards."""
        if getattr(self, "_handle", None):
            self._lib.readout_replay_destroy(self._handle)
            self._handle = None

    def __enter__(self) -> "Replay":
        return self

    def __exit__(self, *exc_info) -> None:
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _require_handle(self):
        if not self._handle:
            raise ReadoutError("this Replay has been closed")

    def _apply_config(self) -> None:
        config = self.config
        lib, handle = self._lib, self._handle
        if config.counting_time is not None:
            _lib.check(lib.readout_replay_set_counting_time(handle, float(config.counting_time)))
        else:
            _lib.check(lib.readout_replay_clear_counting_time(handle))
        _lib.check(lib.readout_replay_set_seed(handle, int(config.seed)))
        _lib.check(lib.readout_replay_set_random_order(handle, 1 if config.random_order else 0))
        _lib.check(lib.readout_replay_set_default_endpoint(
            handle, str(config.default_address).encode(), int(config.default_port)))
        _lib.check(lib.readout_replay_set_chunk_size(handle, int(config.chunk_size)))
        if config.subset is not None:
            _lib.check(lib.readout_replay_set_subset(
                handle, int(config.subset.first), int(config.subset.number), int(config.subset.every)))
        else:
            _lib.check(lib.readout_replay_clear_subset(handle))
        _lib.check(lib.readout_replay_set_pulse_rate(handle, float(config.pulse_rate)))
        _lib.check(lib.readout_replay_set_fold_tof(handle, 1 if config.fold_tof else 0))
        if config.senders_json is not None:
            _lib.check(lib.readout_replay_set_senders_json(handle, str(config.senders_json).encode()))

    def _trampolines(self):
        publisher = self.publisher

        def on_publish(_user_data, point, name, value, unit):
            try:
                publisher.publish(int(point),
                                  name.decode(errors="replace") if name is not None else "",
                                  value.decode(errors="replace") if value is not None else "",
                                  unit.decode(errors="replace") if unit is not None else None)
                return 0
            except BaseException as exc:  # noqa: BLE001 — includes KeyboardInterrupt, re-raised after run()
                self._pending_exception = exc
                return 1

        def on_point_ready(_user_data, point):
            try:
                publisher.point_ready(int(point))
                return 0
            except BaseException as exc:  # noqa: BLE001
                self._pending_exception = exc
                return 1

        return _lib.PUBLISH_CB(on_publish), _lib.POINT_READY_CB(on_point_ready)

    def run(self) -> bool:
        """Replay the file; blocking.

        Returns True when the file was replayed to completion, False when
        stopped early via cancel(). Exceptions raised by the publisher (and
        Ctrl-C delivered inside a callback) abort the replay and re-raise here;
        library failures raise ReadoutError.
        """
        self._require_handle()
        if self._running:
            raise ReadoutError("this Replay is already running")
        self._apply_config()
        # each run starts unstopped: a cancel() from a previous (or pre-run) request is cleared
        self._lib.readout_replay_reset_stop(self._handle)
        self._pending_exception = None
        if self.publisher is not None:
            publish_cb, ready_cb = self._trampolines()
        else:
            publish_cb, ready_cb = _lib.PUBLISH_CB(0), _lib.POINT_READY_CB(0)
        # The CFUNCTYPE objects must stay referenced while the C side may call
        # them — dropping them mid-run is a use-after-free.
        self._callbacks = (publish_cb, ready_cb)
        self._running = True
        try:
            status = self._lib.readout_replay_run(
                self._handle, self._filename.encode(), publish_cb, ready_cb, None)
        finally:
            self._running = False
            self._callbacks = None
        if self._pending_exception is not None:
            exc, self._pending_exception = self._pending_exception, None
            raise exc
        return _lib.check(status) == _lib.OK

    def cancel(self) -> None:
        """Request a running replay stop at the next point or chunk boundary.

        Thread-safe: intended to be called from another thread while run()
        blocks. A stop between a point's parameter publication and its pulse
        start suppresses all of that point's events.
        """
        self._require_handle()
        self._lib.readout_replay_request_stop(self._handle)

    @property
    def cancelled(self) -> bool:
        """True when a stop has been requested and not yet cleared by run()."""
        self._require_handle()
        return bool(self._lib.readout_replay_stop_requested(self._handle))


def replay(filename: str | os.PathLike, config: ReplayConfig | None = None,
           publisher: ParameterPublisher | None = None) -> bool:
    """Replay a collector file; blocking. See Replay.run() for semantics."""
    with Replay(filename, config, publisher) as job:
        return job.run()
