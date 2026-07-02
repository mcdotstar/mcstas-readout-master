"""Run tests for Readout components.

These tests compile AND run short simulations, then check that the expected
side effects occurred (e.g. HDF5 output was created and contains data,
simulation completed without error).
"""
from __future__ import annotations

import os
import pytest
from conftest import (
    SHARE_READOUT,
    requires_run,
    _build_env,
    CAEN_USERVARS,
    CAEN_ORIGIN_EXTEND,
    TTL_USERVARS,
    TTL_ORIGIN_EXTEND,
)


def _compile_and_run(instr_source: str, parameters: str = "-n 100", directory: str | None = None):
    """Parse, compile, and run an instrument.

    Returns (stdout_bytes, SimulationOutput).

    If *directory* is provided the build and run artefacts are placed there
    and survive after this function returns (so callers can inspect HDF5
    files, etc.).  When *directory* is ``None`` a temporary directory is used
    that is cleaned up automatically—file paths in the returned
    ``SimulationOutput`` will no longer be valid after the call.
    """
    from pathlib import Path
    from tempfile import TemporaryDirectory

    from mccode_antlr.loader import parse_mcstas_instr
    from mccode_antlr.reader.registry import registry_from_specification
    from mccode_antlr.run import mccode_compile, mccode_run_compiled
    from mccode_antlr.utils import Flavor

    old_path = os.environ.get("PATH", "")
    old_ld = os.environ.get("LD_LIBRARY_PATH", "")
    env = _build_env()
    try:
        os.environ["PATH"] = env["PATH"]
        os.environ["LD_LIBRARY_PATH"] = env.get("LD_LIBRARY_PATH", "")
        reg = registry_from_specification(str(SHARE_READOUT))
        instr = parse_mcstas_instr(instr_source, registries=[reg])

        if directory is not None:
            binary, target = mccode_compile(instr, directory, flavor=Flavor.MCSTAS, dump_source=True)
            output_dir = Path(directory) / "t"
            return mccode_run_compiled(binary, target, output_dir, parameters)
        else:
            # Disposable temp directory — files removed on return.
            with TemporaryDirectory() as tmpdir:
                binary, target = mccode_compile(instr, tmpdir, flavor=Flavor.MCSTAS, dump_source=True)
                output_dir = Path(tmpdir) / "t"
                return mccode_run_compiled(binary, target, output_dir, parameters)
    finally:
        os.environ["PATH"] = old_path
        os.environ["LD_LIBRARY_PATH"] = old_ld


# -----------------------------------------------------------------------
# ReadoutCAEN run
# -----------------------------------------------------------------------
@requires_run
class TestRunReadoutCAEN:
    def test_run_broadcast_off(self):
        """ReadoutCAEN with broadcast=0 runs without error."""
        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_readout_caen_run()
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
""")
        assert b"TRACE end" in result


# -----------------------------------------------------------------------
# CollectCAEN run — produces HDF5 output
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectCAEN:
    def test_run_produces_hdf5(self):
        """CollectCAEN writes an HDF5 file with event data."""
        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collect_run(string filename="output")
{CAEN_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{CAEN_ORIGIN_EXTEND}
COMPONENT collector = CollectCAEN(
  ring="RING", fen="FEN", tube="TUBE",
  a_name="A", b_name="B", tof="tof",
  filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 100 filename=output")
        assert b"TRACE end" in result
        # The output directory should contain an HDF5 file
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0, "Expected an HDF5 output file"

    def test_hdf5_contains_events(self, tmp_path):
        """The HDF5 file has the expected datasets with correct event count."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collect_events(string filename="events_test")
{CAEN_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{CAEN_ORIGIN_EXTEND}
COMPONENT collector = CollectCAEN(
  ring="RING", fen="FEN", tube="TUBE",
  a_name="A", b_name="B", tof="tof",
  filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=events_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            assert "readouts" in group, f"Missing 'readouts' dataset; keys: {list(group.keys())}"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 events, got {ds.shape[0]}"
            # Verify expected columns exist in the compound dtype
            names = ds.dtype.names
            for col in ("ring", "FEN", "time", "weight", "channel", "a", "b"):
                assert col in names, f"Missing column '{col}' in dataset"

    def test_collect_with_points(self, tmp_path):
        """CollectCAEN with total_points creates point-based groups."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collect_points(string filename="points_test", int point=0, int total_points=1)
{CAEN_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{CAEN_ORIGIN_EXTEND}
COMPONENT collector = CollectCAEN(
  ring="RING", fen="FEN", tube="TUBE",
  a_name="A", b_name="B", tof="tof",
  filename=filename, point=point, total_points=total_points, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=points_test point=0 total_points=1", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing collector group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group; keys: {list(group.keys())}"


# -----------------------------------------------------------------------
# ReadoutTTLMonitor run
# -----------------------------------------------------------------------
@requires_run
class TestRunReadoutTTLMonitor:
    def test_run_broadcast_off(self):
        """ReadoutTTLMonitor with broadcast=0 runs without error."""
        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_ttl_run()
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
""")
        assert b"TRACE end" in result


# -----------------------------------------------------------------------
# Multi-component run
# -----------------------------------------------------------------------
@requires_run
class TestRunMultiComponent:
    def test_readout_and_collect_together(self):
        """An instrument with ReadoutCAEN + CollectCAEN produces correct output."""
        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_multi_run(string filename="multi_output")
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

COMPONENT collector = CollectCAEN(
  ring="RING", fen="FEN", tube="TUBE",
  a_name="A", b_name="B", tof="tof",
  filename=filename, verbose=1
) AT (0, 0, 3) ABSOLUTE

END
""", parameters="-n 100 filename=multi_output")
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0, "Expected HDF5 output from CollectCAEN"
