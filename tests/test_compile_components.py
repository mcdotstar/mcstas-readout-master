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
        """An instrument with ReadoutCAEN and CollectorCAEN."""
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
        
        COMPONENT collector = CollectorCAEN(
          ring="RING", fen="FEN", tube="TUBE",
          a_name="A", b_name="B", tof="tof",
          filename=filename, verbose=1
        ) AT (0, 0, 3) ABSOLUTE
        
        END
        """))


class TestCompileCollectorDiskChopper:
    def test_compile_turning(self):
        _compile_instrument(dedent("""
        DEFINE INSTRUMENT test_collector_disk_chopper_turning(string filename="output")
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        COMPONENT origin = Progress_bar() AT (0, 0, 0) ABSOLUTE
        COMPONENT chopper = CollectorDiskChopper(
          slit_edges={10, 30, 100, 140}, n_edges=4,
          radius=0.35, yheight=0.06, nu=14, delay=0,
          tdc_pv="BIFRO-ChpSy1:Chop-PSC-101:00-TS-I",
          filename=filename
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))

    def test_compile_parked(self):
        """A stationary disc is a supported mode, not a degenerate one."""
        _compile_instrument(dedent("""
        DEFINE INSTRUMENT test_collector_disk_chopper_parked(string filename="output")
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        COMPONENT origin = Progress_bar() AT (0, 0, 0) ABSOLUTE
        COMPONENT chopper = CollectorDiskChopper(
          slit_edges={170, 190}, n_edges=2,
          radius=0.35, yheight=0.06, nu=0, park_angle=180,
          filename=filename
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))

    def test_compile_with_a_beam_angle(self):
        """A disc hanging above its beam, placed without turning the component."""
        _compile_instrument(dedent("""
        DEFINE INSTRUMENT test_collector_disk_chopper_beam_angle(string filename="output")
        TRACE
        SEARCH SHELL "readout-config --show compdir"
        COMPONENT origin = Progress_bar() AT (0, 0, 0) ABSOLUTE
        COMPONENT chopper = CollectorDiskChopper(
          slit_edges={95, 265}, n_edges=2,
          radius=0.35, yheight=0.054, nu=14, delay=0, beam_angle=180,
          filename=filename
        ) AT (0, 0, 1) ABSOLUTE
        END
        """))
