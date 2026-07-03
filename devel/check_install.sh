#!/usr/bin/env bash
# Verify that an installed prefix of readout works: the self-locating
# readout-config resolves its paths, a scratch consumer project can
# find_package(Readout) and link against the C API, and (when mccode-antlr is
# available) the pytest run-tests pass against the installed prefix via
# READOUT_BUILD_DIR (an installed prefix has the same bin/lib layout as the
# build tree).
#
# Usage: devel/check_install.sh [build-dir]   (default: build)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${HERE}/${1:-build}"
SCRATCH="$(mktemp -d)"
trap 'rm -rf -- "${SCRATCH}"' EXIT
PREFIX="${SCRATCH}/prefix"

echo "== Installing ${BUILD_DIR} into ${PREFIX}"
cmake --install "${BUILD_DIR}" --prefix "${PREFIX}" > /dev/null

echo "== readout-config resolves installed paths"
CONFIG="${PREFIX}/bin/readout-config"
test -x "${CONFIG}"
for choice in libdir includedir compdir bindir; do
    resolved="$("${CONFIG}" --show "${choice}")"
    echo "   ${choice}: ${resolved}"
    test -d "${resolved}" || { echo "MISSING directory for ${choice}"; exit 1; }
done
test -f "$("${CONFIG}" --show compdir)/CollectorCAEN.comp" || { echo "components not installed"; exit 1; }
test -f "$("${CONFIG}" --show includedir)/reader.h" || { echo "C++ headers not installed"; exit 1; }
test -f "$("${CONFIG}" --show includedir)/version.hpp" || { echo "version.hpp not installed"; exit 1; }

echo "== installed binaries run (INSTALL_RPATH check, no LD_LIBRARY_PATH)"
env -u LD_LIBRARY_PATH "${PREFIX}/bin/readout-replay" --help > /dev/null
env -u LD_LIBRARY_PATH "${PREFIX}/bin/readout-combine" --help > /dev/null

echo "== find_package(Readout) consumer builds and runs"
CONSUMER="${SCRATCH}/consumer"
mkdir -p "${CONSUMER}"
cat > "${CONSUMER}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.20)
project(ReadoutConsumer LANGUAGES C)
find_package(Readout REQUIRED)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE Readout::readout)
EOF
cat > "${CONSUMER}/main.c" <<'EOF'
#include <Readout.h>
#include <stdio.h>
int main(void) {
  /* the C API is usable without HDF5/HighFive headers */
  if (collector_record_size(NULL) != 0) return 1;
  printf("consumer ok\n");
  return 0;
}
EOF
cmake -S "${CONSUMER}" -B "${CONSUMER}/build" -DReadout_DIR="${PREFIX}/$("${CONFIG}" --show libdir | sed "s|${PREFIX}/||")/cmake/Readout" > /dev/null
cmake --build "${CONSUMER}/build" > /dev/null
"${CONSUMER}/build/consumer"

if command -v mcstas-antlr > /dev/null 2>&1; then
    echo "== pytest run-tests against the installed prefix"
    (cd "${HERE}" && READOUT_BUILD_DIR="${PREFIX}" python3 -m pytest tests/ -q)
else
    echo "== mcstas-antlr not found; skipping pytest against the installed prefix"
fi

echo "== install check passed"
