"""Tests of the mcstas_readout ctypes package against a build tree.

These need only a built libreadout (no mccode-antlr, no C compiler): the
fixture collector files are written through the library's own C collector API.
Point the tests at a specific library with READOUT_LIB, or at a build tree
with READOUT_BUILD_DIR (see conftest.py); otherwise build/ or build-dev/ is
used automatically.
"""
from __future__ import annotations

import ctypes
import os
import sys
import threading
from pathlib import Path

import pytest

from conftest import BUILD_DIR, REPO_ROOT

# Make the in-repo package importable without installation
sys.path.insert(0, str(REPO_ROOT / "python"))


def _find_built_library() -> Path | None:
    env = os.environ.get("READOUT_LIB")
    if env and Path(env).is_file():
        return Path(env)
    if BUILD_DIR is None:
        return None
    for pattern in ("lib*/libreadout.so", "lib*/libreadout.dylib", "bin/readout.dll"):
        for candidate in sorted(BUILD_DIR.glob(pattern)):
            return candidate
    return None


LIBREADOUT = _find_built_library()

requires_library = pytest.mark.skipif(
    LIBREADOUT is None,
    reason="Python API tests require a built libreadout (build the project or set READOUT_LIB)",
)

if LIBREADOUT is not None:
    os.environ["READOUT_LIB"] = str(LIBREADOUT)


class CaenReadout(ctypes.Structure):
    # struct CAEN_readout in readout_structs.h; native alignment matches the C layout
    _fields_ = [("channel", ctypes.c_uint8), ("a", ctypes.c_uint16), ("b", ctypes.c_uint16),
                ("c", ctypes.c_uint16), ("d", ctypes.c_uint16)]


def _collector_api() -> ctypes.CDLL:
    lib = ctypes.CDLL(str(LIBREADOUT))
    lib.collector_new.restype = ctypes.c_void_p
    lib.collector_new.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int, ctypes.c_uint64]
    lib.collector_add.restype = None
    lib.collector_add.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8,
                                  ctypes.c_double, ctypes.c_double, ctypes.c_void_p]
    lib.collector_sink_double.restype = None
    lib.collector_sink_double.argtypes = [ctypes.c_char_p, ctypes.c_double, ctypes.c_char_p, ctypes.c_char_p]
    lib.collector_free.restype = None
    lib.collector_free.argtypes = [ctypes.c_void_p]
    return lib


def write_caen_point_file(path: Path, chopper_speed: float, rays: int = 50) -> Path:
    """One single-point CAEN collector file with a chopper_speed parameter."""
    lib = _collector_api()
    collector = lib.collector_new(str(path).encode(), b"events", 0x34, 1)
    assert collector, "collector_new failed"
    lib.collector_sink_double(b"chopper_speed", chopper_speed, b"Hz", None)
    for i in range(rays):
        record = CaenReadout(channel=3, a=i, b=rays - i, c=0, d=0)
        lib.collector_add(collector, 1, 0, i / rays, 1.0, ctypes.byref(record))
    lib.collector_free(collector)
    return path


class RecordingPublisher:
    def __init__(self):
        self.published: list[tuple[int, str, str, str | None]] = []
        self.ready: list[int] = []

    def publish(self, point, name, value, unit):
        self.published.append((point, name, value, unit))

    def point_ready(self, point):
        self.ready.append(point)


@requires_library
def test_loader_finds_library_and_checks_abi():
    import mcstas_readout as ro
    assert Path(ro.lib_path()).resolve() == LIBREADOUT.resolve()
    assert len(ro.lib_version()) > 0


@requires_library
def test_validate_missing_file_raises(tmp_path):
    import mcstas_readout as ro
    with pytest.raises(ro.ReadoutError, match="not a valid collector file"):
        ro.validate_collector_file(tmp_path / "missing.h5")


@requires_library
def test_replay_missing_file_raises(tmp_path):
    import mcstas_readout as ro
    with pytest.raises(ro.ReadoutError):
        ro.replay(tmp_path / "missing.h5")


