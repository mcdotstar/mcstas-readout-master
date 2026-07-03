"""MPI tests for CollectorCAEN.

These tests compile and run CollectorCAEN instruments with MPI enabled,
verifying that event data from multiple ranks is correctly merged into a
single HDF5 output file.

Requirements beyond the standard integration tests:
  - An MPI C compiler (mpicc) on PATH
  - mpirun on PATH
  - e.g. ``module load mpi/openmpi-x86_64`` on Fedora/RHEL
"""
from __future__ import annotations

import os
import pytest
from pathlib import Path
from conftest import (
    SHARE_READOUT,
    _build_env,
    CAEN_USERVARS,
    CAEN_ORIGIN_EXTEND,
)

# ---------------------------------------------------------------------------
# Skip / gate helpers
# ---------------------------------------------------------------------------
try:
    from mccode_antlr.test import mpi_compiled_test
    _have_mccode = True
except ImportError:
    _have_mccode = False

    def mpi_compiled_test(method):
        """Fallback: skip when mccode-antlr is not installed."""
        return pytest.mark.skip(reason="mccode-antlr not installed")(method)


def _readout_config_works() -> bool:
    import subprocess
    try:
        env = _build_env()
        result = subprocess.run(
            ["readout-config", "--show", "compdir"],
            capture_output=True, text=True, env=env, timeout=5,
        )
        return result.returncode == 0 and len(result.stdout.strip()) > 0
    except Exception:
        return False


