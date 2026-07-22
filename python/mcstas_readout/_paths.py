"""Locate installed Readout resources: McStas components, headers, binaries.

Mirrors the discovery order in _lib.py (bundled wheel data first, otherwise
readout-config on PATH), so these resolve the same way regardless of whether
mcstas-readout-master came from a pip wheel, a conda environment, or a build
tree with readout-config on PATH.
"""
from __future__ import annotations

import subprocess
from pathlib import Path

from .exceptions import ReadoutError


def _wheel_data_dir() -> Path | None:
    try:
        from _readout_core import data_dir
    except ImportError:
        return None
    data = data_dir()
    return data if data.is_dir() else None


def _show(item: str) -> Path:
    try:
        result = subprocess.run(["readout-config", "--show", item],
                                capture_output=True, text=True, timeout=5, check=True)
    except (OSError, subprocess.SubprocessError) as exc:
        raise ReadoutError(
            f"could not resolve {item!r}: readout-config not found on PATH "
            "(install the mcstas-readout-master wheel, or put readout-config on PATH)"
        ) from exc
    return Path(result.stdout.strip())


def bin_dir() -> Path:
    """The directory containing the readout-config/-replay/-combine binaries."""
    wheel = _wheel_data_dir()
    return wheel / "bin" if wheel is not None else _show("bindir")


def include_dir() -> Path:
    """The directory containing the installed Readout public headers."""
    wheel = _wheel_data_dir()
    return wheel / "include" if wheel is not None else _show("includedir")


def comp_dir() -> Path:
    """The directory containing the Readout McStas components."""
    wheel = _wheel_data_dir()
    return wheel / "share" / "Readout" if wheel is not None else _show("compdir")


def cmakedir() -> Path:
    """The directory containing ReadoutConfig.cmake, usable as Readout_DIR for
    find_package(Readout). Only resolvable for a wheel install: readout-config
    has no --show cmakedir, since a conda or system install already puts its
    CMake package config on the normal find_package(Readout) search path."""
    try:
        from _readout_core import cmakedir as wheel_cmakedir
    except ImportError:
        raise ReadoutError(
            "cmakedir() is only resolvable for a pip-installed wheel; for a conda "
            "or system install, Readout's CMake package config is already on the "
            "normal find_package(Readout) search path"
        )
    return wheel_cmakedir()
