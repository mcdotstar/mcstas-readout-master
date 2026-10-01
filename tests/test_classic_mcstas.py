"""Collector components with the classic McStas code generator (mcstas/mcrun).

These tests compile and run instruments with the classic McStas toolchain and check
both record sinks:

  * sink "hdf5"  -- the cue-based collector file (the default without NeXus output),
  * sink "nexus" -- records written into the McStas NeXus output file of the run
                    (``mcrun --format=NeXus``), then converted with
                    ``readout-combine import`` into a collector file.

They also check that the stored rate (weight / normalization) does not depend on
the number of MPI processes.

Requirements (tests are skipped when missing):
  - classic McStas: ``mcrun`` and ``mcstas`` on PATH (McStas 3.x with SEARCH SHELL)
  - h5py
  - NeXus support in McStas for the NeXus tests, mpirun for the MPI tests
  - readout-config / readout-combine from this project's build (see conftest.py)
"""
from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

from conftest import BUILD_DIR, REPO_ROOT, _build_env, _readout_config_works

h5py = pytest.importorskip("h5py")
np = pytest.importorskip("numpy")

EXAMPLE = REPO_ROOT / "examples" / "readout_example.instr"


def _classic_mcstas() -> bool:
    if shutil.which("mcrun") is None or shutil.which("mcstas") is None:
        return False
    try:
        out = subprocess.run(["mcstas", "--version"], capture_output=True, text=True, timeout=30)
        return "mcstas" in (out.stdout + out.stderr).lower()
    except Exception:
        return False


requires_classic = pytest.mark.skipif(
    not (_classic_mcstas() and _readout_config_works()),
    reason="needs classic McStas (mcstas, mcrun) and a working readout-config",
)
requires_mpi = pytest.mark.skipif(shutil.which("mpirun") is None, reason="needs mpirun")


def mcrun(instr: Path, workdir: Path, outdir: str, ncount: float, fmt: str = "McCode",
          mpi: int = 0, seed: int = 1234, params: tuple[str, ...] = ()) -> Path:
    env = _build_env()
    env.setdefault("OMPI_MCA_rmaps_base_oversubscribe", "1")
    cmd = ["mcrun", "-c", "-y", str(instr.name), f"--ncount={int(ncount)}", f"--dir={outdir}",
           f"--format={fmt}", f"--seed={seed}", *params]
    if mpi:
        cmd.append(f"--mpi={mpi}")
    result = subprocess.run(cmd, cwd=workdir, env=env, capture_output=True, text=True, timeout=900)
    if result.returncode != 0:
        if fmt == "NeXus" and ("napi.h" in result.stdout + result.stderr or "NeXus" in result.stderr):
            pytest.skip("McStas is not able to build with NeXus here")
        raise AssertionError(f"mcrun failed:\n{result.stdout[-4000:]}\n{result.stderr[-4000:]}")
    return workdir / outdir


def readout_combine(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["readout-combine", *args], env=_build_env(), capture_output=True, text=True, timeout=300)


def collector_rate(path: Path, group: str) -> tuple[int, int, float]:
    with h5py.File(path, "r") as f:
        g = f[group]
        norm = int(g["normalizations"][0])
        return g["readouts"].shape[0], norm, float(g["weights"][0]) / norm


def nexus_readout_group(path: Path, component: str, group: str):
    f = h5py.File(path, "r")
    comps = f["entry1/instrument/components"]
    name = next(k for k in comps if k.split("_", 1)[1] == component)
    return f, comps[name][group]


@pytest.fixture
def example_dir(tmp_path):
    shutil.copy(EXAMPLE, tmp_path / EXAMPLE.name)
    return tmp_path


@requires_classic
def test_classic_hdf5_sink(example_dir):
    out = mcrun(EXAMPLE, example_dir, "run_hdf5", 2e4)
    h5 = out / "example.h5"
    assert h5.exists()
    rows, norm, rate = collector_rate(h5, "detector")
    assert norm == 20000
    assert rows > 0
    # the collector's 0D monitor equals the McStas monitor at the same plane
    sim = (out / "mccode.sim").read_text()
    assert "component: collect_detector" in sim
    assert readout_combine("validate", str(h5)).returncode == 0


@requires_classic
def test_classic_nexus_sink_matches_hdf5_sink(example_dir):
    """Same seed, same rays: the NeXus sink stores exactly what the HDF5 sink stores."""
    hdf5_out = mcrun(EXAMPLE, example_dir, "run_hdf5", 2e4)
    nexus_out = mcrun(EXAMPLE, example_dir, "run_nexus", 2e4, fmt="NeXus")
    nexus = nexus_out / "mccode.h5"
    assert nexus.exists()
    # no separate collector file with the NeXus sink
    assert not (nexus_out / "example.h5").exists()

    f, g = nexus_readout_group(nexus, "collect_detector", "detector")
    with f:
        assert g.attrs["type"] == b"Readouts"
        assert int(g.attrs["ess_type"]) == 52
        assert int(g.attrs["normalization"]) == 20000
        assert set(g.keys()) == {"ring", "FEN", "time", "weight", "channel", "a", "b", "c", "d"}
        assert g["ring"].dtype == np.uint8 and g["a"].dtype == np.uint16 and g["weight"].dtype == np.float64

    imported = example_dir / "imported.h5"
    result = readout_combine("import", str(nexus), "-o", str(imported))
    assert result.returncode == 0, result.stderr
    assert readout_combine("validate", str(imported)).returncode == 0

    with h5py.File(hdf5_out / "example.h5", "r") as a, h5py.File(imported, "r") as b:
        for group in ("detector", "monitor"):
            ra, rb = a[f"{group}/readouts"][()], b[f"{group}/readouts"][()]
            assert ra.dtype == rb.dtype  # exact layout: EFU-sendable
            assert np.array_equal(ra, rb)
            assert a[f"{group}/normalizations"][0] == b[f"{group}/normalizations"][0]
            assert a[f"{group}"].attrs["detector"] == b[f"{group}"].attrs["detector"]
        assert set(a["parameters"].keys()) == set(b["parameters"].keys())
        assert a["parameters/lambda"][0] == b["parameters/lambda"][0]


