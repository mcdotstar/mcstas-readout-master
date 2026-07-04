# Examples

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

`senders.json`:

```json
{
  "default": { "address": "efu-default.example.org", "port": 9000 },
  "routes": [
    { "detector": "BIFROST", "readout": "CAEN", "address": "efu-caen.example.org", "port": 9001 },
    { "detector": "CSPEC", "readout": "TTLMonitor", "address": "efu-ttl.example.org", "port": 9002 }
  ]
}
```

Run:

```bash
readout-replay --config senders.json scan_all.h5
```

## Example C: instrument TRACE fragment for a collector

```instr
TRACE
  SEARCH SHELL "readout-config --show compdir"
  ...
  COMPONENT collect = CollectorTTLMonitor(
    filename="monitor_run",
    dataset_name="beam_monitor",
    fen="fen",
    identity="channel",
    output="adc",
    tof="t",
    ess_type=64
  ) AT (0,0,0) RELATIVE PREVIOUS
  ...
```
