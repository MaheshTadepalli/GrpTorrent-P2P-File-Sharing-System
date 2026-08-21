#!/usr/bin/env python3
import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


class Tracker:
    def __init__(self, ttl):
        self.ttl = ttl
        self.swarms = {}

    def announce(self, info_hash, peer_id, host, port, event):
        now = time.time()
        swarm = self.swarms.setdefault(info_hash, {})
        for pid in list(swarm):
            if now - swarm[pid]["last_seen"] > self.ttl:
                del swarm[pid]
        if event == "stopped":
            swarm.pop(peer_id, None)
        else:
            swarm[peer_id] = {
                "peer_id": peer_id,
                "host": host,
                "port": int(port),
                "last_seen": now,
            }
        return [
            {"peer_id": p["peer_id"], "host": p["host"], "port": p["port"]}
            for p in swarm.values()
        ]


def make_handler(tracker):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            return

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path not in ("/announce", "/peers"):
                self.send_error(404)
                return
            q = parse_qs(parsed.query)
            info_hash = q.get("info_hash", [""])[0]
            peer_id = q.get("peer_id", [""])[0]
            port = q.get("port", ["0"])[0]
            event = q.get("event", ["started"])[0]
            if not info_hash:
                self.send_error(400, "missing info_hash")
                return
            if parsed.path == "/peers":
                peers = tracker.announce(info_hash, "__peek__", "127.0.0.1", 0, "stopped")
            else:
                if not peer_id or int(port) <= 0:
                    self.send_error(400, "missing peer_id or port")
                    return
                host = self.client_address[0]
                peers = tracker.announce(info_hash, peer_id, host, port, event)
            body = json.dumps({"peers": peers}).encode()
            self.send_response(200)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    return Handler


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--ttl", type=int, default=30)
    args = ap.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), make_handler(Tracker(args.ttl)))
    print(f"tracker listening on {args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