@requires_classic
@requires_mpi
@pytest.mark.parametrize("fmt", ["McCode", "NeXus"])
def test_classic_mpi_rate_independent_of_node_count(example_dir, fmt):
    """weight / normalization is the rate whatever the number of MPI processes."""
    rates = {}
    for nodes in (0, 2, 3):
        out = mcrun(EXAMPLE, example_dir, f"run_{fmt}_{nodes}", 6e4, fmt=fmt, mpi=nodes)
        if fmt == "NeXus":
            h5 = example_dir / f"imported_{nodes}.h5"
            assert readout_combine("import", str(out / "mccode.h5"), "-o", str(h5)).returncode == 0
        else:
            h5 = out / "example.h5"
        rows, norm, rate = collector_rate(h5, "monitor")
        assert norm == 60000
        rates[nodes] = rate
    # every ray reaches the beam monitor: the rate is the same up to rounding of
    # the ray count over the nodes
    for nodes, rate in rates.items():
        assert rate == pytest.approx(rates[0], rel=1e-3), rates


ALL_COLLECTORS = r"""
DEFINE INSTRUMENT readout_all_collectors(string filename="all")
USERVARS %{
  int ring_id;
  int fen_id;
  int tube_id;
  int amp_a;
  int amp_b;
  int om;
  int cathode;
  int anode;
  int bc;
  int otadc;
  int geo;
  int tdc;
  int vmm;
  int chan;
  int posx;
  int posy;
  int sum;
  int adc;
%}
TRACE
SEARCH SHELL "readout-config --show compdir"
COMPONENT origin = Progress_bar() AT (0, 0, 0) ABSOLUTE
COMPONENT source = Source_simple(radius=0.01, dist=1, focus_xw=0.01, focus_yh=0.01, lambda0=4, dlambda=1)
AT (0, 0, 0) RELATIVE origin
COMPONENT values = Arm() AT (0, 0, 0.5) RELATIVE source
EXTEND %{
  ring_id = 1; fen_id = 0; tube_id = 2; amp_a = 100; amp_b = 200;
  om = 1; cathode = 2; anode = 3;
  bc = 1; otadc = 2; geo = 3; tdc = 4; vmm = 5; chan = 6;
  posx = 7; posy = 8; sum = 9; adc = 10;
%}
COMPONENT chopper = CollectorDiskChopper(slit_edges={-10, 10}, n_edges=2, nu=0, radius=0.5, yheight=0.1,
  tdc_pv="TEST:CHOPPER:TDC", filename=filename)
AT (0, 0, 0.6) RELATIVE source
COMPONENT caen = CollectorCAEN(ring="ring_id", fen="fen_id", tube="tube_id", a_name="amp_a", b_name="amp_b",
  filename=filename) AT (0, 0, 1) RELATIVE source
COMPONENT cdt = CollectorCDT(ring="ring_id", fen="fen_id", om_name="om", cathode_name="cathode", anode_name="anode",
  filename=filename) AT (0, 0, 1) RELATIVE source
COMPONENT vmm3 = CollectorVMM3(ring="ring_id", fen="fen_id", bc_name="bc", otadc_name="otadc", geo_name="geo",
  tdc_name="tdc", vmm_name="vmm", channel_name="chan", filename=filename) AT (0, 0, 1) RELATIVE source
COMPONENT bm0 = CollectorBM0(ring="ring_id", fen="fen_id", channel_name="chan", filename=filename)
AT (0, 0, 1) RELATIVE source
COMPONENT bm2 = CollectorBM2(ring="ring_id", fen="fen_id", channel_name="chan", pos_x_name="posx", pos_y_name="posy",
  filename=filename) AT (0, 0, 1) RELATIVE source
COMPONENT bmi = CollectorBMI(ring="ring_id", fen="fen_id", channel_name="chan", sum_name="sum", adc_name="adc",
  filename=filename) AT (0, 0, 1) RELATIVE source
END
"""


@requires_classic
def test_classic_all_collectors_both_sinks(tmp_path):
    instr = tmp_path / "readout_all_collectors.instr"
    instr.write_text(ALL_COLLECTORS)
    groups = ("caen", "cdt", "vmm3", "bm0", "bm2", "bmi")

    hdf5_out = mcrun(instr, tmp_path, "run_hdf5", 1000)
    nexus_out = mcrun(instr, tmp_path, "run_nexus", 1000, fmt="NeXus")
    assert not (nexus_out / "all.h5").exists()
    imported = tmp_path / "imported.h5"
    result = readout_combine("import", str(nexus_out / "mccode.h5"), "-o", str(imported))
    assert result.returncode == 0, result.stderr

    with h5py.File(hdf5_out / "all.h5", "r") as a, h5py.File(imported, "r") as b:
        for group in groups:
            assert a[f"{group}/readouts"].dtype == b[f"{group}/readouts"].dtype, group
            assert np.array_equal(a[f"{group}/readouts"][()], b[f"{group}/readouts"][()]), group
        # the disc chopper declares its top-dead-centre channel in both cases
        assert "chopper_chopper_tdc" in a["parameters"]
        assert "chopper_chopper_tdc" in b["parameters"]
