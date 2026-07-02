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
    CDT_USERVARS,
    CDT_ORIGIN_EXTEND,
    VMM3_USERVARS,
    VMM3_ORIGIN_EXTEND,
    BM0_USERVARS,
    BM0_ORIGIN_EXTEND,
    BM2_USERVARS,
    BM2_ORIGIN_EXTEND,
    BMI_USERVARS,
    BMI_ORIGIN_EXTEND,
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
# CollectorCAEN run — the description-based (star engine) component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorCAEN:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorCAEN stores records whose compound layout matches the canonical
        CAEN description, with the description recorded and no detector/readout
        attributes (sendability is decided by the datatype)."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collector_star(string filename="star_test")
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
""", parameters="-n 50 filename=star_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 records, got {ds.shape[0]}"
            # canonical CAEN layout: exact member names and C-struct size
            assert ds.dtype.names == ("ring", "FEN", "time", "weight", "channel", "a", "b", "c", "d")
            assert ds.dtype.itemsize == 40, f"Expected itemsize 40, got {ds.dtype.itemsize}"
            # the description string is recorded; detector/readout attributes are not written
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            # per-record weights were stored and accumulated into the point weight
            total = group["weights"][()].sum()
            assert total > 0.0


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
# -----------------------------------------------------------------------
# CollectorTTLMonitor run — description-based TTLMonitor component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorTTLMonitor:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorTTLMonitor stores records with canonical TTLMonitor layout."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collector_ttl(string filename="ttl_test")
{TTL_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{TTL_ORIGIN_EXTEND}
COMPONENT collector = CollectorTTLMonitor(
  ring="RING", fen="FEN",
  position="A", identity="TUBE", value="B", tof="tof",
  filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=ttl_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 records, got {ds.shape[0]}"
            assert ds.dtype.names == ("ring", "FEN", "time", "weight", "channel", "pos", "adc")
            assert ds.dtype.itemsize == 32, f"Expected itemsize 32, got {ds.dtype.itemsize}"
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            total = group["weights"][()].sum()
            assert total > 0.0


# -----------------------------------------------------------------------
# CollectorCDT run — description-based CDT component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorCDT:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorCDT stores records with canonical CDT layout."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collector_cdt(string filename="cdt_test")
{CDT_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{CDT_ORIGIN_EXTEND}
COMPONENT collector = CollectorCDT(
  ring="RING", fen="FEN",
  om_name="OM", cathode_name="CATHODE", anode_name="ANODE", tof="tof",
  filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=cdt_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 records, got {ds.shape[0]}"
            assert ds.dtype.names == ("ring", "FEN", "time", "weight", "om", "cathode", "anode")
            assert ds.dtype.itemsize == 32, f"Expected itemsize 32, got {ds.dtype.itemsize}"
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            total = group["weights"][()].sum()
            assert total > 0.0


# -----------------------------------------------------------------------
# CollectorVMM3 run — description-based VMM3 component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorVMM3:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorVMM3 stores records with canonical VMM3 layout."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collector_vmm3(string filename="vmm3_test")
{VMM3_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{VMM3_ORIGIN_EXTEND}
COMPONENT collector = CollectorVMM3(
  ring="RING", fen="FEN",
  bc_name="BC", otadc_name="OTADC", geo_name="GEO",
  tdc_name="TDC", vmm_name="VMM", channel_name="CHANNEL",
  tof="tof", filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=vmm3_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 records, got {ds.shape[0]}"
            assert ds.dtype.names == ("ring", "FEN", "time", "weight", "bc", "otadc", "geo", "tdc", "vmm", "channel")
            assert ds.dtype.itemsize == 32, f"Expected itemsize 32, got {ds.dtype.itemsize}"
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            total = group["weights"][()].sum()
            assert total > 0.0


# -----------------------------------------------------------------------
# CollectorBM0 run — description-based BM0 component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorBM0:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorBM0 stores records with canonical BM0 layout."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collector_bm0(string filename="bm0_test")
{BM0_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{BM0_ORIGIN_EXTEND}
COMPONENT collector = CollectorBM0(
  ring="RING", fen="FEN",
  channel_name="CHANNEL", tof="tof",
  filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=bm0_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 records, got {ds.shape[0]}"
            assert ds.dtype.names == ("ring", "FEN", "time", "weight", "channel")
            assert ds.dtype.itemsize == 32, f"Expected itemsize 32, got {ds.dtype.itemsize}"
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            total = group["weights"][()].sum()
            assert total > 0.0


# -----------------------------------------------------------------------
# CollectorBM2 run — description-based BM2 component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorBM2:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorBM2 stores records with canonical BM2 layout."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collector_bm2(string filename="bm2_test")
{BM2_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{BM2_ORIGIN_EXTEND}
COMPONENT collector = CollectorBM2(
  ring="RING", fen="FEN",
  channel_name="CHANNEL", pos_x_name="POSX", pos_y_name="POSY",
  tof="tof", filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=bm2_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 records, got {ds.shape[0]}"
            assert ds.dtype.names == ("ring", "FEN", "time", "weight", "channel", "pos_x", "pos_y")
            assert ds.dtype.itemsize == 32, f"Expected itemsize 32, got {ds.dtype.itemsize}"
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            total = group["weights"][()].sum()
            assert total > 0.0


# -----------------------------------------------------------------------
# CollectorBMI run — description-based BMI component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorBMI:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorBMI stores records with canonical BMI layout."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(f"""
DEFINE INSTRUMENT test_collector_bmi(string filename="bmi_test")
{BMI_USERVARS}
TRACE
SEARCH SHELL "readout-config --show compdir"
{BMI_ORIGIN_EXTEND}
COMPONENT collector = CollectorBMI(
  ring="RING", fen="FEN",
  channel_name="CHANNEL", sum_name="SUM", adc_name="ADC",
  tof="tof", filename=filename, verbose=1
) AT (0, 0, 1) ABSOLUTE
END
""", parameters="-n 50 filename=bmi_test", directory=str(tmp_path))
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0

        h5_path = h5_files[0]
        assert Path(h5_path).exists(), f"HDF5 file not found: {h5_path}"
        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            for required in ("readouts", "cues", "weights", "normalizations"):
                assert required in group, f"Missing '{required}' in collector group"
            ds = group["readouts"]
            assert ds.shape[0] == 50, f"Expected 50 records, got {ds.shape[0]}"
            assert ds.dtype.names == ("ring", "FEN", "time", "weight", "channel", "sum", "adc")
            assert ds.dtype.itemsize == 32, f"Expected itemsize 32, got {ds.dtype.itemsize}"
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            total = group["weights"][()].sum()
            assert total > 0.0


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
