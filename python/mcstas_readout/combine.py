"""Python access to collector-file validation and combination.

Mirrors the readout-combine CLI (and CollectorClass.h): validate a file,
append same-point files, concatenate different-point files, or let combine
choose automatically. Failures raise ReadoutError; the C++ implementation may
print additional diagnostic detail to stderr.
"""
from __future__ import annotations

import ctypes
import os
from collections.abc import Sequence
from pathlib import Path

from . import _lib
from .exceptions import ReadoutError


def _string_array(paths: Sequence[str | os.PathLike]):
    encoded = [os.fspath(p).encode() for p in paths]
    return (ctypes.c_char_p * len(encoded))(*encoded), len(encoded)


def validate_collector_file(filename: str | os.PathLike) -> int:
    """The number of scan points in a valid collector file; raises ReadoutError otherwise."""
    lib = _lib.load()
    points = lib.readout_validate_collector_file(os.fspath(filename).encode())
    if points < 0:
        raise ReadoutError(_lib.last_error() or f"{filename} is not a valid collector file")
    return int(points)


def _combine(fn_name: str, out: str | os.PathLike, ins: Sequence[str | os.PathLike], *extra) -> None:
    if len(ins) < 2:
        raise ReadoutError(f"{fn_name} requires at least 2 input files")
    if Path(os.fspath(out)).exists():
        raise ReadoutError(f"output file already exists: {out}")
    lib = _lib.load()
    array, count = _string_array(ins)
    _lib.check(getattr(lib, fn_name)(os.fspath(out).encode(), array, count, *extra))


def append_collector_files(out: str | os.PathLike, ins: Sequence[str | os.PathLike],
                           reset_datasets: bool = False) -> None:
    """Combine same-point collector files (identical parameters) by appending readouts.

    When reset_datasets is True the input files' readout datasets are zeroed
    after a successful combination.
    """
    _combine("readout_append_collector_files", out, ins, 1 if reset_datasets else 0)


def concatenate_collector_files(out: str | os.PathLike, ins: Sequence[str | os.PathLike]) -> None:
    """Combine different-point collector files into one multi-point cue-based file."""
    _combine("readout_concatenate_collector_files", out, ins)


def combine_collector_files(out: str | os.PathLike, ins: Sequence[str | os.PathLike]) -> None:
    """Combine collector files, choosing append or concatenate from their parameters."""
    _combine("readout_combine_collector_files", out, ins)
