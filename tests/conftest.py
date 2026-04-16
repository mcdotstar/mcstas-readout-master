"""Shared fixtures and helpers for mccode-readout Python integration tests.

These tests use mccode-antlr's Python interface to build, compile, and run
McStas instruments that exercise the Readout components defined in share/Readout.

Requirements:
  - mccode-antlr (pip install mccode-antlr)
  - A C compiler (cc / gcc)
  - readout-config on PATH (built by this project's CMake)
"""
from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

# ---------------------------------------------------------------------------
# Locate key paths relative to the repository root
# ---------------------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent
SHARE_READOUT = REPO_ROOT / "share" / "Readout"

# Build directory: honour READOUT_BUILD_DIR env var, else fall back to common names
_BUILD_DIR_CANDIDATES = ["build-dev", "build", "cmake-build-debug", "cmake-build-release"]


def _find_build_dir() -> Path | None:
    env = os.environ.get("READOUT_BUILD_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
    for name in _BUILD_DIR_CANDIDATES:
        p = REPO_ROOT / name
        if (p / "readout-config").is_file() or (p / "readout-config.exe").is_file():
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
    for cc in ("cc", "gcc", "clang"):
        if shutil.which(cc):
            return True
    return False


def _has_readout_config() -> bool:
    if BUILD_DIR is not None:
        rc = BUILD_DIR / "readout-config"
        if rc.is_file():
            return True
    return shutil.which("readout-config") is not None


# ---------------------------------------------------------------------------
# Environment with readout-config on PATH
# ---------------------------------------------------------------------------
def _build_env() -> dict[str, str]:
    env = os.environ.copy()
    if BUILD_DIR is not None:
        env["PATH"] = str(BUILD_DIR) + os.pathsep + env.get("PATH", "")
        # Ensure the dynamic linker can find libreadout
        ld_key = "DYLD_LIBRARY_PATH" if os.uname().sysname == "Darwin" else "LD_LIBRARY_PATH"
        env[ld_key] = str(BUILD_DIR) + os.pathsep + env.get(ld_key, "")
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
