"""Portable negative controls for package custody/inventory and extraction."""
from pathlib import Path
import hashlib
import io
import json
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_package import (FIXTURE_FILES, NOTICES, PACKAGE_FILES, digest, extract,
                            safe_name, verify, write_manifest)
from package_windows import normalized_zip, verified_dependency_payload

SOURCE = Path(__file__).resolve().parents[1]
REVISION = "1" * 40


def write_json(path, value):
    path.write_text(json.dumps(value), encoding="utf-8")


class PackageInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="asciiomium-package-test-")
        self.root = Path(self.temp.name) / "package"
        self.root.mkdir()
        self.lock = json.loads((SOURCE / "cmake/cef-lock.json").read_text())
        deps = set(self.lock["runtime_dlls"] + self.lock["runtime_data"] + self.lock["resources"]) | NOTICES | {"locales/en-US.pak"}
        for name in PACKAGE_FILES | deps:
            p = self.root / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"test-only placeholder: " + name.encode())
        shutil.copytree(SOURCE / "fixtures/web", self.root / "fixtures/web")
        write_json(self.root / "cef-lock.json", self.lock)
        self.provenance = {"schema": "asciiomium-package-provenance-v1",
                           "source": {"revision": REVISION, "tree": "2" * 40, "dirty": False},
                           "cef": {"version": self.lock["version"], "platform": self.lock["platform"],
                                   "chromium": self.lock["chromium"], "archive_sha1": self.lock["archive_sha1"],
                                   "archive_sha256": "3" * 64, "locales": ["locales/en-US.pak"],
                                   "payload": {n: digest(self.root / n) for n in deps}}}
        self.refresh()

    def tearDown(self):
        self.temp.cleanup()

    def refresh(self):
        write_json(self.root / "PROVENANCE.json", self.provenance)
        write_manifest(self.root)

    def test_valid_consistency_and_exact_revision(self):
        self.assertEqual(verify(self.root, REVISION)["source"]["revision"], REVISION)
        with self.assertRaisesRegex(ValueError, "unexpected package source"):
            verify(self.root, "4" * 40)

    def test_tamper_and_unexpected_files(self):
        (self.root / "libcef.dll").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            verify(self.root)
        self.refresh()
        with self.assertRaisesRegex(ValueError, "archive-matched"):
            verify(self.root)
        (self.root / "debug.log").write_text("unexpected")
        self.refresh()
        with self.assertRaisesRegex(ValueError, "unexpected files"):
            verify(self.root)

    def test_missing_dependency_with_regenerated_manifest(self):
        (self.root / "libcef.dll").unlink()
        self.refresh()
        with self.assertRaisesRegex(ValueError, "unexpected files"):
            verify(self.root)

    def test_empty_or_partial_archive_proof(self):
        for payload in ({}, {"libcef.dll": digest(self.root / "libcef.dll")}):
            self.provenance["cef"]["payload"] = payload
            self.refresh()
            with self.assertRaisesRegex(ValueError, "payload inventory"):
                verify(self.root)

    def test_empty_or_corrupt_fixture_manifest(self):
        manifest = self.root / "fixtures/web/MANIFEST.json"
        write_json(manifest, {"files": {}})
        self.refresh()
        with self.assertRaisesRegex(ValueError, "fixture bundle identity"):
            verify(self.root)
        shutil.copyfile(SOURCE / "fixtures/web/MANIFEST.json", manifest)
        (self.root / "fixtures/web/flat-ui.html").write_text("different bytes")
        self.refresh()
        with self.assertRaisesRegex(ValueError, "fixture Git-blob"):
            verify(self.root)

    def test_unsafe_windows_paths(self):
        for name in ("../escape", "/absolute", "C:/escape", "a\\b", "a//b", "CON", "AUX.txt", "NUL", "COM1.bin", "LPT².txt", "foo.", "foo ", "a/../b", "bad?name", "nul\0byte", "folder/"):
            self.assertFalse(safe_name(name), name)
        for name in ("locales/en-US.pak", "licenses/CEF-CREDITS.html", "fixtures/web/common.js"):
            self.assertTrue(safe_name(name), name)

    def test_zip_rejects_duplicates_traversal_and_ancestor_conflicts(self):
        for names in (("../escape",), ("CON.txt",), ("A.txt", "a.txt"), ("parent", "parent/child"), ("folder/",)):
            archive = Path(self.temp.name) / "bad.zip"
            with zipfile.ZipFile(archive, "w") as handle:
                for name in names:
                    handle.writestr(name, b"x")
            destination = Path(self.temp.name) / "extract"
            destination.mkdir(exist_ok=True)
            with self.assertRaises(ValueError):
                extract(archive, destination)
            self.assertEqual(list(destination.iterdir()), [])

    def test_normalized_zip_reproduces_identical_payload(self):
        a, b = Path(self.temp.name) / "a.zip", Path(self.temp.name) / "b.zip"
        normalized_zip(self.root, a, 1700000000)
        normalized_zip(self.root, b, 1700000000)
        self.assertEqual(digest(a), digest(b))
        destination = Path(self.temp.name) / "extract"
        destination.mkdir()
        extract(a, destination)
        verify(destination, REVISION)

    def test_failed_verification_preserves_report(self):
        (self.root / "libcef.dll").unlink()
        output = Path(self.temp.name) / "failure.json"
        process = subprocess.run([sys.executable, str(SOURCE / "tools/verify_package.py"),
                                  "--directory", str(self.root), "--report", str(output)],
                                 capture_output=True, text=True)
        self.assertNotEqual(process.returncode, 0)
        result = json.loads(output.read_text())
        self.assertFalse(result["success"])
        self.assertFalse(result["inventory_verified"])
        self.assertEqual(result["failure"]["type"], "ValueError")

    def test_dependency_archive_uses_exact_verified_bytes(self):
        archive = Path(self.temp.name) / "cef-test.tar.bz2"
        distribution = "cef_binary_" + self.lock["version"] + "_" + self.lock["platform"]
        members = {"Release/" + n: b"runtime " + n.encode() for n in self.lock["runtime_dlls"] + self.lock["runtime_data"]}
        members.update({"Resources/" + n: b"resource " + n.encode() for n in self.lock["resources"]})
        members.update({"LICENSE.txt": b"CEF license", "CREDITS.html": b"Chromium notices", "Resources/locales/en-US.pak": b"locale",
                        "include/cef_version.h": ('#define CEF_VERSION "' + self.lock["version"] + '"\n#define CEF_COMMIT_HASH "' + self.lock["commit_prefix"] + '"\n').encode(),
                        "Debug/unwanted.dll": b"excluded", "README.txt": b"excluded"})
        with tarfile.open(archive, "w:bz2") as handle:
            for name, body in members.items():
                entry = tarfile.TarInfo(distribution + "/" + name)
                entry.size = len(body)
                handle.addfile(entry, io.BytesIO(body))
        test_lock = dict(self.lock, archive_sha1=hashlib.sha1(archive.read_bytes()).hexdigest())
        archive.with_name(archive.name + ".sha1").write_text(test_lock["archive_sha1"])
        destination = Path(self.temp.name) / "dependency"
        destination.mkdir()
        result = verified_dependency_payload(archive, test_lock, destination)
        self.assertEqual((destination / "libcef.dll").read_bytes(), members["Release/libcef.dll"])
        self.assertEqual(result["archive_sha256"], digest(archive))
        self.assertFalse((destination / "Debug").exists())
        self.assertEqual(set(result["payload"]), set(self.provenance["cef"]["payload"]))
        with self.assertRaisesRegex(ValueError, "checksum"):
            verified_dependency_payload(archive, self.lock, destination)


if __name__ == "__main__":
    unittest.main()
