#!/usr/bin/env python3
import subprocess
import json
import shutil
import time
from pathlib import Path

from swarm_test import EXE, ROOT, build_if_needed, make_data, run, sha1, start_seed, start_tracker, terminate


def main():
    build_if_needed()
    tmp = ROOT / "data" / f"tmp-corrupt-{int(time.time() * 1000)}"
    tmp.mkdir(parents=True, exist_ok=True)
    procs = []
    try:
        tracker = start_tracker(18280)
        procs.append(tracker)
        src = tmp / "source.bin"
        bad = tmp / "bad.bin"
        make_data(src, 1024 * 1024)
        shutil.copyfile(src, bad)
        torrent = tmp / "source.grptorrent"
        run([EXE, "create", src, "http://127.0.0.1:18280/announce", torrent, 1048576])
        bad_seed = start_seed(torrent, bad, 19200, tmp / "bad.state")
        procs.append(bad_seed)
        time.sleep(1)
        with open(bad, "r+b") as f:
            f.seek(70000)
            f.write(b"CORRUPTED")
        out = tmp / "out.bin"
        state = tmp / "out.state"
        started = time.perf_counter()
        leecher = subprocess.Popen([str(EXE), "leech", str(torrent), str(out), "19202", str(state)], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(1.0)
        good_seed = start_seed(torrent, src, 19201, tmp / "good.state")
        procs.append(good_seed)
        stdout, stderr = leecher.communicate(timeout=20)
        elapsed = time.perf_counter() - started
        retry_count = 0
        for line in stdout.splitlines():
            if line.startswith("{"):
                retry_count = json.loads(line).get("corrupt_or_failed_retries", 0)
        metrics = {
            "elapsed_s": round(elapsed, 3),
            "integrity_ok": sha1(src) == sha1(out),
            "failed_or_corrupted_piece_recovery": retry_count,
        }
        print(json.dumps(metrics, indent=2))
        return 0 if metrics["integrity_ok"] else 1
    finally:
        for p in reversed(procs):
            terminate(p)
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
