"""Shared fixtures and helpers for mccode-readout Python integration tests.

These tests use mccode-antlr's Python interface to build, compile, and run
McStas instruments that exercise the Readout components defined in share/Readout.

Requirements:
  - mccode-antlr (pip install mccode-antlr)
  - A C compiler (cc / gcc / clang, or cl.exe on Windows)
  - readout-config on PATH (built by this project's CMake)
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Locate key paths relative to the repository root
# ---------------------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent
SHARE_READOUT = REPO_ROOT / "readout_core" / "components"

# Build directory: honour READOUT_BUILD_DIR env var, else fall back to common names
_BUILD_DIR_CANDIDATES = ["build-dev", "build", "cmake-build-debug", "cmake-build-release"]

# Multi-config generators (Visual Studio, Xcode) nest binaries one level deeper.
_BIN_SUBDIRS = ["", "Debug", "Release", "RelWithDebInfo", "MinSizeRel"]


def _readout_config_in(build_dir: Path) -> Path | None:
    """Locate readout-config inside a build tree, single- or multi-config."""
    for sub in _BIN_SUBDIRS:
        bin_dir = build_dir / "bin" / sub if sub else build_dir / "bin"
        for name in ("readout-config", "readout-config.exe"):
            if (bin_dir / name).is_file():
                return bin_dir / name
    return None


def _find_build_dir() -> Path | None:
    env = os.environ.get("READOUT_BUILD_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
    for name in _BUILD_DIR_CANDIDATES:
        p = REPO_ROOT / name
        if _readout_config_in(p) is not None:
            return p
    return None


BUILD_DIR = _find_build_dir()

# ---------------------------------------------------------------------------
# Skip helpers
# ---------------------------------------------------------------------------

def _can_import_mccode_antlr() -> bool:
    try:
        import mccode_antlr  # noqa: F401
        return True
    except ImportError:
        return False


def _has_c_compiler() -> bool:
    # mccode-antlr picks cl.exe on Windows (see mccode_antlr/config/platforms.yaml),
    # so a pure-MSVC machine counts as having a C compiler.
    candidates = ("cl", "cc", "gcc", "clang") if os.name == "nt" else ("cc", "gcc", "clang")
    for cc in candidates:
        if shutil.which(cc):
            return True
    return False


def _has_readout_config() -> bool:
    if BUILD_DIR is not None and _readout_config_in(BUILD_DIR) is not None:
        return True
    return shutil.which("readout-config") is not None


# ---------------------------------------------------------------------------
# Environment with readout-config on PATH
# ---------------------------------------------------------------------------
def _build_env() -> dict[str, str]:
    env = os.environ.copy()
    if BUILD_DIR is not None:
        rc = _readout_config_in(BUILD_DIR)
        bin_dirs = [str(rc.parent)] if rc is not None else []
        if str(BUILD_DIR / "bin") not in bin_dirs:
            bin_dirs.append(str(BUILD_DIR / "bin"))
        env["PATH"] = os.pathsep.join(bin_dirs) + os.pathsep + env.get("PATH", "")
        # Ensure the dynamic loader can find libreadout.  os.uname() is POSIX-only,
        # so branch on sys.platform; Windows resolves readout.dll from PATH (set
        # above, since CMAKE_RUNTIME_OUTPUT_DIRECTORY puts the DLL in bin/) and has
        # no LD_LIBRARY_PATH equivalent.
        if sys.platform == "darwin":
            ld_key = "DYLD_LIBRARY_PATH"
        elif os.name == "nt":
            ld_key = None
        else:
            ld_key = "LD_LIBRARY_PATH"
        if ld_key is not None:
            env[ld_key] = str(BUILD_DIR / "lib") + os.pathsep + env.get(ld_key, "")
    return env


def _readout_config_works() -> bool:
    """Check that readout-config can actually return the component directory."""
    try:
        env = _build_env()
        result = subprocess.run(
            ["readout-config", "--show", "compdir"],
            capture_output=True, text=True, env=env, timeout=5,
        )
        return result.returncode == 0 and len(result.stdout.strip()) > 0
    except Exception:
        return False


# Combined skip condition: everything needed for integration tests
requires_integration = pytest.mark.skipif(
    not (_can_import_mccode_antlr() and _has_c_compiler() and _has_readout_config()),
    reason="Integration tests require mccode-antlr, a C compiler, and readout-config",
)

requires_run = pytest.mark.skipif(
    not (_can_import_mccode_antlr() and _has_c_compiler() and _readout_config_works()),
    reason="Run tests require mccode-antlr, a C compiler, and a working readout-config",
)


@pytest.fixture
def build_env() -> dict[str, str]:
    """Return an environment dict with readout-config and libreadout discoverable."""
    return _build_env()


@pytest.fixture
def readout_registry():
    """Return a LocalRegistry pointing at share/Readout."""
    from mccode_antlr.reader.registry import registry_from_specification
    return registry_from_specification(str(SHARE_READOUT))


# ---------------------------------------------------------------------------
# Common instrument fragments
# ---------------------------------------------------------------------------
CAEN_USERVARS = """\
USERVARS %{
int RING;
int FEN;
int TUBE;
int A;
int B;
double tof;
%}
"""

CAEN_ORIGIN_EXTEND = """\
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND %{
RING = 1;
FEN = 2;
TUBE = 3;
A = 100;
B = 200;
tof = 0.001;
x = 0; y = 0; z = 0;
vx = 0; vy = 0; vz = 1000;
p = 1;
%}
"""

TTL_USERVARS = """\
USERVARS %{
int RING;
int FEN;
int TUBE;
int A;
int B;
double tof;
%}
"""

TTL_ORIGIN_EXTEND = """\
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND %{
RING = 1;
FEN = 2;
TUBE = 3;
A = 10;
B = 1;
tof = 0.001;
x = 0; y = 0; z = 0;
vx = 0; vy = 0; vz = 1000;
p = 1;
%}
"""

CDT_USERVARS = """\
USERVARS %{
int RING;
int FEN;
int OM;
int CATHODE;
int ANODE;
double tof;
%}
"""

CDT_ORIGIN_EXTEND = """\
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND %{
RING = 1;
FEN = 0;
OM = 2;
CATHODE = 3;
ANODE = 4;
tof = 0.001;
x = 0; y = 0; z = 0;
vx = 0; vy = 0; vz = 1000;
p = 1;
%}
"""

VMM3_USERVARS = """\
USERVARS %{
int RING;
int FEN;
int BC;
int OTADC;
int GEO;
int TDC;
int VMM;
int CHANNEL;
double tof;
%}
"""

VMM3_ORIGIN_EXTEND = """\
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND %{
RING = 1;
FEN = 0;
BC = 100;
OTADC = 200;
GEO = 1;
TDC = 2;
VMM = 3;
CHANNEL = 4;
tof = 0.001;
x = 0; y = 0; z = 0;
vx = 0; vy = 0; vz = 1000;
p = 1;
%}
"""

BM0_USERVARS = """\
USERVARS %{
int RING;
int FEN;
int CHANNEL;
double tof;
%}
"""

BM0_ORIGIN_EXTEND = """\
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND %{
RING = 1;
FEN = 0;
CHANNEL = 5;
tof = 0.001;
x = 0; y = 0; z = 0;
vx = 0; vy = 0; vz = 1000;
p = 1;
%}
"""

BM2_USERVARS = """\
USERVARS %{
int RING;
int FEN;
int CHANNEL;
int POSX;
int POSY;
double tof;
%}
"""

BM2_ORIGIN_EXTEND = """\
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND %{
RING = 1;
FEN = 0;
CHANNEL = 5;
POSX = 100;
POSY = 200;
tof = 0.001;
x = 0; y = 0; z = 0;
vx = 0; vy = 0; vz = 1000;
p = 1;
%}
"""

BMI_USERVARS = """\
USERVARS %{
int RING;
int FEN;
int CHANNEL;
int SUM;
int ADC;
double tof;
%}
"""

BMI_ORIGIN_EXTEND = """\
COMPONENT origin = Arm() AT (0, 0, 0) ABSOLUTE
EXTEND %{
RING = 1;
FEN = 0;
CHANNEL = 5;
SUM = 7;
ADC = 1000;
tof = 0.001;
x = 0; y = 0; z = 0;
vx = 0; vy = 0; vz = 1000;
p = 1;
%}
"""
