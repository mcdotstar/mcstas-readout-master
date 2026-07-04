#!/usr/bin/env python3
"""Python twin of run_example.sh step 3: replay a collector file with a
stdout parameter publisher, equivalent to

    readout-replay --verbose --sequential --time 2.0 --seed 42 \
        --addr 127.0.0.1 --port 9000 <file>

but driven through the mcstas_readout package, the route an external tool
(e.g. mccode-plumber) takes to publish parameters to EPICS instead of stdout.

Usage: replay_publisher.py <collector-file.h5> [--cancel-after SECONDS]

Replay sends UDP packets; without a listening EFU they are simply dropped, so
this is safe to run anywhere. --cancel-after demonstrates thread-safe
cooperative cancellation: run() returns False instead of True when the timer
fires first.
"""
import argparse
import threading

import mcstas_readout as ro


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("filename", help="multi-point collector file, e.g. scan.h5")
    parser.add_argument("--cancel-after", type=float, default=None, metavar="SECONDS",
                        help="cancel the replay from a timer thread after this many seconds")
    args = parser.parse_args()

    points = ro.validate_collector_file(args.filename)
    print(f"{args.filename}: {points} point(s)")

    config = ro.ReplayConfig(counting_time=2.0, seed=42,
                             default_address="127.0.0.1", default_port=9000)
    job = ro.Replay(args.filename, config, ro.StreamParameterPublisher())

    timer = None
    if args.cancel_after is not None:
        timer = threading.Timer(args.cancel_after, job.cancel)
        timer.start()

    with job:
        completed = job.run()
    if timer is not None:
        timer.cancel()

    print("replay completed" if completed else "replay cancelled")
    return 0 if completed else 1


if __name__ == "__main__":
    raise SystemExit(main())
