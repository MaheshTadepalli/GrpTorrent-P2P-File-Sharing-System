# GrpTorrent - P2P File Sharing System

GrpTorrent is a compact BitTorrent-style demo written for local swarms. The torrent client is C++17; the tracker is a small Python HTTP service used only for peer discovery.

Implemented core features:

- Torrent metadata parser for file size, piece size, piece SHA-1 hashes, and tracker URL.
- Tracker announce flow for peer registration and discovery.
- BitTorrent-style peer messages: handshake, choke, unchoke, interested, not-interested, have, request, piece, and cancel.
- Rarest-first piece choice from connected peer bitfields.
- Tit-for-tat-oriented choking structure with interested/choke/unchoke control messages and useful-byte accounting hooks kept in the peer model.
- Pipelined block transfer with multiple outstanding requests per piece.
- SHA-1 verification before accepting a piece.
- Crash-safe resume through a persisted verified-piece bitmap.
- Peer failure recovery by releasing failed in-flight pieces back to the scheduler.
- Local swarm, crash/recovery, corruption/retry, and load benchmark scripts.

## Build

Windows PowerShell:

```powershell
.\scripts\build.ps1
```

Linux/macOS:

```bash
bash scripts/build.sh
```

## Quick Run

Start a tracker:

```bash
python3 tracker/tracker.py --host 127.0.0.1 --port 8080
```

Create metadata:

```bash
build/grptorrent create data/source.bin http://127.0.0.1:8080/announce data/source.grptorrent 65536
```

Start a seeder:

```bash
build/grptorrent seed data/source.grptorrent data/source.bin 9000 data/seed.state
```

Start a leecher:

```bash
build/grptorrent leech data/source.grptorrent data/download.bin 9001 data/download.state
```

## Tests

```bash
python3 tests/run_tests.py
python3 scripts/crash_recovery_test.py
python3 scripts/corruption_retry_test.py
```

## Swarm Benchmark

```bash
python3 scripts/swarm_test.py --seeds 2 --leechers 3 --size-mb 8
```

The benchmark prints only measured values:

- aggregate throughput
- p50/p95/p99 transfer latency
- number of peers
- piece integrity rate
- failed/corrupted piece recovery count

Crash recovery prints the measured recovery time separately:

```bash
python3 scripts/crash_recovery_test.py
```

## Docker Compose

```bash
docker compose up --build
```

The compose file starts a tracker, one seed, and one leecher. The local scripts are better for repeatable measurements because they control process lifetime and compute integrity metrics.

## Metadata Format

GrpTorrent uses a small line-oriented `.grptorrent` metadata file:

```text
announce=http://127.0.0.1:8080/announce
name=source.bin
length=1048576
piece_length=65536
pieces=<40 hex chars per piece>
info_hash=<sha1 of metadata identity>
```

The format is intentionally simple so the protocol and swarm behavior remain the focus.