_skip_no_readout = pytest.mark.skipif(
    not (_have_mccode and _readout_config_works()),
    reason="MPI tests require mccode-antlr and a working readout-config",
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _mpi_compile_and_run(
    instr_source: str,
    parameters: str = "-n 100",
    nranks: int = 2,
    directory: str | None = None,
):
    """Compile with ``mpicc`` and run via ``mpirun -np nranks``."""
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

        target_dict = {"mpi": True, "count": nranks}

        if directory is not None:
            binary, target = mccode_compile(
                instr, directory, flavor=Flavor.MCSTAS,
                target=target_dict, dump_source=True,
            )
            output_dir = Path(directory) / "t"
            return mccode_run_compiled(binary, target, output_dir, parameters)
        else:
            with TemporaryDirectory() as tmpdir:
                binary, target = mccode_compile(
                    instr, tmpdir, flavor=Flavor.MCSTAS,
                    target=target_dict, dump_source=True,
                )
                output_dir = Path(tmpdir) / "t"
                return mccode_run_compiled(binary, target, output_dir, parameters)
    finally:
        os.environ["PATH"] = old_path
        os.environ["LD_LIBRARY_PATH"] = old_ld


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
@_skip_no_readout
class TestMPICollectorCAEN:
    """CollectorCAEN tests compiled and run under MPI."""

    @mpi_compiled_test
    def test_mpi_collect_runs(self):
        """CollectorCAEN compiles with mpicc and runs under mpirun without error."""
        result, dats = _mpi_compile_and_run(f"""
            DEFINE INSTRUMENT test_mpi_collect(string filename="mpi_output")
            {CAEN_USERVARS}
            TRACE
            SEARCH SHELL "readout-config --show compdir"
            {CAEN_ORIGIN_EXTEND}
            COMPONENT collector = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1
            ) AT (0, 0, 1) ABSOLUTE
            END
        """, parameters="-n 100 filename=mpi_output", nranks=2)
        assert b"TRACE end" in result

    @mpi_compiled_test
    def test_mpi_collect_produces_hdf5(self, tmp_path):
        """MPI run produces an HDF5 file containing events from all ranks."""
        result, dats = _mpi_compile_and_run(f"""
            DEFINE INSTRUMENT test_mpi_collect_h5(string filename="mpi_events")
            {CAEN_USERVARS}
            TRACE
            SEARCH SHELL "readout-config --show compdir"
            {CAEN_ORIGIN_EXTEND}
            COMPONENT collector = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1
            ) AT (0, 0, 1) ABSOLUTE
            END
        """, parameters="-n 100 filename=mpi_events", nranks=2, directory=str(tmp_path))
        assert b"TRACE end" in result

        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0, "Expected HDF5 output from CollectorCAEN"
        assert Path(h5_files[0]).exists()

    @mpi_compiled_test
    def test_mpi_collect_event_count(self, tmp_path):
        """With 2 ranks × 50 neutrons each, HDF5 should contain 100 events."""
        h5py = pytest.importorskip("h5py")

        result, dats = _mpi_compile_and_run(f"""
            DEFINE INSTRUMENT test_mpi_count(string filename="mpi_count")
            {CAEN_USERVARS}
            TRACE
            SEARCH SHELL "readout-config --show compdir"
            {CAEN_ORIGIN_EXTEND}
            COMPONENT collector = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1
            ) AT (0, 0, 1) ABSOLUTE
            END
        """, parameters="-n 100 filename=mpi_count", nranks=2, directory=str(tmp_path))
        assert b"TRACE end" in result

        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0
        h5_path = h5_files[0]
        assert Path(h5_path).exists()

        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f, f"Missing 'collector' group; keys: {list(f.keys())}"
            group = f["collector"]
            assert "readouts" in group, f"Missing 'readouts' dataset; keys: {list(group.keys())}"
            ds = group["readouts"]
            # -n 100 is split across 2 ranks (50 each), all events collected
            assert ds.shape[0] == 100, f"Expected 100 events, got {ds.shape[0]}"

    @mpi_compiled_test
    def test_mpi_collect_four_ranks(self, tmp_path):
        """CollectorCAEN works correctly with 4 MPI ranks."""
        h5py = pytest.importorskip("h5py")

        result, dats = _mpi_compile_and_run(f"""
            DEFINE INSTRUMENT test_mpi_4ranks(string filename="mpi_4ranks")
            {CAEN_USERVARS}
            TRACE
            SEARCH SHELL "readout-config --show compdir"
            {CAEN_ORIGIN_EXTEND}
            COMPONENT collector = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1
            ) AT (0, 0, 1) ABSOLUTE
            END
        """, parameters="-n 100 filename=mpi_4ranks", nranks=4, directory=str(tmp_path))
        assert b"TRACE end" in result

        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0
        h5_path = h5_files[0]
        assert Path(h5_path).exists()

        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f
            ds = f["collector"]["readouts"]
            # 100 neutrons split across 4 ranks, all collected
            assert ds.shape[0] == 100, f"Expected 100 events, got {ds.shape[0]}"
            names = ds.dtype.names
            for col in ("ring", "FEN", "time", "weight", "channel", "a", "b"):
                assert col in names, f"Missing column '{col}' in dataset"

    @mpi_compiled_test
    def test_mpi_collect_with_readout(self, tmp_path):
        """Multi-component instrument: ReadoutCAEN + CollectorCAEN under MPI."""
        h5py = pytest.importorskip("h5py")

        result, dats = _mpi_compile_and_run(f"""
            DEFINE INSTRUMENT test_mpi_multi(string filename="mpi_multi")
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
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1
            ) AT (0, 0, 2) ABSOLUTE
            END
        """, parameters="-n 100 filename=mpi_multi", nranks=2, directory=str(tmp_path))
        assert b"TRACE end" in result

        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0
        h5_path = h5_files[0]
        assert Path(h5_path).exists()

        with h5py.File(str(h5_path), "r") as f:
            assert "collector" in f
            assert f["collector"]["readouts"].shape[0] == 100


    @mpi_compiled_test
    def test_multi_collectors(self, tmp_path):
        """Multi-component instrument: 4x CollectorCAEN under MPI."""
        h5py = pytest.importorskip("h5py")

        total_rays = 1000

        result, dats = _mpi_compile_and_run(f"""
            DEFINE INSTRUMENT test_multi_collectors(string filename="mpi_multi")
            {CAEN_USERVARS}
            TRACE
            SEARCH SHELL "readout-config --show compdir"
            {CAEN_ORIGIN_EXTEND}
            
            COMPONENT xswitch = Arm() AT (0, 0, 0) ABSOLUTE EXTEND %{{
            x = rand01() - 0.5;
            y = rand01() - 0.5;
            %}}
            
            COMPONENT profile = PSD_monitor(
              nx = 11,
              ny = 11,
              filename = "profile.dat",
              xwidth = 2.,
              yheight = 2.) 
             AT (0, 0, 1) ABSOLUTE
            
            COMPONENT collector_mm = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1, dataset_name="collector--"
            ) WHEN (x < 0 && y < 0) AT (0, 0, 2) ABSOLUTE 
            COMPONENT collector_mp = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1, dataset_name="collector-+"
            ) WHEN (x < 0 && y >= 0) AT (0, 0, 2) ABSOLUTE 
            COMPONENT collector_pm = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1, dataset_name="collector+-"
            ) WHEN (x >= 0 && y < 0) AT (0, 0, 2) ABSOLUTE 
            COMPONENT collector_pp = CollectorCAEN(
              ring="RING", fen="FEN", tube="TUBE",
              event_mode="p", a_name="A", b_name="B", tof="tof",
              filename=filename, verbose=1, dataset_name="collector++"
            ) WHEN (x >= 0 && y >= 0) AT (0, 0, 2) ABSOLUTE 
            
            END
        """, parameters=f"-n {total_rays} filename=mpi_multi", nranks=2, directory=str(tmp_path))
        assert b"TRACE end" in result

        h5_files = [f for f in dats.unrecognized if Path(f).suffix == ".h5"]
        assert len(h5_files) > 0
        h5_path = h5_files[0]
        assert Path(h5_path).exists()

        def is_present_count(name, container):
            assert name in container
            assert "readouts" in container[name]
            return container[name]["readouts"].shape[0]


        with h5py.File(str(h5_path), "r") as f:
            mm = is_present_count('collector--', f)
            mp = is_present_count('collector-+', f)
            pm = is_present_count('collector+-', f)
            pp = is_present_count('collector++', f)
            assert mm + mp + pm + pp == total_rays
