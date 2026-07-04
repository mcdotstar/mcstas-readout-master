"""Locate and load libreadout, and declare its C API (readout_capi.h) prototypes.

The shared library is found by trying, in order:

1. the ``READOUT_LIB`` environment variable (a path to the library file itself),
   used by the test suite to point at a build tree;
2. the wheel layout, via the ``_readout_core`` shim package that carries the
   native binaries inside a pip-installed wheel;
3. ``readout-config --show libdir``/``--show libname`` for any installation
   whose binaries are on PATH (a conda environment, a system install, or a
   build tree) — readout-config is self-locating, so this always agrees with
   the library actually installed;
4. ``ctypes.util.find_library`` as a last resort.

The wrapper refuses to load a library whose READOUT_CAPI_ABI_VERSION differs
from the one it was written against.
"""
from __future__ import annotations

import ctypes
import ctypes.util
import os
import subprocess
import sys
from pathlib import Path

from .exceptions import ReadoutError

# The readout_capi.h ABI generation this wrapper implements.
ABI_VERSION = 1

# Status codes (enum readout_status in readout_capi.h)
OK = 0
STOPPED = 1
ERROR = -1

# Callback types (readout_publish_cb / readout_point_ready_cb).
# c_char_p arguments arrive in Python callbacks as bytes, or None for NULL.
PUBLISH_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint64,
                              ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p)
POINT_READY_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_uint64)


def _wheel_candidates() -> list[Path]:
    try:
        from _readout_core import data_dir
    except ImportError:
        return []
    data = data_dir()
    if sys.platform.startswith("win"):
        # DLLs (readout.dll and its hdf5.dll dependency) live beside the binaries
        bindir = data / "bin"
        if bindir.is_dir():
            os.add_dll_directory(str(bindir))
        return sorted(bindir.glob("readout.dll"))
    suffix = ".dylib" if sys.platform == "darwin" else ".so"
    exact = sorted(data.glob(f"lib*/libreadout{suffix}"))
    return exact if exact else sorted(data.glob(f"lib*/libreadout{suffix}*"))


def _readout_config_candidate() -> Path | None:
    try:
        libdir = subprocess.run(["readout-config", "--show", "libdir"],
                                capture_output=True, text=True, timeout=5, check=True).stdout.strip()
        libname = subprocess.run(["readout-config", "--show", "libname"],
                                 capture_output=True, text=True, timeout=5, check=True).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    path = Path(libdir) / libname
    return path if path.is_file() else None


def _find_library() -> str:
    override = os.environ.get("READOUT_LIB")
    if override:
        if Path(override).is_file():
            return override
        raise ReadoutError(f"READOUT_LIB points at a nonexistent file: {override}")
    for candidate in _wheel_candidates():
        return str(candidate)
    candidate = _readout_config_candidate()
    if candidate is not None:
        return str(candidate)
    found = ctypes.util.find_library("readout")
    if found:
        return found
    raise ReadoutError(
        "could not locate libreadout: set READOUT_LIB, install the mcstas-readout-master "
        "wheel, or put readout-config on PATH"
    )


def _declare(lib: ctypes.CDLL) -> ctypes.CDLL:
    handle = ctypes.c_void_p
    lib.readout_capi_abi_version.restype = ctypes.c_int
    lib.readout_capi_abi_version.argtypes = []
    lib.readout_version.restype = ctypes.c_char_p
    lib.readout_version.argtypes = []
    lib.readout_last_error.restype = ctypes.c_char_p
    lib.readout_last_error.argtypes = []

    lib.readout_replay_create.restype = handle
    lib.readout_replay_create.argtypes = []
    lib.readout_replay_destroy.restype = None
    lib.readout_replay_destroy.argtypes = [handle]

    for name, extra in (
        ("set_counting_time", [ctypes.c_double]),
        ("clear_counting_time", []),
        ("set_seed", [ctypes.c_uint32]),
        ("set_random_order", [ctypes.c_int]),
        ("set_default_endpoint", [ctypes.c_char_p, ctypes.c_int]),
        ("set_chunk_size", [ctypes.c_uint64]),
        ("set_subset", [ctypes.c_uint64, ctypes.c_uint64, ctypes.c_uint64]),
        ("clear_subset", []),
        ("set_pulse_rate", [ctypes.c_double]),
        ("set_fold_tof", [ctypes.c_int]),
        ("set_senders_json", [ctypes.c_char_p]),
    ):
        fn = getattr(lib, f"readout_replay_{name}")
        fn.restype = ctypes.c_int
        fn.argtypes = [handle, *extra]

    lib.readout_replay_run.restype = ctypes.c_int
    lib.readout_replay_run.argtypes = [handle, ctypes.c_char_p, PUBLISH_CB, POINT_READY_CB, ctypes.c_void_p]
    lib.readout_replay_request_stop.restype = None
    lib.readout_replay_request_stop.argtypes = [handle]
    lib.readout_replay_stop_requested.restype = ctypes.c_int
    lib.readout_replay_stop_requested.argtypes = [handle]
    lib.readout_replay_reset_stop.restype = None
    lib.readout_replay_reset_stop.argtypes = [handle]

    lib.readout_validate_collector_file.restype = ctypes.c_int64
    lib.readout_validate_collector_file.argtypes = [ctypes.c_char_p]
    string_array = ctypes.POINTER(ctypes.c_char_p)
    lib.readout_append_collector_files.restype = ctypes.c_int
    lib.readout_append_collector_files.argtypes = [ctypes.c_char_p, string_array, ctypes.c_size_t, ctypes.c_int]
    lib.readout_concatenate_collector_files.restype = ctypes.c_int
    lib.readout_concatenate_collector_files.argtypes = [ctypes.c_char_p, string_array, ctypes.c_size_t]
    lib.readout_combine_collector_files.restype = ctypes.c_int
    lib.readout_combine_collector_files.argtypes = [ctypes.c_char_p, string_array, ctypes.c_size_t]
    return lib


_lib: ctypes.CDLL | None = None
_lib_path: str | None = None


def load() -> ctypes.CDLL:
    """The libreadout CDLL singleton, located and ABI-checked on first use."""
    global _lib, _lib_path
    if _lib is None:
        path = _find_library()
        lib = _declare(ctypes.CDLL(path))
        abi = lib.readout_capi_abi_version()
        if abi != ABI_VERSION:
            raise ReadoutError(
                f"{path} implements C API ABI version {abi}, but this mcstas_readout "
                f"wrapper requires {ABI_VERSION}; upgrade whichever is older"
            )
        _lib, _lib_path = lib, path
    return _lib


def lib_path() -> str:
    """The filesystem path of the loaded libreadout."""
    load()
    assert _lib_path is not None
    return _lib_path


def lib_version() -> str:
    """The version string of the loaded libreadout, e.g. '0.4.0'."""
    return load().readout_version().decode()


def last_error() -> str:
    """The library's description of the most recent failure on this thread."""
    return load().readout_last_error().decode(errors="replace")


def check(status: int) -> int:
    """Raise ReadoutError when status is READOUT_ERROR; return status otherwise."""
    if status == ERROR:
        raise ReadoutError(last_error() or "libreadout call failed")
    return status
