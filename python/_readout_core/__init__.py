"""Runtime support for the mcstas-readout-master wheel.

The wheel is a container for the native libreadout, the CLI tools, the public
headers, and the McStas components: there is no Python API here. The console
entry points below exec the bundled binaries, which self-locate their
resources (readout-config resolves lib/include/component paths relative to its
own location inside this package's data directory).
"""
from __future__ import annotations

import os
import sys
from pathlib import Path


def data_dir() -> Path:
    """The wheel's install prefix: data_dir()/bin, .../include, .../share/Readout."""
    return Path(__file__).resolve().parent / "data"


def cmakedir() -> Path:
    """The directory containing ReadoutConfig.cmake, usable as Readout_DIR for
    find_package(Readout) against the wheel-installed library. Provided in
    Python because a cross-compiled readout-config binary may not be runnable
    on the build host (the mcpl-core pattern)."""
    matches = sorted(data_dir().glob("lib*/cmake/Readout"))
    if not matches:
        raise FileNotFoundError("ReadoutConfig.cmake not found in the wheel data directory")
    return matches[0]


def _executable(name: str) -> Path:
    exe = data_dir() / "bin" / (name + (".exe" if os.name == "nt" else ""))
    if not exe.is_file():
        raise FileNotFoundError(f"{name} not found in the wheel data directory: {exe}")
    return exe


def _exec(name: str) -> int:
    exe = str(_executable(name))
    argv = [exe, *sys.argv[1:]]
    if os.name == "nt":
        import subprocess

        raise SystemExit(subprocess.call(argv))
    os.execv(exe, argv)
    raise SystemExit(1)  # unreachable


def readout_config() -> int:
    return _exec("readout-config")


def readout_replay() -> int:
    return _exec("readout-replay")


def readout_combine() -> int:
    return _exec("readout-combine")
