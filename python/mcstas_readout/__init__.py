"""Python control of libreadout replay and combine operations.

The compiled machinery lives in libreadout (shipped in the same
mcstas-readout-master distribution); this package drives it through the C API
in readout_capi.h via ctypes, so it works against a pip wheel, a conda
installation, or a build tree (see mcstas_readout._lib for the search order).

Typical use, replacing a `readout-replay` shell invocation::

    import mcstas_readout as ro

    class MyPublisher(ro.ParameterPublisher):
        def publish(self, point, name, value, unit):
            ...  # e.g. forward to EPICS

    config = ro.ReplayConfig(counting_time=2.0, seed=42)
    ro.replay("scan.h5", config, MyPublisher())
"""
from __future__ import annotations

from ._lib import lib_path, lib_version
from ._paths import bin_dir, cmakedir, comp_dir, include_dir
from .exceptions import ReadoutError
from .replay import (ParameterPublisher, Replay, ReplayConfig, ReplaySubset,
                     StreamParameterPublisher, replay)
from .combine import (append_collector_files, combine_collector_files,
                      concatenate_collector_files, validate_collector_file)


def _version() -> str:
    try:
        from importlib.metadata import version
        return version("mcstas-readout-master")
    except Exception:
        try:
            return lib_version()
        except Exception:
            return "0.0.0"


__version__ = _version()

__all__ = [
    "ReadoutError",
    "ParameterPublisher",
    "StreamParameterPublisher",
    "Replay",
    "ReplayConfig",
    "ReplaySubset",
    "replay",
    "validate_collector_file",
    "append_collector_files",
    "concatenate_collector_files",
    "combine_collector_files",
    "lib_path",
    "lib_version",
    "bin_dir",
    "include_dir",
    "comp_dir",
    "cmakedir",
    "__version__",
]
