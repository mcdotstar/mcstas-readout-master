#!/usr/bin/env bash
#
# run_tests.sh - Execute tests with proper environment isolation
#
# This script sets up the environment to run tests using the local build
# instead of any system-installed versions. It ensures that the local
# readout-config and libraries are found first in PATH and LD_LIBRARY_PATH.
#
# Usage:
#   ./run_tests.sh [BUILD_DIR] [TEST_SCRIPT]
#
# Examples:
#   ./run_tests.sh                          # Use 'build-dev' and run test_integration.sh
#   ./run_tests.sh build-dev                # Use 'build-dev' and run test_integration.sh
#   ./run_tests.sh build-dev test/test_integration.sh  # Specify both
#

set -e
set -o pipefail

# Get the directory where this script is located
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Defaults
BUILD_DIR="${1:-.}"
TEST_SCRIPT="${2:-test/test_integration.sh}"

# Handle case where build directory is passed but test is inferred
if [ "$#" -lt 2 ] && [ -n "$1" ] && [ ! -f "$1" ]; then
    # First arg is a directory (build dir), test script is default
    BUILD_DIR="$1"
    TEST_SCRIPT="test/test_integration.sh"
fi

# Resolve paths
# If BUILD_DIR is relative, make it relative to script location
if [[ "$BUILD_DIR" != /* ]]; then
    BUILD_DIR="${SCRIPT_DIR}/${BUILD_DIR}"
fi

BUILD_DIR="$(cd "$BUILD_DIR" 2>/dev/null && pwd)" || {
    echo "ERROR: Build directory not found at $BUILD_DIR" >&2
    exit 1
}

# Resolve test script path
if [[ "$TEST_SCRIPT" != /* ]]; then
    TEST_SCRIPT="${SCRIPT_DIR}/${TEST_SCRIPT}"
fi

if [ ! -f "$TEST_SCRIPT" ]; then
    echo "ERROR: Test script not found at $TEST_SCRIPT" >&2
    exit 1
fi

# Verify the build artifacts exist
READOUT_CONFIG="${BUILD_DIR}/readout-config"
if [ ! -x "$READOUT_CONFIG" ]; then
    echo "ERROR: readout-config not found at $READOUT_CONFIG" >&2
    echo "       Build the project first with CMake in development mode:" >&2
    echo "       cmake -S . -B $BUILD_DIR -DREADOUT_DEVELOPMENT_MODE=ON" >&2
    echo "       cmake --build $BUILD_DIR" >&2
    exit 1
fi

READOUT_LIBDIR="${BUILD_DIR}/lib"
if [ ! -d "$READOUT_LIBDIR" ]; then
    echo "ERROR: Library directory not found at $READOUT_LIBDIR" >&2
    exit 1
fi

READOUT_DATADIR="${BUILD_DIR}/share/Readout"
if [ ! -d "$READOUT_DATADIR" ]; then
    echo "ERROR: Data directory not found at $READOUT_DATADIR" >&2
    exit 1
fi

# Set up environment for testing
# Prepend build dir to PATH so ./readout-config is found first
export PATH="${BUILD_DIR}:${PATH}"

# Set LD_LIBRARY_PATH so libreadout is found
export LD_LIBRARY_PATH="${READOUT_LIBDIR}:${LD_LIBRARY_PATH:-}"

# Export paths for shell commands and scripts that might use readout-config
export READOUT_LIBDIR
export READOUT_INCLUDEDIR="${BUILD_DIR}/include"
export READOUT_COMPDIR="${READOUT_DATADIR}"

# Print environment info
echo "Test Environment Setup"
echo "======================"
echo "Build Directory:        $BUILD_DIR"
echo "Test Script:            $TEST_SCRIPT"
echo "PATH (first):           $BUILD_DIR"
echo "LD_LIBRARY_PATH:        $READOUT_LIBDIR"
echo "READOUT_COMPDIR:        $READOUT_COMPDIR"
echo ""

# Verify readout-config works
echo "Verifying readout-config..."
echo "  libdir:               $("$READOUT_CONFIG" --show libdir)"
echo "  includedir:           $("$READOUT_CONFIG" --show includedir)"
echo "  compdir:              $("$READOUT_CONFIG" --show compdir)"
echo ""

# Run the test - cd to project root first so relative paths work
echo "Running test: $TEST_SCRIPT"
echo "=============================================="

# Create symlink to readout-config in project root for the integration test script
READOUT_CONFIG_LINK="${SCRIPT_DIR}/readout-config"
if [ ! -L "$READOUT_CONFIG_LINK" ] && [ ! -f "$READOUT_CONFIG_LINK" ]; then
    ln -s "$READOUT_CONFIG" "$READOUT_CONFIG_LINK"
    CLEANUP_LINK=1
else
    CLEANUP_LINK=0
fi

trap 'if [ "$CLEANUP_LINK" = 1 ]; then rm -f "$READOUT_CONFIG_LINK"; fi' EXIT

# Run from a location without the source share underneath; otherwise mcstas-anltr will find
# the in-source share/ directory in addition to the build share/ which prevents instrument construction
cd "$BUILD_DIR"
bash "$TEST_SCRIPT"
EXIT_CODE=$?

echo "=============================================="
echo "Test exit code: $EXIT_CODE"
exit $EXIT_CODE
