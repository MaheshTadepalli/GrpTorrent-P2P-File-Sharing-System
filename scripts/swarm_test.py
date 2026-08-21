#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
import signal
import statistics
import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
def find_exe():
    names = ["grptorrent.exe", "grptorrent"] if os.name == "nt" else ["grptorrent"]
    candidates = [ROOT / "build" / n for n in names] + [ROOT / "build" / "Release" / n for n in names]
    for c in candidates:
        if c.exists():
            return c
    return candidates[0]


EXE = find_exe()


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], check=True, text=True, capture_output=True, **kw)


def popen(cmd, **kw):
    return subprocess.Popen([str(c) for c in cmd], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw)


def terminate(proc):
    if proc.poll() is not None:
        return
    if os.name == "nt":
        proc.terminate()
    else:
        proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=3)


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def make_data(path, size):
    with open(path, "wb") as f:
        block = hashlib.sha256(b"grptorrent-test-block").digest()
        written = 0
        while written < size:
            n = min(len(block), size - written)
            f.write(block[:n])
            written += n


def build_if_needed():
    if EXE.exists():
        return
    if os.name == "nt":
        run(["powershell", "-ExecutionPolicy", "Bypass", "-File", ROOT / "scripts" / "build.ps1"], cwd=ROOT)
    else:
        run(["bash", ROOT / "scripts" / "build.sh"], cwd=ROOT)


def start_tracker(port):
    p = popen([sys.executable, ROOT / "tracker" / "tracker.py", "--host", "127.0.0.1", "--port", port])
    time.sleep(0.6)
    return p


def start_seed(torrent, data, port, state):
    return popen([EXE, "seed", torrent, data, port, state])


def leech(torrent, out, port, state):
    t0 = time.perf_counter()
    p = run([EXE, "leech", torrent, out, port, state])
    elapsed = time.perf_counter() - t0
    last = "{}"
    for line in p.stdout.splitlines():
        if line.startswith("{"):
            last = line
    return elapsed, json.loads(last)


def percentile(values, pct):
    if not values:
        return 0.0
    values = sorted(values)
    idx = int(round((pct / 100.0) * (len(values) - 1)))
    return values[idx]


def swarm(args):
    build_if_needed()
    tmp = ROOT / "data" / f"tmp-swarm-{int(time.time() * 1000)}"
    tmp.mkdir(parents=True, exist_ok=True)
    procs = []
    try:
        tracker_port = args.tracker_port
        tracker = start_tracker(tracker_port)
        procs.append(tracker)
        src = tmp / "source.bin"
        make_data(src, args.size_mb * 1024 * 1024)
        torrent = tmp / "source.grptorrent"
        run([EXE, "create", src, f"http://127.0.0.1:{tracker_port}/announce", torrent, args.piece_size])
        seeds = []
        for i in range(args.seeds):
            state = tmp / f"seed-{i}.state"
            seeds.append(start_seed(torrent, src, args.base_port + i, state))
        procs.extend(seeds)
        time.sleep(1.0)
        latencies = []
        results = []
        for i in range(args.leechers):
            out = tmp / f"leecher-{i}.bin"
            state = tmp / f"leecher-{i}.state"
            elapsed, report = leech(torrent, out, args.base_port + args.seeds + i, state)
            latencies.append(elapsed)
            report["ok"] = sha1(src) == sha1(out)
            results.append(report)
        total_bytes = args.size_mb * 1024 * 1024 * args.leechers
        total_time = sum(latencies)
        metrics = {
            "aggregate_throughput_mib_s": round(total_bytes / max(total_time, 1e-9) / 1024 / 1024, 3),
            "p50_transfer_latency_s": round(percentile(latencies, 50), 3),
            "p95_transfer_latency_s": round(percentile(latencies, 95), 3),
            "p99_transfer_latency_s": round(percentile(latencies, 99), 3),
            "number_of_peers": args.seeds + args.leechers,
            "piece_integrity_rate": round(sum(1 for r in results if r["ok"]) / len(results), 3),
            "failed_or_corrupted_piece_recovery": sum(r.get("corrupt_or_failed_retries", 0) for r in results),
        }
        print(json.dumps(metrics, indent=2))
        return 0 if all(r["ok"] for r in results) else 1
    finally:
        for p in reversed(procs):
            terminate(p)
        if not args.keep:
            shutil.rmtree(tmp, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", type=int, default=2)
    ap.add_argument("--leechers", type=int, default=3)
    ap.add_argument("--size-mb", type=int, default=8)
    ap.add_argument("--piece-size", type=int, default=65536)
    ap.add_argument("--tracker-port", type=int, default=18080)
    ap.add_argument("--base-port", type=int, default=19000)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()
    raise SystemExit(swarm(args))


if __name__ == "__main__":
    main()
