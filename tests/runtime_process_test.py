#!/usr/bin/env python3
"""Exercise failed-process evidence without pretending to execute Chromium."""
from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from runtime_process import output_tail, run_recorded
import verify_browser_runtime as runtime


class RecordedProcessTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="asciiomium-process-evidence-")
        self.root = Path(self.temp.name)
        self.out, self.err = self.root / "stdout.txt", self.root / "stderr.txt"

    def tearDown(self):
        self.temp.cleanup()

    def run_python(self, code, timeout=10):
        return run_recorded([sys.executable, "-c", code], self.out, self.err, timeout)

    def test_success_and_bounded_tail(self):
        result = self.run_python("import sys; print('x'*20000); print('diagnostic',file=sys.stderr)")
        self.assertEqual(result["exit_code"], 0)
        self.assertEqual(result["state"], "exited")
        self.assertFalse(result["timed_out"])
        self.assertEqual(self.out.read_bytes().rstrip(b"\r\n"), b"x" * 20000)
        self.assertLessEqual(len(output_tail(self.out, 64)), 64)
        self.assertIn("diagnostic", output_tail(self.err))

    def test_nonzero_preserves_partial_evidence(self):
        result = self.run_python("import sys; print('partial report'); print('failed stage',file=sys.stderr); sys.exit(23)")
        self.assertEqual(result["exit_code"], 23)
        self.assertIn("partial report", self.out.read_text())
        self.assertIn("failed stage", self.err.read_text())

    def test_timeout_is_recorded_and_process_reaped(self):
        result = self.run_python("import time; print('before wait',flush=True); time.sleep(30)", .3)
        self.assertEqual(result["state"], "timeout")
        self.assertTrue(result["timed_out"])
        self.assertTrue(result["termination"]["process_reaped"])
        self.assertIsNotNone(result["exit_code"])
        self.assertLess(result["elapsed_seconds"], 15)
        self.assertTrue(self.out.exists() and self.err.exists())

    def test_missing_executable_is_a_retained_result(self):
        result = run_recorded([str(self.root / "missing-program")], self.out, self.err, 1)
        self.assertEqual(result["state"], "start-error")
        self.assertIsNone(result["exit_code"])
        self.assertEqual(result["error_type"], "FileNotFoundError")
        self.assertTrue(self.out.exists() and self.err.exists())

    def test_missing_probe_retains_suite_setup_failure(self):
        report = self.root / "missing-probe-summary.json"
        result = subprocess.run([sys.executable, str(Path(runtime.__file__)),
            "--exe", str(self.root / "missing-probe.exe"), "--output", str(report)],
            capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        evidence = json.loads(report.read_text())
        self.assertEqual(evidence["state"], "failed")
        self.assertEqual(evidence["error"]["type"], "FileNotFoundError")
        self.assertEqual(evidence["results"], [])
        self.assertIsNone(evidence["binary"]["sha256"])

    def test_loopback_bind_failure_retains_suite_setup_failure(self):
        report = self.root / "bind-failure-summary.json"
        with mock.patch.object(sys, "argv", ["verify_browser_runtime.py", "--exe", sys.executable,
                                           "--output", str(report)]), \
             mock.patch.object(runtime, "FixtureServer", side_effect=OSError("injected bind failure")):
            with self.assertRaisesRegex(OSError, "injected bind failure"):
                runtime.main()
        evidence = json.loads(report.read_text())
        self.assertEqual(evidence["state"], "failed")
        self.assertEqual(evidence["error"]["type"], "OSError")
        self.assertEqual(evidence["http_requests"], {})
        self.assertEqual(evidence["results"], [])


if __name__ == "__main__":
    unittest.main()
