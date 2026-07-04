"""Exceptions raised by the mcstas_readout package."""
from __future__ import annotations


class ReadoutError(RuntimeError):
    """A libreadout operation failed; the message comes from readout_last_error()."""
