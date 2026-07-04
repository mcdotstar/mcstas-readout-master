"""Compilation tests for every Readout component.

Each test verifies that an instrument containing a single Readout component
can be successfully compiled by mccode-antlr + gcc.  No simulation is run.
"""
from __future__ import annotations

import os
import pytest
from textwrap import dedent
from conftest import (
    SHARE_READOUT,
    requires_integration,
    _build_env,
    CAEN_USERVARS,
    CAEN_ORIGIN_EXTEND,
    TTL_USERVARS,
    TTL_ORIGIN_EXTEND,
)


def _compile_instrument(instr_source: str) -> None:
    """Parse and compile an instrument source string (no run)."""
    from mccode_antlr.loader import parse_mcstas_instr
    from mccode_antlr.reader.registry import registry_from_specification
    from mccode_antlr.utils import compile_and_run

    old_path = os.environ.get("PATH", "")
    try:
        os.environ["PATH"] = _build_env()["PATH"]
        reg = registry_from_specification(str(SHARE_READOUT))
        instr = parse_mcstas_instr(instr_source, registries=[reg])
        compile_and_run(instr, None, run=False)
    finally:
        os.environ["PATH"] = old_path


# -----------------------------------------------------------------------
# ReadoutCAEN
# -----------------------------------------------------------------------
@requires_integration
class TestCompileReadoutCAEN:
    def test_compile_broadcast_off(self):
        _compile_instrument(dedent(f"""
        DEFINE INSTRUMENT test_readout_caen_compile()
        {CAEN_USERVARS}
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        {CAEN_ORIGIN_EXTEND}
        COMPONENT readout = ReadoutCAEN(
          ring="RING", fen="FEN", tube="TUBE",
          event_mode="p", a_name="A", b_name="B", tof="tof",
          ip="127.0.0.1", port=9000, broadcast=0
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))

    def test_compile_weight_squared_mode(self):
        _compile_instrument(dedent(f"""
        DEFINE INSTRUMENT test_readout_caen_pp()
        {CAEN_USERVARS}
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        {CAEN_ORIGIN_EXTEND}
        COMPONENT readout = ReadoutCAEN(
          ring="RING", fen="FEN", tube="TUBE",
          event_mode="pp", a_name="A", b_name="B", tof="tof",
          ip="127.0.0.1", port=9000, broadcast=0
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))


# -----------------------------------------------------------------------
# CollectorCAEN
# -----------------------------------------------------------------------
@requires_integration
class TestCompileCollectorCAEN:
    def test_compile_basic(self):
        _compile_instrument(dedent(f"""
        DEFINE INSTRUMENT test_collect_caen_compile(string filename="output")
        {CAEN_USERVARS}
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        {CAEN_ORIGIN_EXTEND}
        COMPONENT collector = CollectorCAEN(
          ring="RING", fen="FEN", tube="TUBE",
          a_name="A", b_name="B", tof="tof",
          filename=filename, verbose=1
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))


# -----------------------------------------------------------------------
# ReadoutTTLMonitor
# -----------------------------------------------------------------------
@requires_integration
class TestCompileReadoutTTLMonitor:
    def test_compile_basic(self):
        _compile_instrument(dedent(f"""
        DEFINE INSTRUMENT test_ttl_compile()
        {TTL_USERVARS}
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        {TTL_ORIGIN_EXTEND}
        COMPONENT monitor = ReadoutTTLMonitor(
          ring="RING", fen="FEN",
          position="A", identity="TUBE", value="B", tof="tof",
          ip="127.0.0.1", port=9001, broadcast=0
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))


# -----------------------------------------------------------------------
# ReadoutDiscreteCAEN
# -----------------------------------------------------------------------
@requires_integration
class TestCompileReadoutDiscreteCAEN:
#    @pytest.mark.xfail(reason="ReadoutDiscreteCAEN has a known code-generation issue with mccode-antlr")
    def test_compile_with_count(self):
        _compile_instrument(dedent(f"""
        DEFINE INSTRUMENT test_discrete_compile()
        {CAEN_USERVARS}
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        {CAEN_ORIGIN_EXTEND}
        COMPONENT readout = ReadoutDiscreteCAEN(
          ring="RING", fen="FEN", tube="TUBE",
          event_mode="p", a_name="A", b_name="B", tof="tof",
          ip="127.0.0.1", port=9002, broadcast=0,
          discrete_count=50
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))


# -----------------------------------------------------------------------
# Multi-component instrument
# -----------------------------------------------------------------------
@requires_integration
class TestCompileMultiComponent:
    def test_compile_all_together(self):
        """An instrument with ReadoutCAEN, ReadoutTTLMonitor, and CollectorCAEN."""
        _compile_instrument(dedent(f"""
        DEFINE INSTRUMENT test_multi_compile(string filename="output")
        {CAEN_USERVARS}
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        {CAEN_ORIGIN_EXTEND}
        
        COMPONENT readout = ReadoutCAEN(
          ring="RING", fen="FEN", tube="TUBE",
          event_mode="p", a_name="A", b_name="B", tof="tof",
          ip="127.0.0.1", port=9000, broadcast=0
        ) AT (0, 0, 1) ABSOLUTE
        
        COMPONENT monitor = ReadoutTTLMonitor(
          ring="RING", fen="FEN",
          position="A", identity="TUBE", value="B", tof="tof",
          ip="127.0.0.1", port=9001, broadcast=0
        ) AT (0, 0, 2) ABSOLUTE
        
        COMPONENT collector = CollectorCAEN(
          ring="RING", fen="FEN", tube="TUBE",
          a_name="A", b_name="B", tof="tof",
          filename=filename, verbose=1
        ) AT (0, 0, 3) ABSOLUTE
        
        END
        """))
