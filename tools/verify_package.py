#!/usr/bin/env python3
"""Standalone portable-package inventory and clean-extraction verifier."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tempfile
import zipfile

FIXTURE_FILES = {"common.css", "common.js", "index.html", "flat-ui.html", "colour-ramps.html",
                 "raster-imagery.html", "motion-scroll.html", "input-focus.html"}
PACKAGE_FILES = {"asciiomium.exe", "asciiomium_live.exe", "browse.cmd", "README.txt",
                 "PROVENANCE.json", "cef-lock.json", "verify_package.py"}
NOTICES = {"licenses/CEF-LICENSE.txt", "licenses/CEF-CREDITS.html"}


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def safe_name(name: str) -> bool:
    if not isinstance(name, str):
        return False
    path = PurePosixPath(name)
    if not name or path.is_absolute() or re.search(r'[\\:<>"|?*\x00-\x1f\x7f]', name):
        return False
    for part in name.split("/"):
        if part in (".", "..", "") or part[-1:] in (".", " "):
            return False
        stem = part.split(".", 1)[0].upper()
        if stem in {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"} or re.fullmatch(r"(?:COM|LPT)[1-9¹²³]", stem):
            return False
    return True


def inventory(directory: Path) -> dict[str, dict]:
    result = {}
    for path in sorted(directory.rglob("*")):
        info = path.lstat()
        if path.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("package contains link/reparse point")
        if path.is_file():
            name = path.relative_to(directory).as_posix()
            if not safe_name(name):
                raise ValueError("unsafe package filename")
            if name != "SHA256SUMS.json":
                result[name] = {"bytes": info.st_size, "sha256": digest(path)}
        elif not path.is_dir():
            raise ValueError("non-regular package member")
    return result


def verify(directory: Path, expected_revision: str | None = None) -> dict:
    manifest = json.loads((directory / "SHA256SUMS.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != "asciiomium-package-manifest-v1":
        raise ValueError("unsupported package manifest")
    files = manifest.get("files", {})
    if not files or not isinstance(files, dict) or not all(safe_name(name) for name in files) or len({n.casefold() for n in files}) != len(files):
        raise ValueError("unsafe/empty package inventory")
    if inventory(directory) != files:
        raise ValueError("package inventory or SHA-256 mismatch")
    provenance = json.loads((directory / "PROVENANCE.json").read_text(encoding="utf-8"))
    if provenance.get("schema") != "asciiomium-package-provenance-v1":
        raise ValueError("unsupported package provenance")
    source = provenance["source"]
    if source["dirty"] or not re.fullmatch(r"[0-9a-f]{40}", source["revision"]) or not re.fullmatch(r"[0-9a-f]{40}", source["tree"]):
        raise ValueError("package is not bound to a clean source revision")
    if expected_revision and source["revision"] != expected_revision:
        raise ValueError("unexpected package source revision")
    lock = json.loads((directory / "cef-lock.json").read_text(encoding="utf-8"))
    if lock.get("schema") != "asciiomium-cef-lock-v1" or lock.get("platform") != "windows64":
        raise ValueError("unsupported dependency lock")
    runtime = set(lock["runtime_dlls"] + lock["runtime_data"] + lock["resources"])
    if not runtime or any(not safe_name(n) or "/" in n for n in runtime) or "libcef.dll" not in runtime:
        raise ValueError("invalid dependency runtime inventory")
    fixture = json.loads((directory / "fixtures/web/MANIFEST.json").read_text(encoding="utf-8"))
    if fixture.get("schema") != "asciiomium-web-fixture-manifest-v1" or fixture.get("bundle_version") != "web-fixtures-v1" or fixture.get("hash_kind") != "git-blob-sha1" or set(fixture.get("files", {})) != FIXTURE_FILES:
        raise ValueError("fixture bundle identity/inventory invalid")
    for name, expected in fixture["files"].items():
        if not safe_name(name) or "/" in name:
            raise ValueError("unsafe fixture filename")
        body = (directory / "fixtures/web" / name).read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(body)).encode() + b"\0" + body).hexdigest()
        if actual != expected:
            raise ValueError("fixture Git-blob identity mismatch")
    cef = provenance["cef"]
    if cef["version"] != lock["version"] or cef["archive_sha1"] != lock["archive_sha1"] or cef["chromium"] != lock["chromium"] or cef["platform"] != lock["platform"] or not re.fullmatch(r"[0-9a-f]{64}", cef["archive_sha256"]):
        raise ValueError("dependency provenance differs from pinned lock")
    locales = set(cef["locales"])
    if not locales or "locales/en-US.pak" not in locales or any(not safe_name(n) or not n.startswith("locales/") for n in locales):
        raise ValueError("archive locales inventory invalid")
    dependencies = runtime | NOTICES | locales
    expected_dependencies = cef["payload"]
    if set(expected_dependencies) != dependencies:
        raise ValueError("dependency payload inventory is incomplete or unexpected")
    required = PACKAGE_FILES | dependencies | {"fixtures/web/" + n for n in FIXTURE_FILES | {"MANIFEST.json"}}
    if set(files) != required:
        raise ValueError("package contains missing or unexpected files")
    for name, source_hash in expected_dependencies.items():
        if name not in files or files[name]["sha256"] != source_hash:
            raise ValueError("dependency payload is not archive-matched")
    return provenance


def extract(archive: Path, directory: Path) -> None:
    with zipfile.ZipFile(archive) as handle:
        names = set()
        members = handle.infolist()
        if len(members) > 10000 or sum(m.file_size for m in members) > 4 * 1024**3:
            raise ValueError("package exceeds bounded extraction inventory")
        for member in members:
            name = member.filename
            mode = member.external_attr >> 16
            if not safe_name(name) or name.casefold() in names or member.is_dir() or stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG)):
                raise ValueError("unsafe/duplicate ZIP path")
            names.add(name.casefold())
        for name in names:
            if any(str(p) in names for p in PurePosixPath(name).parents if str(p) != "."):
                raise ValueError("ZIP file/directory ancestor conflict")
        for member in members:
            name = member.filename
            target = directory.joinpath(*PurePosixPath(name).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with handle.open(member) as source, target.open("xb") as destination:
                shutil.copyfileobj(source, destination, 1024 * 1024)


def recorded_run(command: list[str], outside: Path, timeout: int, evidence: dict) -> subprocess.CompletedProcess:
    entry = {"command": command, "timeout_seconds": timeout}
    evidence.setdefault("processes", []).append(entry)
    try:
        process = subprocess.run(command, cwd=outside, capture_output=True, text=True, timeout=timeout)
        entry.update(returncode=process.returncode, stdout=process.stdout[-16384:], stderr=process.stderr[-16384:])
        return process
    except subprocess.TimeoutExpired as error:
        def bounded_text(value):
            return (value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or "")[-16384:]
        entry.update(timed_out=True, stdout=bounded_text(error.stdout), stderr=bounded_text(error.stderr))
        raise


def smoke(directory: Path, outside: Path, provenance: dict, evidence: dict) -> None:
    before = inventory(directory)
    versions = evidence["versions"] = {}
    for binary in ("asciiomium.exe", "asciiomium_live.exe"):
        process = recorded_run([str(directory / binary), "--version"], outside, 20, evidence)
        if process.returncode or "CEF ABI/header match: yes" not in process.stdout or provenance["source"]["revision"] not in process.stdout or provenance["cef"]["version"] not in process.stdout:
            raise ValueError("extracted version/ABI/source check failed: " + binary + "\n" + process.stdout + process.stderr)
        versions[binary] = process.stdout.strip()
    results = evidence["fixtures"] = []
    for fixture, duration in (("flat-ui", "2200"), ("motion-scroll", "3000")):
        report = outside / (fixture + ".json")
        command = [str(directory / "asciiomium_live.exe"), "--private", "--fixture", fixture,
                   "--no-terminal", "--quiet", "--duration-ms", duration, "--report", str(report)]
        if fixture == "flat-ui":
            command += ["--freeze"]
        result_entry = {"fixture": fixture, "result": None}
        results.append(result_entry)
        try:
            process = recorded_run(command, outside, 45, evidence)
        finally:
            if report.is_file():
                try:
                    result_entry["result"] = json.loads(report.read_text(encoding="utf-8"))
                except (ValueError, OSError) as error:
                    result_entry["report_error"] = str(error)
        if process.returncode:
            raise ValueError("extracted browser smoke failed: " + process.stderr)
        result = result_entry["result"]
        if result is None:
            raise ValueError("extracted browser produced no valid report")
        if not (result["load"]["complete"] and result["runtime"]["closed_cleanly"] and result["browser"]["profile_cleanup"] and result["fixture_resolution"] == "executable-relative" and result["scheduler"]["rendered_frames"] > 0):
            raise ValueError("extracted runtime/fixture/cleanup evidence failed")
        if fixture == "motion-scroll" and result["source"]["view_paint_count"] < 10:
            raise ValueError("packaged animation did not paint repeatedly")
    if inventory(directory) != before:
        raise ValueError("browser wrote cache/profile/log data into package")
    # Check required-file coverage independently of ordinary checksum mismatch.
    required = directory / "libcef.dll"
    moved = outside / "libcef-negative.dll"
    required.rename(moved)
    original_manifest = (directory / "SHA256SUMS.json").read_bytes()
    try:
        write_manifest(directory)
        try:
            verify(directory)
        except ValueError:
            negative = True
        else:
            raise ValueError("missing runtime file was accepted")
    finally:
        moved.rename(required)
        (directory / "SHA256SUMS.json").write_bytes(original_manifest)
    verify(directory, provenance["source"]["revision"])
    evidence.update(missing_runtime_negative_control=negative, package_unchanged_after_smoke=True)


def write_manifest(directory: Path) -> None:
    (directory / "SHA256SUMS.json").write_text(json.dumps({
        "schema": "asciiomium-package-manifest-v1", "algorithm": "sha256", "files": inventory(directory)
    }, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path)
    parser.add_argument("--zip", type=Path)
    parser.add_argument("--expected-sha256")
    parser.add_argument("--expected-revision")
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if bool(args.directory) == bool(args.zip):
        parser.error("choose exactly one --directory or --zip")
    report = {"schema": "asciiomium-package-verification-v1", "source_revision": None,
              "inventory_verified": False, "physical_windows_terminal": False, "smoke": None,
              "success": False, "failure": None}
    with tempfile.TemporaryDirectory(prefix="asciiomium-extraction-") as tmp:
        root = Path(tmp)
        directory = args.directory.resolve() if args.directory else root / "package"
        if args.report and args.report.resolve().is_relative_to(directory):
            parser.error("--report must be outside the package inventory")
        try:
            if args.zip:
                if args.expected_sha256 and digest(args.zip) != args.expected_sha256.lower():
                    raise ValueError("ZIP custody digest mismatch")
                directory.mkdir()
                extract(args.zip, directory)
            provenance = verify(directory, args.expected_revision)
            report.update(source_revision=provenance["source"]["revision"], inventory_verified=True)
            if args.smoke:
                if os.name != "nt":
                    raise ValueError("real package smoke requires Windows")
                outside = root / "unrelated-working-directory"
                outside.mkdir()
                report["smoke"] = {}
                smoke(directory, outside, provenance, report["smoke"])
            report["success"] = True
        except Exception as error:
            report["failure"] = {"type": type(error).__name__, "message": str(error)}
            raise
        finally:
            # Persist failure and partial fixture/process evidence before the
            # disposable extraction directory is removed.
            if args.report:
                args.report.parent.mkdir(parents=True, exist_ok=True)
                args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"inventory_verified": True, "source_revision": provenance["source"]["revision"], "smoke_passed": report["smoke"] is not None}))


if __name__ == "__main__":
    main()
