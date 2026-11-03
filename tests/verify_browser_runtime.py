#!/usr/bin/env python3
"""Real CEF, loopback-only browser qualification. Requires the Windows probe.

No callbacks are mocked: storage is checked by page JavaScript across separate
processes, navigation by page markers, downloads by exact bytes, and TLS by an
untrusted local certificate. This is automated CEF evidence, not physical VT UI.
"""
from __future__ import annotations

import argparse
import hashlib
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

from runtime_process import output_tail, run_recorded, terminate_owned_process

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = b"ASCIIomium deterministic download v1\n"


def file_sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


class FixtureServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.counts: dict[str, int] = {}
        self.held: dict[str, dict] = {}
        self.state_lock = threading.Lock()

    def hold(self, run_id: str) -> dict:
        state = {"release": threading.Event(), "started": threading.Event(),
                 "finished": threading.Event(), "expired": False}
        with self.state_lock:
            self.held[run_id] = state
        return state

    def snapshot(self) -> dict:
        with self.state_lock:
            return {"http_requests": dict(self.counts), "held_responses": {
                name: {"prefix_flushed": state["started"].is_set(),
                       "released": state["release"].is_set(),
                       "finished": state["finished"].is_set(),
                       "safeguard_expired": state["expired"]}
                for name, state in self.held.items()}}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_GET(self):
        path = urllib.parse.urlsplit(self.path).path
        with self.server.state_lock:
            request_count = self.server.counts.get(path, 0) + 1
            self.server.counts[path] = request_count
        if path == "/network-error":
            self.close_connection = True
            return
        if path.startswith("/lifecycle-held/"):
            run_id = path.removeprefix("/lifecycle-held/")
            with self.server.state_lock:
                state = self.server.held.get(run_id)
            if state is None:
                self.send_error(404)
                return
            prefix = (f'<!doctype html><title>LOADING:{run_id}</title><body>' +
                      '<!--' + 'x' * 4096 + '--><p>Pending response</p>').encode("ascii")
            suffix = b'</body>'
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(prefix) + len(suffix)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                self.wfile.write(prefix)
                self.wfile.flush()
                state["started"].set()
                # Expiry is a failing safeguard, never the success condition.
                if not state["release"].wait(40):
                    with self.server.state_lock:
                        state["expired"] = True
                else:
                    self.wfile.write(suffix)
            except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
                pass  # The test deliberately closes during the unfinished body.
            finally:
                state["finished"].set()
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
            body = f'<!doctype html><title>PAGE:{name}:{request_count}</title><h1>{name}</h1>'.encode()
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
    parser.add_argument("--lifecycle-cycles", type=int, choices=(0, 10), default=0)
    args = parser.parse_args()
    exe, output = args.exe.resolve(), args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    reports = output.parent / "runtime-probes"
    http, https, origin, secure = None, None, None, None
    servers = []
    results = []
    source, source_tree = None, None
    binary = {"sha256": None, "identity": None}
    suite_state, suite_error = "running", None
    def save():
        server_state = http.snapshot() if http is not None else {"held_responses": {}, "http_requests": {}}
        output.write_text(json.dumps({"schema": "asciiomium-browser-runtime-v1", "source_revision": source,
            "source_tree": source_tree, "binary": binary, "state": suite_state, "error": suite_error,
            "qualification": "automated-real-CEF-loopback", "physical_windows_terminal": False,
            "lifecycle_cycles_requested": args.lifecycle_cycles,
            **server_state, "results": results}, indent=2) + "\n", encoding="utf-8")

    def verify_identity(identity):
        assert identity["source_identity_available"] and not identity["source_dirty"], "probe source identity is unavailable or dirty"
        assert identity["source_revision"] == source and identity["source_tree"] == source_tree, "probe binary/source identity mismatch"
        assert identity["cef_runtime_matches_headers"], "probe CEF ABI/header mismatch"

    def run(name, scenario, extra, *, base=None, expected=0):
        base = base or origin
        target = reports / (name + ".json")
        target.unlink(missing_ok=True)
        command = [str(exe), "--origin", base, "--scenario", scenario, "--report", str(target), *map(str, extra)]
        stdout, stderr = reports / (name + ".stdout.txt"), reports / (name + ".stderr.txt")
        record = {"name": name, "scenario": scenario, "state": "starting", "expected_exit": expected}
        results.append(record)
        save()
        try:
            record.update(run_recorded(command, stdout, stderr))
            record["report_present"] = target.exists()
            if target.exists():
                record["probe"] = json.loads(target.read_text(encoding="utf-8"))
                verify_identity(record["probe"])
            assert record["state"] == "exited" and record["exit_code"] == expected, \
                f"{name}: {record['state']}, exit {record['exit_code']}\n{output_tail(stderr)}\n{output_tail(stdout)}"
            if expected == 0:
                assert record["probe"]["schema"] == "asciiomium-runtime-probe-v1" and record["probe"]["scenario"] == scenario
                assert record["probe"]["passed"] and record["probe"]["closed_cleanly"] and record["probe"]["profile_cleanup"]
                assert not record["probe"]["callbacks"]["failed"], "probe retained a callback failure"
                if "--private" in extra:
                    supervisor = record["probe"]["private_supervisor"]
                    assert supervisor["worker_exit"] == 0 and supervisor["process_tree_reaped"] and supervisor["profile_removed"]
                    assert not supervisor["forced_descendants"] and not record["probe"]["profile_cleanup_deferred"]
                if scenario.startswith("lifecycle-"):
                    assert record["probe"]["probe_stage"] == "complete" and record["probe"]["phase_reached"]
                    assert record["probe"]["lifecycle_phase"] == scenario.removeprefix("lifecycle-")
                    if scenario == "lifecycle-resize-requested":
                        resize = record["probe"]["resize"]
                        assert resize["baseline_generation"] > 0 and resize["request_accepted"]
                        assert (resize["initial_width"], resize["initial_height"]) == (640, 360)
                        assert (resize["requested_width"], resize["requested_height"]) == (800, 450)
                        assert not resize["explicit_pump_before_shutdown"]
            record["qualification_passed"] = True
        except BaseException as error:
            record["qualification_passed"] = False
            record["error"] = {"type": type(error).__name__, "message": str(error)[:8192]}
            raise
        finally:
            save()
        print(f"PASS {name}", flush=True)
        return record
    try:
        save()
        reports.mkdir(exist_ok=True)
        source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
        source_tree = subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip()
        binary["sha256"] = file_sha256(exe)
        http = FixtureServer()
        servers.append([http, False])
        https = FixtureServer()
        servers.append([https, False])
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(ROOT / "fixtures/runtime/untrusted-test-cert.pem",
                                ROOT / "fixtures/runtime/untrusted-test-key.pem")
        https.socket = context.wrap_socket(https.socket, server_side=True)
        for owned in servers:
            threading.Thread(target=owned[0].serve_forever, daemon=True).start()
            owned[1] = True
        origin = f"http://127.0.0.1:{http.server_port}"
        secure = f"https://127.0.0.1:{https.server_port}"
        assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip(), "runtime qualification requires a clean source checkout"
        assert not any((exe.parent / name).exists() for name in ("chrome_debug.log", "debug.log")), "qualification requires an executable directory without stale diagnostic browser logs"
        identity_out, identity_err = reports / "identity.json", reports / "identity.stderr.txt"
        identity_process = run_recorded([str(exe), "--identity"], identity_out, identity_err, 20)
        binary["identity_process"] = identity_process
        assert identity_process["state"] == "exited" and identity_process["exit_code"] == 0, "probe identity command failed"
        binary["identity"] = json.loads(identity_out.read_text(encoding="utf-8"))
        assert binary["identity"]["schema"] == "asciiomium-runtime-probe-identity-v1"
        verify_identity(binary["identity"])
        save()
        with tempfile.TemporaryDirectory(prefix="asciiomium-runtime-") as temporary:
            temp = Path(temporary)
            a, b, download = temp / "profile-a", temp / "profile-b", temp / "downloads"
            run("persistent-write", "storage-write", ["--profile", a])
            run("persistent-restart", "storage-read", ["--profile", a])
            run("separate-profile", "storage-empty", ["--profile", b])
            run("private-write", "storage-write", ["--private"])
            run("private-restart", "storage-empty", ["--private"])
            run("persistent-still-intact", "storage-read", ["--profile", a])
            for profile_root in (a, b):
                assert not list(profile_root.rglob("chrome_debug.log")) and not list(profile_root.rglob("debug.log")), "runtime created a diagnostic browser log despite explicit disabled destination"
            assert not any((exe.parent / name).exists() for name in ("chrome_debug.log", "debug.log")), "runtime created a diagnostic browser log beside the executable"
            results.append({"name": "diagnostic-file-logging-disabled", "passed": True})
            ready_report = reports / "ownership-holder.json"
            ready_report.unlink(missing_ok=True)
            ready = Path(str(ready_report) + ".ready")
            ready.unlink(missing_ok=True)
            holder_record = {"name": "ownership-holder", "state": "starting", "expected_exit": 0,
                             "exit_code": None, "timed_out": False,
                             "stdout_file": "ownership-holder.stdout.txt", "stderr_file": "ownership-holder.stderr.txt"}
            results.append(holder_record)
            save()
            holder, started = None, time.monotonic()
            with (reports / holder_record["stdout_file"]).open("w", encoding="utf-8") as out, \
                 (reports / holder_record["stderr_file"]).open("w", encoding="utf-8") as err:
                try:
                    holder = subprocess.Popen([str(exe), "--origin", origin, "--scenario", "hold", "--profile", str(a), "--report", str(ready_report)], stdout=out, stderr=err)
                    holder_record["state"], holder_record["pid"] = "running", holder.pid
                    deadline = time.monotonic() + 15
                    while not ready.exists() and time.monotonic() < deadline and holder.poll() is None:
                        time.sleep(0.05)
                    if not ready.exists() and holder.poll() is None:
                        holder_record["timed_out"] = time.monotonic() >= deadline
                    assert ready.exists(), "profile holder failed to reach real CEF page"
                    collision = run("ownership-collision", "storage-read", ["--profile", a], expected=1)
                    assert "already in use" in output_tail(reports / collision["stderr_file"]), "ownership refusal is not explicit"
                    holder_record["exit_code"] = holder.wait(timeout=30)
                    holder_record["state"] = "exited"
                    assert holder.returncode == 0, "ownership holder failed"
                    holder_record["probe"] = json.loads(ready_report.read_text(encoding="utf-8"))
                    verify_identity(holder_record["probe"])
                    assert holder_record["probe"]["passed"] and holder_record["probe"]["closed_cleanly"] and holder_record["probe"]["profile_cleanup"]
                    assert not holder_record["probe"]["callbacks"]["failed"], "ownership holder retained a callback failure"
                    holder_record["qualification_passed"] = True
                except BaseException as error:
                    holder_record["timed_out"] |= isinstance(error, subprocess.TimeoutExpired)
                    holder_record["state"] = "timeout" if holder_record["timed_out"] else "start-error" if holder is None else "failed"
                    holder_record["qualification_passed"] = False
                    holder_record["error"] = {"type": type(error).__name__, "message": str(error)[:8192]}
                    raise
                finally:
                    if holder is not None and holder.poll() is None:
                        holder_record["termination"] = terminate_owned_process(holder)
                    if holder is not None:
                        holder_record["exit_code"] = holder.poll()
                    holder_record["elapsed_seconds"] = time.monotonic() - started
                    holder_record["report_present"] = ready_report.exists()
                    save()
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
            phases = ["ready", "before-created", "navigation-requested", "held-load", "resize-requested"] * 2
            for index, phase in enumerate(phases[:args.lifecycle_cycles]):
                run_id = f"shutdown-{index:02d}"
                held = http.hold(run_id) if phase == "held-load" else None
                record = None
                try:
                    record = run(run_id + "-" + phase, "lifecycle-" + phase,
                                 ["--private", "--run-id", run_id])
                    if held:
                        assert held["started"].is_set(), "incomplete response never reached the browser"
                        assert not held["expired"], "held response escaped on its safeguard timeout"
                        record["held_response"] = {"prefix_flushed": True, "release_after_process_exit": True,
                                                   "safeguard_expired": False}
                except BaseException as error:
                    if record is not None:
                        record["qualification_passed"] = False
                        record["error"] = {"type": type(error).__name__, "message": str(error)[:8192]}
                    raise
                finally:
                    if held:
                        held["release"].set()
                        finished = held["finished"].wait(5)
                        if not finished and record is not None and record.get("qualification_passed"):
                            record["qualification_passed"] = False
                            raise AssertionError("held response thread did not finish after release")
                    save()
            binary["sha256_after"] = file_sha256(exe)
            assert binary["sha256_after"] == binary["sha256"], "probe executable changed during qualification"
            assert subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip() == source, "source revision changed during qualification"
            assert subprocess.check_output(["git", "rev-parse", "HEAD^{tree}"], cwd=ROOT, text=True).strip() == source_tree, "source tree changed during qualification"
            assert not subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip(), "source checkout changed during qualification"
            suite_state = "passed"
            save()
    except BaseException as error:
        suite_state = "failed"
        suite_error = {"type": type(error).__name__, "message": str(error)[:8192]}
        raise
    finally:
        try:
            save()
        finally:
            if http is not None:
                for held in http.held.values():
                    held["release"].set()
            for server, started in servers:
                if started:
                    server.shutdown()
                server.server_close()
    print(f"Browser runtime qualification complete: {len(results)} records")


if __name__ == "__main__":
    main()
