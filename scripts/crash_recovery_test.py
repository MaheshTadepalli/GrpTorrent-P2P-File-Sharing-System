#!/usr/bin/env python3
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

from swarm_test import EXE, ROOT, build_if_needed, make_data, run, sha1, start_seed, start_tracker, terminate


def main():
    build_if_needed()
    tmp = ROOT / "data" / f"tmp-crash-{int(time.time() * 1000)}"
    tmp.mkdir(parents=True, exist_ok=True)
    procs = []
    try:
        tracker = start_tracker(18180)
        procs.append(tracker)
        src = tmp / "source.bin"
        make_data(src, 16 * 1024 * 1024)
        torrent = tmp / "source.grptorrent"
        run([EXE, "create", src, "http://127.0.0.1:18180/announce", torrent, 65536])
        seed = start_seed(torrent, src, 19100, tmp / "seed.state")
        procs.append(seed)
        time.sleep(1)
        out = tmp / "out.bin"
        state = tmp / "out.state"
        first = subprocess.Popen([str(EXE), "leech", str(torrent), str(out), "19101", str(state)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(1.5)
        terminate(first)
        before = state.read_text().count("1") if state.exists() else 0
        t0 = time.perf_counter()
        resumed = run([EXE, "leech", torrent, out, 19101, state])
        recovery_time = time.perf_counter() - t0
        after = state.read_text().count("1")
        metrics = {
            "recovery_time_s": round(recovery_time, 3),
            "verified_pieces_before_resume": before,
            "verified_pieces_after_resume": after,
            "integrity_ok": sha1(src) == sha1(out),
        }
        print(json.dumps(metrics, indent=2))
        return 0 if metrics["integrity_ok"] and after >= before else 1
    finally:
        for p in reversed(procs):
            terminate(p)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
