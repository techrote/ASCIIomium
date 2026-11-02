#!/usr/bin/env python3
"""Real CEF, loopback-only browser qualification. Requires the Windows probe.

No callbacks are mocked: storage is checked by page JavaScript across separate
processes, navigation by page markers, downloads by exact bytes, and TLS by an
untrusted local certificate. This is automated CEF evidence, not physical VT UI.
"""
from __future__ import annotations

import argparse
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import time
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = b"ASCIIomium deterministic download v1\n"


class FixtureServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.counts: dict[str, int] = {}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        self.server.counts[path] = self.server.counts.get(path, 0) + 1
        if path == "/network-error":
            self.close_connection = True
            return
        if path == "/slow":
            time.sleep(4)
        if path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/a")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if path == "/storage":
            body = (ROOT / "fixtures/runtime/storage.html").read_bytes()
        elif path == "/policies":
            body = (ROOT / "fixtures/runtime/policies.html").read_bytes()
        elif path == "/download-page":
            body = b'<!doctype html><title>READY:download</title><style>body{margin:0}a{position:absolute;left:10px;top:10px;width:200px;height:50px}</style><a href="/download">Download benign test file</a>'
        elif path == "/download":
            body = PAYLOAD
        else:
            name = path.strip("/") or "a"
            body = f'<!doctype html><title>PAGE:{name}:{self.server.counts[path]}</title><h1>{name}</h1>'.encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream" if path == "/download" else "text/html; charset=utf-8")
        if path == "/download":
            # The remote name must never control an on-disk path.
            self.send_header("Content-Disposition", 'attachment; filename="../../remote-name.exe"')
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass  # Expected for StopLoad and shutdown probes.


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    exe, output = args.exe.resolve(), args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    reports = output.parent / "runtime-probes"
    reports.mkdir(exist_ok=True)
    http = FixtureServer()
    https = FixtureServer()
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(ROOT / "fixtures/runtime/untrusted-test-cert.pem",
                            ROOT / "fixtures/runtime/untrusted-test-key.pem")
    https.socket = context.wrap_socket(https.socket, server_side=True)
    for server in (http, https):
        threading.Thread(target=server.serve_forever, daemon=True).start()
    origin = f"http://127.0.0.1:{http.server_port}"
    secure = f"https://127.0.0.1:{https.server_port}"
    results = []
    source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    def save():
        output.write_text(json.dumps({"schema": "asciiomium-browser-runtime-v1", "source_revision": source,
            "qualification": "automated-real-CEF-loopback", "physical_windows_terminal": False,
            "results": results, "http_requests": http.counts}, indent=2) + "\n", encoding="utf-8")
    def run(name, scenario, extra, *, base=origin, expected=0):
        target = reports / (name + ".json")
        target.unlink(missing_ok=True)
        command = [str(exe), "--origin", base, "--scenario", scenario, "--report", str(target), *map(str, extra)]
        process = subprocess.run(command, capture_output=True, text=True, timeout=45)
        (reports / (name + ".stderr.txt")).write_text(process.stderr, encoding="utf-8")
        record = {"name": name, "exit_code": process.returncode, "expected_exit": expected}
        if target.exists():
            record["probe"] = json.loads(target.read_text(encoding="utf-8"))
        results.append(record)
        save()
        assert process.returncode == expected, f"{name}: exit {process.returncode}\n{process.stderr}\n{process.stdout}"
        if expected == 0:
            assert record["probe"]["schema"] == "asciiomium-runtime-probe-v1" and record["probe"]["scenario"] == scenario
            assert record["probe"]["passed"] and record["probe"]["closed_cleanly"] and record["probe"]["profile_cleanup"]
        print(f"PASS {name}", flush=True)
        return process
    try:
        with tempfile.TemporaryDirectory(prefix="asciiomium-runtime-") as temporary:
            temp = Path(temporary)
            a, b, download = temp / "profile-a", temp / "profile-b", temp / "downloads"
            run("persistent-write", "storage-write", ["--profile", a])
            run("persistent-restart", "storage-read", ["--profile", a])
            run("separate-profile", "storage-empty", ["--profile", b])
            run("private-write", "storage-write", ["--private"])
            run("private-restart", "storage-empty", ["--private"])
            run("persistent-still-intact", "storage-read", ["--profile", a])
            ready_report = reports / "ownership-holder.json"
            ready = Path(str(ready_report) + ".ready")
            ready.unlink(missing_ok=True)
            holder = subprocess.Popen([str(exe), "--origin", origin, "--scenario", "hold", "--profile", str(a), "--report", str(ready_report)], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 15
                while not ready.exists() and time.monotonic() < deadline and holder.poll() is None:
                    time.sleep(0.05)
                assert ready.exists(), "profile holder failed to reach real CEF page"
                collision = run("ownership-collision", "storage-read", ["--profile", a], expected=1)
                assert "already in use" in collision.stderr, "ownership refusal is not explicit"
                _, errors = holder.communicate(timeout=30)
                assert holder.returncode == 0, errors
            finally:
                if holder.poll() is None:
                    holder.kill()
                    holder.wait()
            run("after-ownership-release", "storage-read", ["--profile", a])
            run("navigation-history-reload-stop-recovery", "navigation", ["--private"])
            run("popup-permission-denial", "policies", ["--private"])
            fallback = temp / "chromium-default-downloads"
            fallback.mkdir()
            run("download-default-denial", "download-deny", ["--private", "--default-download-directory", fallback])
            assert not any(fallback.rglob("*")), "denied download wrote to Chromium's bounded default destination"
            run("download-controlled-destination", "download-allow", ["--private", "--downloads", download])
            files = sorted(p for p in download.rglob("*") if p.is_file())
            assert len(files) == 1 and files[0].name == "download.bin" and files[0].read_bytes() == PAYLOAD, "download bytes/path do not match controlled destination"
            results.append({"name": "download-byte-integrity", "passed": True, "bytes": len(PAYLOAD), "remote_filename_ignored": True})
            run("untrusted-TLS", "tls", ["--private"], base=secure)
            save()
    finally:
        save()
        for server in (http, https):
            server.shutdown()
            server.server_close()
    print(f"Browser runtime qualification complete: {len(results)} records")


if __name__ == "__main__":
    main()
