# Measured Results

Measured locally on 2026-08-21 in this workspace.

## Swarm Benchmark

Command:

```bash
python scripts/swarm_test.py --seeds 2 --leechers 3 --size-mb 8
```

Results:

```json
{
  "aggregate_throughput_mib_s": 0.973,
  "p50_transfer_latency_s": 8.614,
  "p95_transfer_latency_s": 11.275,
  "p99_transfer_latency_s": 11.275,
  "number_of_peers": 5,
  "piece_integrity_rate": 1.0,
  "failed_or_corrupted_piece_recovery": 0
}
```

## Crash Recovery

Command:

```bash
python scripts/crash_recovery_test.py
```

Results:

```json
{
  "recovery_time_s": 22.278,
  "verified_pieces_before_resume": 25,
  "verified_pieces_after_resume": 256,
  "integrity_ok": true
}
```

## Corruption Retry

Command:

```bash
python scripts/corruption_retry_test.py
```

Results:

```json
{
  "elapsed_s": 4.813,
  "integrity_ok": true,
  "failed_or_corrupted_piece_recovery": 2
}
```
