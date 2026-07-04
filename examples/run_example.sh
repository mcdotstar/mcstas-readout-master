#!/usr/bin/env bash
# Collect -> combine -> replay walkthrough for readout_example.instr.
#
# Requires: McStas 3.3+ (mcrun) and an installed mcstas-readout-master
# (readout-config, readout-combine, readout-replay on PATH).
#
# Replay sends UDP packets; without a listening EFU they are simply dropped,
# so the walkthrough is safe to run anywhere. Point --addr/--port at a real
# EFU to feed the full ESS pipeline.
set -euo pipefail
cd "$(dirname "$0")"

command -v mcrun >/dev/null || { echo "mcrun (McStas 3.3+) not found on PATH" >&2; exit 1; }
command -v readout-config >/dev/null || { echo "readout-config not found on PATH; install mcstas-readout-master first" >&2; exit 1; }

workdir=$(mktemp -d readout-example.XXXXXX)
echo "Working in $workdir"

# 1. Collect: run two simulation points with different wavelengths.
#    Each run writes its collector groups ('monitor', 'detector') into one
#    HDF5 file in the run directory.
mcrun readout_example.instr -d "$workdir/point_0" -n 1e5 lambda=4.0 filename=point_0
mcrun readout_example.instr -d "$workdir/point_1" -n 1e5 lambda=5.0 filename=point_1

# 2. Validate and combine the per-point files into one multi-point file.
readout-combine validate "$workdir"/point_*/point_*.h5
readout-combine concatenate --output "$workdir/scan.h5" \
    "$workdir/point_0/point_0.h5" "$workdir/point_1/point_1.h5"
readout-combine validate "$workdir/scan.h5"

# 3. Replay: step through the two points, publish their parameters, and send
#    n ~ Poisson(weight * 2.0 s) events per stored readout.
readout-replay --verbose --sequential --time 2.0 --seed 42 \
    --addr 127.0.0.1 --port 9000 "$workdir/scan.h5"

echo "Done. Combined file: $workdir/scan.h5"
