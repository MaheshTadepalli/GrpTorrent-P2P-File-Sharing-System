#!/usr/bin/env python3
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / "build" / ("grptorrent.exe" if __import__("os").name == "nt" else "grptorrent")


def test_create_metadata():
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        src = d / "a.bin"
        src.write_bytes(b"abcdef" * 20000)
        tor = d / "a.grptorrent"
        subprocess.run([str(EXE), "create", str(src), "http://127.0.0.1:8080/announce", str(tor), "32768"], check=True)
        text = tor.read_text()
        assert "announce=http://127.0.0.1:8080/announce" in text
        assert "piece_length=32768" in text
        assert "pieces=" in text
        assert "info_hash=" in text

