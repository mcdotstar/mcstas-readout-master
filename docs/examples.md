# Examples

## Complete runnable example

The repository ships a full collect → combine → replay walkthrough in
`examples/`:

- `examples/readout_example.instr` — a small instrument with a TTL beam
  monitor and a bank of CAEN-read tubes, both storing records into one
  collector file,
- `examples/run_example.sh` — runs two simulation points, validates and
  concatenates the per-point files, and replays the combined file.

\include readout_example.instr

The walkthrough:

\include run_example.sh

Replay sends UDP packets; without a listening EFU they are simply dropped, so
the walkthrough is safe to run anywhere.

## Example A: scan points, combine, replay

Run your instrument repeatedly (or as a scan), producing one collector file per
point:

```bash
./MyInstrument.out -n 1000000 filename=point_000
./MyInstrument.out -n 1000000 filename=point_001
./MyInstrument.out -n 1000000 filename=point_002
```

Concatenate into one multi-point file:

```bash
readout-combine concatenate --output scan_all.h5 point_000.h5 point_001.h5 point_002.h5
```

Replay with parameter publication and Poisson sampling:

```bash
readout-replay --time 2.0 --seed 42 --config senders.json scan_all.h5
```

## Example B: explicit sender routing

`senders.json` (see the [CLI reference](cli.md) for the full schema):

```json
{
  "senders": [
    {
      "detector_type": "BIFROST",
      "readout_type": "CAEN",
      "ip_address": "efu-caen.example.org",
      "udp_port": 9001,
      "tcp_port": 10800
    },
    {
      "detector_type": "TTLMonitor",
      "readout_type": "TTLMonitor",
      "ip_address": "efu-ttl.example.org",
      "udp_port": 9002,
      "tcp_port": 10800
    }
  ]
}
```

Run, with command-line defaults for any group not matched by the configuration
or by file-embedded attributes:

```bash
readout-replay --config senders.json --addr efu-default.example.org --port 9000 scan_all.h5
```

## Example C: instrument TRACE fragment for a collector

```instr
TRACE
  SEARCH SHELL "readout-config --show compdir"
  ...
  COMPONENT collect = CollectorTTLMonitor(
    filename="monitor_run",
    dataset_name="beam_monitor",
    ring="ring_id",
    fen="fen_id",
    identity="channel",
    value="adc",
    tof="t"
  ) AT (0,0,0) RELATIVE PREVIOUS
  ...
```
