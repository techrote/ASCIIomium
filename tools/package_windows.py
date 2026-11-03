#!/usr/bin/env python3
"""Build an unsigned, source-bound Windows qualification ZIP from an allowlist."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import urllib.parse
import zipfile

from verify_package import (FIXTURE_FILES, NOTICES, digest, safe_name, verify,
                            write_manifest)


def git(source: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(source), *args], text=True).strip()


def json_file(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def verified_dependency_payload(archive: Path, lock: dict, stage: Path) -> dict:
    """Read fresh bytes from the verified retained archive, never extracted cache."""
    sha1 = hashlib.sha1()
    sha256 = hashlib.sha256()
    with archive.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            sha1.update(block)
            sha256.update(block)
    if sha1.hexdigest() != lock["archive_sha1"]:
        raise ValueError("CEF archive does not match reviewed repository checksum")
    sidecar = archive.with_name(archive.name + ".sha1")
    if not sidecar.is_file() or sidecar.read_text(encoding="ascii").strip().lower() != lock["archive_sha1"]:
        raise ValueError("retained official CEF checksum sidecar differs from lock")
    distribution = "cef_binary_" + lock["version"] + "_" + lock["platform"]
    selected = {"Release/" + n: n for n in lock["runtime_dlls"] + lock["runtime_data"]}
    selected.update({"Resources/" + n: n for n in lock["resources"]})
    selected.update({"LICENSE.txt": "licenses/CEF-LICENSE.txt", "CREDITS.html": "licenses/CEF-CREDITS.html"})
    found = set()
    payload = {}
    locales = []
    version_header = None
    # Stream the standard distribution: no compiler headers, test binaries,
    # import libraries, debug payload or unrelated files enter the package.
    with tarfile.open(archive, "r|bz2") as handle:
        for member in handle:
            prefix = distribution + "/"
            if not member.name.startswith(prefix):
                if member.name.rstrip("/") == distribution and member.isdir():
                    continue
                raise ValueError("unexpected CEF archive top-level path")
            relative = member.name[len(prefix):]
            if member.isdir():
                continue
            target_name = selected.get(relative)
            if relative.startswith("Resources/locales/"):
                target_name = "locales/" + relative.removeprefix("Resources/locales/")
            is_header = relative == "include/cef_version.h"
            if not target_name and not is_header:
                continue
            if not member.isfile() or not safe_name(relative) or (target_name and not safe_name(target_name)):
                raise ValueError("unsafe/non-regular dependency member")
            if relative.casefold() in found:
                raise ValueError("duplicate dependency archive member")
            found.add(relative.casefold())
            stream = handle.extractfile(member)
            if stream is None:
                raise ValueError("unreadable dependency member")
            if is_header:
                if member.size > 65536:
                    raise ValueError("unexpected dependency version header size")
                version_header = stream.read().decode("utf-8")
                continue
            target = stage.joinpath(*PurePosixPath(target_name).parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with stream, target.open("xb") as output:
                shutil.copyfileobj(stream, output, 1024 * 1024)
            payload[target_name] = digest(target)
            if target_name.startswith("locales/"):
                locales.append(target_name)
    if not set(n.casefold() for n in selected).issubset(found):
        raise ValueError("verified dependency archive is missing required payload/notices")
    if not version_header or not re.search(r'^#define CEF_VERSION "' + re.escape(lock["version"]) + r'"\s*$', version_header, re.M):
        raise ValueError("archive CEF version does not match reviewed pin")
    commit = re.search(r'^#define CEF_COMMIT_HASH "([0-9a-f]+)"', version_header, re.M)
    if not commit or not commit.group(1).startswith(lock["commit_prefix"]):
        raise ValueError("archive CEF commit does not match pin")
    if "locales/en-US.pak" not in locales:
        raise ValueError("archive has no en-US locale")
    return {"version": lock["version"], "chromium": lock["chromium"], "platform": lock["platform"],
            "commit": commit.group(1), "archive_url": lock["url_base"] + "/" + urllib.parse.quote(archive.name),
            "archive_sha1": sha1.hexdigest(), "archive_sha256": sha256.hexdigest(),
            "locales": sorted(locales), "payload": dict(sorted(payload.items()))}


def normalized_zip(directory: Path, output: Path, epoch: int) -> None:
    stamp = datetime.fromtimestamp(max(epoch, 315532800), timezone.utc)
    timestamp = (stamp.year, stamp.month, stamp.day, stamp.hour, stamp.minute, stamp.second // 2 * 2)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as archive:
        for path in sorted(p for p in directory.rglob("*") if p.is_file()):
            name = path.relative_to(directory).as_posix()
            info = zipfile.ZipInfo(name, timestamp)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            with path.open("rb") as source, archive.open(info, "w", force_zip64=True) as target:
                shutil.copyfileobj(source, target, 1024 * 1024)


def build(source: Path, binaries: Path, archive: Path, output: Path, expected_revision: str | None) -> dict:
    if os.name != "nt":
        raise ValueError("application qualification package must be built on Windows")
    source, binaries, archive, output = (p.resolve() for p in (source, binaries, archive, output))
    if git(source, "status", "--porcelain"):
        raise ValueError("package requires a clean tracked and untracked source checkout")
    revision = git(source, "rev-parse", "HEAD")
    tree = git(source, "rev-parse", "HEAD^{tree}")
    if expected_revision and revision != expected_revision:
        raise ValueError("checkout differs from expected qualification revision")
    lock = json.loads((source / "cmake/cef-lock.json").read_text(encoding="utf-8"))
    # Both application targets must disclose the same clean source identity.
    versions = {}
    fixture_manifest_sha256 = digest(source / "fixtures/web/MANIFEST.json")
    for binary in ("asciiomium.exe", "asciiomium_live.exe"):
        result = subprocess.run([str(binaries / binary), "--version"], cwd=binaries,
                                capture_output=True, text=True, timeout=20, check=True)
        fields = dict(line.split(": ", 1) for line in result.stdout.splitlines() if ": " in line)
        if (fields.get("Source revision") != revision or fields.get("Source tree") != tree or
            fields.get("Source checkout dirty") != "no" or fields.get("Source identity") != "available" or
            fields.get("Fixture manifest SHA-256") != fixture_manifest_sha256 or
            fields.get("CEF compile") != lock["version"] or fields.get("Build", "").lower() != "release" or
            fields.get("Architecture") != "x64" or fields.get("CEF ABI/header match") != "yes"):
            raise ValueError("application binary identity/ABI differs from clean Release x64 source: " + binary)
        versions[binary] = result.stdout.strip()
    version = versions["asciiomium.exe"].splitlines()[0].removeprefix("ASCIIomium ")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("unexpected application version")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="asciiomium-package-stage-") as temporary:
        stage = Path(temporary)
        dependency = verified_dependency_payload(archive, lock, stage)
        for binary in versions:
            shutil.copyfile(binaries / binary, stage / binary)
        shutil.copyfile(source / "cmake/cef-lock.json", stage / "cef-lock.json")
        shutil.copyfile(source / "tools/verify_package.py", stage / "verify_package.py")
        fixture_dir = stage / "fixtures/web"
        fixture_dir.mkdir(parents=True)
        for name in sorted(FIXTURE_FILES | {"MANIFEST.json"}):
            shutil.copyfile(source / "fixtures/web" / name, fixture_dir / name)
        shutil.copyfile(source / "packaging/README.txt", stage / "README.txt")
        shutil.copyfile(source / "packaging/browse.cmd", stage / "browse.cmd")
        provenance = {"schema": "asciiomium-package-provenance-v1", "asciiomium_version": version,
                      "source": {"repository": "https://github.com/techrote/ASCIIomium", "revision": revision,
                                 "tree": tree, "dirty": False}, "cef": dependency,
                      "configuration": "Release", "architecture": "x64", "versions": versions,
                      "signed": False, "sandbox_enabled": False,
                      "qualification": "development candidate; automated and physical gates tracked separately",
                      "archive_reproducibility": "sorted files, fixed permissions and source-commit UTC timestamps; identical payload and Python/zlib required",
                      "compiler": fields["Compiler"], "cmake_version": subprocess.check_output(["cmake", "--version"], text=True).splitlines()[0],
                      "workflow": {"run_id": os.environ.get("GITHUB_RUN_ID"), "run_attempt": os.environ.get("GITHUB_RUN_ATTEMPT")}}
        json_file(stage / "PROVENANCE.json", provenance)
        write_manifest(stage)
        verify(stage, revision)
        name = "ASCIIomium-" + version + "-windows-x64-dev-" + revision[:12] + ".zip"
        result_zip = output / name
        normalized_zip(stage, result_zip, int(git(source, "show", "-s", "--format=%ct", "HEAD")))
        shutil.copyfile(stage / "PROVENANCE.json", output / "PROVENANCE.json")
        shutil.copyfile(stage / "SHA256SUMS.json", output / "SHA256SUMS.json")
        shutil.copyfile(stage / "verify_package.py", output / "verify_package.py")
        custody = {"schema": "asciiomium-package-custody-v1", "source_revision": revision,
                   "source_tree": tree, "zip_file": name, "zip_bytes": result_zip.stat().st_size,
                   "zip_sha256": digest(result_zip), "payload_manifest_sha256": digest(stage / "SHA256SUMS.json"),
                   "provenance_sha256": digest(stage / "PROVENANCE.json"),
                   "verifier_sha256": digest(stage / "verify_package.py"), "workflow": provenance["workflow"]}
        json_file(output / "CUSTODY.json", custody)
        (output / (name + ".sha256")).write_text(custody["zip_sha256"] + "  " + name + "\n", encoding="ascii")
    if git(source, "status", "--porcelain") or git(source, "rev-parse", "HEAD") != revision or git(source, "rev-parse", "HEAD^{tree}") != tree:
        raise ValueError("source checkout/revision changed during packaging")
    return custody


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--binaries", type=Path, required=True)
    parser.add_argument("--cef-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expected-revision")
    args = parser.parse_args()
    print(json.dumps(build(args.source, args.binaries, args.cef_archive, args.output, args.expected_revision), indent=2))


if __name__ == "__main__":
    main()
