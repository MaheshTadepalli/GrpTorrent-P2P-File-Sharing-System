#!/usr/bin/env python3
import hashlib
import os
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


def run(cmd, timeout=None):
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, text=True, capture_output=True, check=True, timeout=timeout)


def build():
    if os.name == "nt":
        run(["powershell", "-ExecutionPolicy", "Bypass", "-File", ROOT / "scripts" / "build.ps1"])
    else:
        run(["bash", ROOT / "scripts" / "build.sh"])


def sha1(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def terminate(p):
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=3)
        except subprocess.TimeoutExpired:
            p.kill()


def main():
    build()
    scratch = ROOT / "data" / "tmp-test"
    if scratch.exists():
        import shutil
        shutil.rmtree(scratch)
    scratch.mkdir(parents=True)
    try:
        td = scratch
        src = td / "src.bin"
        src.write_bytes((b"0123456789abcdef" * 65536)[:1024 * 1024])
        torrent = td / "src.grptorrent"
        run([EXE, "create", src, "http://127.0.0.1:18380/announce", torrent, 65536])
        meta = torrent.read_text()
        assert "length=1048576" in meta
        tracker = subprocess.Popen([sys.executable, str(ROOT / "tracker" / "tracker.py"), "--host", "127.0.0.1", "--port", "18380"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        seed = None
        try:
            time.sleep(0.6)
            seed = subprocess.Popen([str(EXE), "seed", str(torrent), str(src), "19300", str(td / "seed.state")], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            time.sleep(1.0)
            out = td / "out.bin"
            try:
                result = run([EXE, "leech", torrent, out, 19301, td / "out.state"], timeout=15)
            except subprocess.TimeoutExpired as e:
                if seed:
                    terminate(seed)
                terminate(tracker)
                seed_err = seed.stderr.read() if seed and seed.stderr else ""
                tracker_err = tracker.stderr.read() if tracker and tracker.stderr else ""
                raise AssertionError(f"leecher timed out\nstdout={e.stdout}\nstderr={e.stderr}\nseed={seed_err}\ntracker={tracker_err}")
            except subprocess.CalledProcessError as e:
                raise AssertionError(f"leecher failed rc={e.returncode}\nstdout={e.stdout}\nstderr={e.stderr}")
            assert sha1(src) == sha1(out), result.stdout + result.stderr
        finally:
            if seed:
                terminate(seed)
            terminate(tracker)
        print("tests passed")
    finally:
        import shutil
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
