"""Run tests for Readout components.

These tests compile AND run short simulations, then check that the expected
side effects occurred (e.g. HDF5 output was created and contains data,
simulation completed without error).
"""
from __future__ import annotations

import os
import pytest
from textwrap import dedent
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
        result, dats = _compile_and_run(dedent(f"""
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
            """))
        assert b"TRACE end" in result


# -----------------------------------------------------------------------
# CollectorCAEN run — produces HDF5 output
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorCAENOutput:
    def test_run_produces_hdf5(self):
        """CollectorCAEN writes an HDF5 file with event data."""
        result, dats = _compile_and_run(dedent(f"""
            DEFINE INSTRUMENT test_collect_run(string filename="output")
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
            """), parameters="-n 100 filename=output")
        assert b"TRACE end" in result
        # The output directory should contain an HDF5 file
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0, "Expected an HDF5 output file"

    def test_hdf5_contains_events(self, tmp_path):
        """The HDF5 file has the expected datasets with correct event count."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(dedent(f"""
            DEFINE INSTRUMENT test_collect_events(string filename="events_test")
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
            """), parameters="-n 50 filename=events_test", directory=str(tmp_path))
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


# -----------------------------------------------------------------------
# CollectorCAEN run — the description-based (star engine) component
# -----------------------------------------------------------------------
@requires_run
class TestRunCollectorCAEN:
    def test_star_component_writes_sendable_layout(self, tmp_path):
        """CollectorCAEN stores records whose compound layout matches the canonical
        CAEN description, the description recorded on the dataset, the detector
        attributes (sendability is decided by the datatype)."""
        h5py = pytest.importorskip("h5py")

        result, dats = _compile_and_run(dedent(f"""
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
            """), parameters="-n 50 filename=star_test", directory=str(tmp_path))
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
            # the description is a dataset attribute; the record layout needs no attributes at all
            assert "description" in ds.attrs
            assert "detector" not in ds.attrs
            assert "readout" not in ds.attrs
            # the detector identity (EFU packet-type byte at replay) is a GROUP attribute
            assert "detector" in group.attrs
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
        result, dats = _compile_and_run(dedent(f"""
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
            """))
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

        result, dats = _compile_and_run(dedent(f"""
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
            """), parameters="-n 50 filename=ttl_test", directory=str(tmp_path))
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
            # the detector identity (EFU packet-type byte at replay) is a GROUP attribute
            assert "detector" in group.attrs
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

        result, dats = _compile_and_run(dedent(f"""
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
            """), parameters="-n 50 filename=cdt_test", directory=str(tmp_path))
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
            # the detector identity (EFU packet-type byte at replay) is a GROUP attribute
            assert "detector" in group.attrs
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

        result, dats = _compile_and_run(dedent(f"""
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
            """), parameters="-n 50 filename=vmm3_test", directory=str(tmp_path))
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
            # the detector identity (EFU packet-type byte at replay) is a GROUP attribute
            assert "detector" in group.attrs
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

        result, dats = _compile_and_run(dedent(f"""
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
            """), parameters="-n 50 filename=bm0_test", directory=str(tmp_path))
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
            # the detector identity (EFU packet-type byte at replay) is a GROUP attribute
            assert "detector" in group.attrs
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

        result, dats = _compile_and_run(dedent(f"""
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
            """), parameters="-n 50 filename=bm2_test", directory=str(tmp_path))
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
            # the detector identity (EFU packet-type byte at replay) is a GROUP attribute
            assert "detector" in group.attrs
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

        result, dats = _compile_and_run(dedent(f"""
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
            """), parameters="-n 50 filename=bmi_test", directory=str(tmp_path))
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
            # the detector identity (EFU packet-type byte at replay) is a GROUP attribute
            assert "detector" in group.attrs
            total = group["weights"][()].sum()
            assert total > 0.0


# -----------------------------------------------------------------------
# Multi-component run
# -----------------------------------------------------------------------
@requires_run
class TestRunMultiComponent:
    def test_readout_and_collect_together(self):
        """An instrument with ReadoutCAEN + CollectorCAEN produces correct output."""
        result, dats = _compile_and_run(dedent(f"""
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
            
            COMPONENT collector = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1
            ) AT (0, 0, 3) ABSOLUTE
            
            END
            """), parameters="-n 100 filename=multi_output")
        assert b"TRACE end" in result
        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0, "Expected HDF5 output from CollectorCAEN"


@requires_run
class TestRunCollectorDiskChopper:
    """The transmission a disc chopper is for, and the declaration it leaves behind."""

    #: A monochromatic pencil beam straight down the axis, so a ray's arrival time at
    #: the disc is fixed and the only thing deciding transmission is the disc itself.
    SOURCE = """
        COMPONENT origin = Progress_bar() AT (0, 0, 0) ABSOLUTE
        COMPONENT source = Source_simple(
          radius=0.001, dist=1, focus_xw=0.002, focus_yh=0.002,
          E0=5, dE=0, flux=1e10
        ) AT (0, 0, 0) ABSOLUTE
    """

    RAYS = 200

    def _run(self, chopper: str, tmp_path):
        return _compile_and_run(dedent(f"""
            DEFINE INSTRUMENT test_chopper(string filename="chopper_test")
            TRACE
            SEARCH SHELL "readout-config --show compdir"
            {self.SOURCE}
            {chopper}
            COMPONENT mon = Monitor_nD(
              xwidth=0.02, yheight=0.02, options="intensity"
            ) AT (0, 0, 2) ABSOLUTE
            END
            """), parameters=f"-n {self.RAYS} filename=chopper_test",
            directory=str(tmp_path))

    @staticmethod
    def _transmitted(dats) -> float:
        """How many rays reached the monitor past the disc."""
        monitor = next(d for name, d in dats.dats.items() if name.startswith("mon"))
        return float(monitor.metadata["values"].split()[2])

    def test_a_parked_disc_passes_the_beam_through_an_opening(self, tmp_path):
        """The case stock DiskChopper cannot express.

        The disc is stationary with its mark at 180 degrees and a slit spanning it, so
        the opening sits squarely on the beam and rays go through.
        """
        result, dats = self._run(dedent("""
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={170, 190}, n_edges=2, radius=0.35, yheight=0.06,
              nu=0, park_angle=180, filename=filename
            ) AT (0, 0, 1) ABSOLUTE
        """), tmp_path)
        assert b"TRACE end" in result
        assert self._transmitted(dats) == self.RAYS

    def test_a_parked_disc_blocks_when_no_opening_faces_the_beam(self, tmp_path):
        """Same disc, turned so the solid part faces the beam. `DiskChopper` with
        `nu=0` would pass everything here, because it substitutes omega=1e-15 and
        falls permanently open."""
        result, dats = self._run(dedent("""
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={170, 190}, n_edges=2, radius=0.35, yheight=0.06,
              nu=0, park_angle=0, filename=filename
            ) AT (0, 0, 1) ABSOLUTE
        """), tmp_path)
        assert b"TRACE end" in result
        assert self._transmitted(dats) == 0

    def test_a_turning_disc_chops_in_time(self, tmp_path):
        """The disc is a clock, not a mask.

        Two runs of one instrument differing only in `delay`, half a rotation apart, so
        the same rays meet the opening in one and the solid disc in the other. Asserted
        without saying which is which, because that depends on the neutron velocity;
        what matters is that the two are opposite, which a disc that did not chop could
        not produce.
        """
        template = """
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={{170, 190}}, n_edges=2, radius=0.35, yheight=0.06,
              nu=14, delay={delay}, filename=filename
            ) AT (0, 0, 1) ABSOLUTE
        """
        counts = []
        for index, delay in enumerate((0.0, 1.0 / (2 * 14))):
            run_dir = tmp_path / f"delay{index}"
            run_dir.mkdir()
            _, dats = self._run(dedent(template.format(delay=delay)), run_dir)
            counts.append(self._transmitted(dats))
        assert sorted(counts) == [0, self.RAYS], counts

    def test_the_default_beam_angle_leaves_the_beam_at_the_top(self, tmp_path):
        """Zero is the DiskChopper convention, so nothing already written changes."""
        _, dats = self._run(dedent("""
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={-10, 10}, n_edges=2, radius=0.35, yheight=0.06,
              nu=0, park_angle=0, beam_angle=0, filename=filename
            ) AT (0, 0, 1) ABSOLUTE
        """), tmp_path)
        assert self._transmitted(dats) == self.RAYS

    def test_a_beam_angle_moves_the_beam_round_the_disc(self, tmp_path):
        """A disc hanging above its beam: the opening is at 180, and so is the beam.

        With `beam_angle` the caller no longer turns the whole component about its own z
        to bring that part of the disc to the top -- which is the only thing
        `DiskChopper` could express.
        """
        _, dats = self._run(dedent("""
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={170, 190}, n_edges=2, radius=0.35, yheight=0.06,
              nu=0, park_angle=0, beam_angle=180, filename=filename
            ) AT (0, 0, 1) ABSOLUTE
        """), tmp_path)
        assert self._transmitted(dats) == self.RAYS

    def test_a_beam_angle_is_a_shift_of_the_openings(self, tmp_path):
        """What fixes the sense, rather than leaving it to be discovered.

        `beam_angle` enters in the same sense as `slit_edges`, so moving the beam round
        by B is the same disc as moving every edge back by B. Two instruments that must
        agree ray for ray.
        """
        shapes = {
            # the beam moved round to the opening
            "shifted_beam": ("slit_edges={100, 140}, n_edges=2, beam_angle=120", self.RAYS),
            # the opening moved round to the beam: the same disc, said the other way
            "shifted_edges": ("slit_edges={-20, 20}, n_edges=2, beam_angle=0", self.RAYS),
            # and the other direction, which is what fails if the sign is inverted
            "wrong_way": ("slit_edges={100, 140}, n_edges=2, beam_angle=-120", 0),
        }
        for label, (spec, expected) in shapes.items():
            run_dir = tmp_path / label
            run_dir.mkdir()
            _, dats = self._run(dedent(f"""
                COMPONENT chopper = CollectorDiskChopper(
                  {spec}, radius=0.35, yheight=0.06,
                  nu=0, park_angle=0, filename=filename
                ) AT (0, 0, 1) ABSOLUTE
            """), run_dir)
            assert self._transmitted(dats) == expected, label

    def test_the_spindle_lies_where_the_angles_put_it(self, tmp_path):
        """Which side of the beam the disc hangs on, tested by what it absorbs.

        An on-axis pencil beam cannot see this -- it is the same distance from a spindle
        above as from one below -- so the beam is displaced in +y and `abs_out` is off,
        which makes the two sides behave oppositely: a ray beyond the rim passes, and
        one inside the solid middle does not.

        With the spindle below (`beam_angle=0`) the displaced rays are past the rim and
        get through; with the disc hanging above the beam (`beam_angle=180`) the same
        rays are inside the hub and are absorbed. A component that assumed the spindle
        was always below would pass both.

        The beam has width, so it straddles the rim and only some of it clears -- the
        claim is that one side transmits and the other does not, not a precise fraction.
        """
        template = """
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={{0, 359}}, n_edges=2, radius=0.35, yheight=0.06,
              nu=0, park_angle=0, beam_angle={beam_angle}, abs_out=0,
              filename=filename
            ) AT (0, -0.04, 1) ABSOLUTE
        """
        outcomes = {}
        for label, beam_angle in (("below", 0), ("above", 180)):
            run_dir = tmp_path / label
            run_dir.mkdir()
            _, dats = self._run(dedent(template.format(beam_angle=beam_angle)), run_dir)
            outcomes[label] = self._transmitted(dats)
        assert outcomes["above"] == 0, outcomes
        assert outcomes["below"] > self.RAYS / 4, outcomes

    def test_zero_angle_does_not_change_what_passes(self, tmp_path):
        """It moves the disc, not the openings.

        Whether a neutron passes is decided on the disc, in the mark's own frame, and
        `zero_angle` says nothing about that -- it only rotates the pickup, and with it
        the spindle, about the component's own axis.
        """
        template = """
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={{170, 190}}, n_edges=2, radius=0.35, yheight=0.06,
              nu=0, park_angle=180, beam_angle=0, zero_angle={zero_angle},
              filename=filename
            ) AT (0, 0, 1) ABSOLUTE
        """
        for label, zero_angle in (("aligned", 0), ("turned", 90)):
            run_dir = tmp_path / label
            run_dir.mkdir()
            _, dats = self._run(dedent(template.format(zero_angle=zero_angle)), run_dir)
            assert self._transmitted(dats) == self.RAYS, label

    def test_a_disc_declares_its_top_dead_centre_channel(self, tmp_path):
        """What makes the chopper discoverable at replay."""
        h5py = pytest.importorskip("h5py")
        result, dats = self._run(dedent("""
            COMPONENT chopper = CollectorDiskChopper(
              slit_edges={10, 350}, n_edges=2, radius=0.35, yheight=0.06,
              nu=14, delay=0, tdc_pv="BIFRO-ChpSy1:Chop-PSC-101:00-TS-I",
              filename=filename
            ) AT (0, 0, 1) ABSOLUTE
        """), tmp_path)
        assert b"TRACE end" in result

        from pathlib import Path
        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert h5_files, "expected an HDF5 output file"
        with h5py.File(h5_files[0], "r") as f:
            assert "chopper" in f, list(f)
            parameters = f["parameters"]
            key = "chopper_chopper_tdc"
            assert key in parameters, list(parameters)
            value = parameters[key][0]
            assert (value.decode() if isinstance(value, bytes) else value) \
                == "BIFRO-ChpSy1:Chop-PSC-101:00-TS-I"
