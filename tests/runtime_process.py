"""Bounded process waits with evidence independent of inherited pipe handles."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import time


def terminate_owned_process(process: subprocess.Popen) -> dict:
    """Terminate only the process launched by this harness and its descendants."""
    result = {"attempted": process.poll() is None, "process_reaped": False}
    if result["attempted"]:
        if os.name == "nt":
            # The live Popen handle pins this PID. Do not select processes by
            # executable name; that could include unrelated browser instances.
            try:
                killed = subprocess.run(
                    ["taskkill", "/PID", str(process.pid), "/T", "/F"],
                    capture_output=True, text=True, timeout=10)
                result["tree_termination_exit"] = killed.returncode
                result["tree_termination_detail"] = (killed.stdout + killed.stderr)[-4096:]
            except (OSError, subprocess.TimeoutExpired) as error:
                result["tree_termination_error"] = type(error).__name__
        if process.poll() is None:
            try:
                process.kill()
            except OSError as error:
                result["termination_error"] = str(error)[:4096]
    try:
        process.wait(timeout=10)
        result["process_reaped"] = True
    except (subprocess.TimeoutExpired, OSError) as error:
        result["process_reaped"] = False
        result["wait_error"] = type(error).__name__
    return result


def run_recorded(command: list[str], stdout: Path, stderr: Path,
                 timeout: float = 45) -> dict:
    """Keep complete output files and return even when launch/timeout fails.

    Waiting on the process handle, rather than communicate() on a pipe, avoids
    waiting for a descendant that still holds an inherited output handle.
    Callers must persist the returned failure before asserting success.
    """
    started = time.monotonic()
    result = {"state": "starting", "exit_code": None, "timed_out": False,
              "stdout_file": stdout.name, "stderr_file": stderr.name}
    process = None
    with stdout.open("w", encoding="utf-8") as out, stderr.open("w", encoding="utf-8") as err:
        try:
            process = subprocess.Popen(command, stdout=out, stderr=err)
            result["state"] = "running"
            result["pid"] = process.pid
            try:
                result["exit_code"] = process.wait(timeout=timeout)
                result["state"] = "exited"
            except subprocess.TimeoutExpired:
                result["timed_out"] = True
                result["state"] = "timeout"
                result["termination"] = terminate_owned_process(process)
                result["exit_code"] = process.poll()
        except OSError as error:
            result["state"] = "start-error" if process is None else "process-error"
            result["error_type"] = type(error).__name__
            result["error"] = str(error)[:4096]
        finally:
            if process is not None and process.poll() is None:
                result["termination"] = terminate_owned_process(process)
            result["elapsed_seconds"] = time.monotonic() - started
    return result


def output_tail(path: Path, limit: int = 8192) -> str:
    with path.open("rb") as handle:
        handle.seek(max(0, path.stat().st_size - limit))
        return handle.read(limit).decode("utf-8", errors="replace")