@requires_library
def test_bad_senders_json_raises_before_replay(tmp_path):
    import mcstas_readout as ro
    config = ro.ReplayConfig(senders_json="{not json")
    with pytest.raises(ro.ReadoutError, match="parse"):
        ro.replay(tmp_path / "irrelevant.h5", config)


@requires_library
def test_invalid_config_values_raise(tmp_path):
    import mcstas_readout as ro
    with pytest.raises(ro.ReadoutError, match="counting time"):
        ro.replay(tmp_path / "irrelevant.h5", ro.ReplayConfig(counting_time=-1.0))
    with pytest.raises(ro.ReadoutError, match="pulse rate"):
        ro.replay(tmp_path / "irrelevant.h5", ro.ReplayConfig(pulse_rate=0.0))


@requires_library
def test_replay_publishes_parameters_and_completes(tmp_path):
    import mcstas_readout as ro

    class Recorder(RecordingPublisher, ro.ParameterPublisher):
        pass

    filename = write_caen_point_file(tmp_path / "point.h5", 100.0)
    assert ro.validate_collector_file(filename) == 1

    publisher = Recorder()
    # a fast pulse grid keeps the per-point wait short; the port is unmonitored
    config = ro.ReplayConfig(pulse_rate=50.0, default_port=29876)
    assert ro.replay(filename, config, publisher) is True
    assert publisher.published == [(0, "chopper_speed", "100", "Hz")]
    assert publisher.ready == [0]


@requires_library
def test_publisher_exception_propagates(tmp_path):
    import mcstas_readout as ro

    class Failing(ro.ParameterPublisher):
        def publish(self, point, name, value, unit):
            raise ValueError("publisher rejected the point")

    filename = write_caen_point_file(tmp_path / "point.h5", 100.0)
    config = ro.ReplayConfig(pulse_rate=50.0, default_port=29876)
    with pytest.raises(ValueError, match="publisher rejected"):
        ro.replay(filename, config, Failing())


@requires_library
def test_cancel_from_thread_stops_and_reset_allows_rerun(tmp_path):
    import mcstas_readout as ro

    filename = write_caen_point_file(tmp_path / "point.h5", 100.0)
    config = ro.ReplayConfig(pulse_rate=50.0, default_port=29876)

    class Gate(ro.ParameterPublisher):
        def __init__(self):
            self.entered = threading.Event()

        def publish(self, point, name, value, unit):
            pass

        def point_ready(self, point):
            self.entered.set()

    with ro.Replay(filename, config, Gate()) as job:
        canceller = threading.Thread(target=lambda: (job.publisher.entered.wait(10), job.cancel()))
        canceller.start()
        assert job.run() is False  # stopped between point_ready and the pulse start
        canceller.join()
        assert job.cancelled  # sticky until the next run clears it
        job.publisher = None
        assert job.run() is True  # run() resets the stop and completes


@requires_library
def test_combine_operations(tmp_path):
    import mcstas_readout as ro

    file_a = write_caen_point_file(tmp_path / "a.h5", 100.0)
    file_b = write_caen_point_file(tmp_path / "b.h5", 200.0)

    out = tmp_path / "multi.h5"
    ro.concatenate_collector_files(out, [file_a, file_b])
    assert ro.validate_collector_file(out) == 2

    auto = tmp_path / "auto.h5"
    ro.combine_collector_files(auto, [file_a, file_b])
    assert ro.validate_collector_file(auto) == 2

    # append requires identical parameters; these differ (100 vs 200)
    with pytest.raises(ro.ReadoutError):
        ro.append_collector_files(tmp_path / "bad.h5", [file_a, file_b])

    # refusal to overwrite an existing output
    with pytest.raises(ro.ReadoutError, match="already exists"):
        ro.concatenate_collector_files(out, [file_a, file_b])

    with pytest.raises(ro.ReadoutError, match="at least 2"):
        ro.concatenate_collector_files(tmp_path / "one.h5", [file_a])
