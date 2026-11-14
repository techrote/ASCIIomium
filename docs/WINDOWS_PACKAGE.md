# Portable Windows qualification package (#17)

This branch adds an unsigned Windows x64 development package. Automated package
qualification and physical Windows Terminal acceptance are separate gates; no
release or v0.1 tag is created. The direct-executable CEF sandbox exception is
unchanged and remains a release decision. ASCIIomium's repository currently has
no project-owned LICENSE file; this package does not invent a license grant.

## Runtime and paths

`asciiomium_live.exe` is the browser. `browse.cmd` locates it using `%~dp0` and
forwards arguments. `asciiomium.exe` remains the version/terminal diagnostics
program. Both executables now print the same source commit/tree/dirty state,
compiler, CEF/Chromium, configuration, architecture and fixture-manifest hash.

Fixture lookup uses `fixtures/web` beside the executable, with no source-tree or
working-directory fallback. Normal development builds stage the same bundle.
The browser's profile rule still rejects the application directory. A source
checkout is detected from executable ancestors for development-only source-tree
protection; no build-machine drive/path is required in the portable browser.
Default persistent and private roots remain outside the package.

## Build from exact, clean source

After the normal Debug/Release build and tests:

```powershell
python tools/package_windows.py `
  --binaries build/bin/RELEASE `
  --cef-archive 'third_party/cef/cef_binary_151.3.17+gf059e67+chromium-151.0.7922.138_windows64.tar.bz2' `
  --output build/package --expected-revision (git rev-parse HEAD)
```

Python 3.10+ is required for package tooling. CMake supplies the C++ build.
The builder refuses dirty tracked or nonignored untracked source, mismatched
binary source/fixture identity, non-Release/x64 binaries, CEF ABI mismatch and
source changes while packaging. Build identity refreshes before every BuildInfo
compilation; this shared prerequisite matches the #15 implementation, without
pulling the telemetry feature branch into #17.

`cmake/cef-lock.json` contains the reviewed pin and official archive SHA-1.
AcquireCEF requires the fetched sidecar to agree before extraction and validates
the retained archive on cache hits. The package builder independently rechecks
the archive and retained sidecar, records its measured SHA-256, and streams fresh
payload bytes from that archive. An extracted CEF cache is not package authority.

## Exact inventory

| Area | Included payload |
| --- | --- |
| Application | `asciiomium.exe`, `asciiomium_live.exe`, launcher, instructions, verifier, lock and provenance |
| CEF DLLs | `chrome_elf`, `d3dcompiler_47`, `libcef`, `libEGL`, `libGLESv2`, `vk_swiftshader`, `vulkan-1`, `dxil`, `dxcompiler` |
| CEF data/resources | V8 snapshot, SwiftShader ICD JSON, 100/200-percent PAKs, resources PAK, ICU data |
| Locales | Exact complete locale file inventory enumerated from the pinned archive |
| Fixtures | Eight known HTML/CSS/JS files plus the versioned Git-blob manifest |
| Notices | Unmodified CEF `LICENSE.txt` and Chromium/third-party `CREDITS.html` under `licenses/` |

Debug binaries, tests, import libraries, build output, arbitrary files, logs,
profiles and caches are excluded. There is no separate subprocess executable in
this direct-executable architecture: CEF child processes reuse the live binary.

`SHA256SUMS.json` records every payload file's size and SHA-256 except itself.
`CUSTODY.json`, outside the ZIP, records the ZIP, manifest, provenance and verifier
digests, source commit/tree and Actions run identity. The ZIP has sorted files,
fixed permissions and source-commit UTC timestamps. The same payload with the
same Python/zlib implementation reproduces the same ZIP; this does not promise
reproducible MSVC executable bytes across independent builds or toolchains.

Checksums prove consistency against trusted custody/source records, not signing
or authenticity when every record is replaced together. Source PRs and exact-head
workflow records are the review authority. No signing key is used.

## Verification and clean extraction

The portable verifier rejects missing/extra files, altered bytes, partial
dependency proof, invalid fixture schema or hashes, links/reparse points,
duplicates and Windows reserved/ambiguous paths. It validates all paths before
extracting files, with bounded member count and total uncompressed size.

The dependent `package-clean-extraction` Windows job downloads only the produced
artifact. It has no checkout or dependency cache. After checking external custody
hashes, it extracts to a new temporary directory and runs both version commands,
then private static and animated browser sessions from an unrelated working
directory. It checks fixture loading, rendering, repeated motion paints, CEF
shutdown/private cleanup and an unchanged package inventory. Removing `libcef.dll`
and regenerating the ordinary manifest must still fail required-file coverage.

Every smoke subprocess records bounded stdout/stderr/exit/timeout evidence.
Success and failure reports, including partial fixture JSON, are saved before
temporary extraction removal; custody is copied before the smoke starts. Actions
uploads the ZIP/custody and extraction report separately. Preserve downloaded
artifact bytes and digest-checked custody outside Actions retention when recording
the qualification checkpoint.

Local portable controls:

```text
python tests/package_inventory_test.py
```

These validate tooling and negative controls; only hosted Windows execution
qualifies the actual PE/CEF payload. Physical display, keyboard/mouse, command
row, resize/restoration, representative real websites and the interactive
30-minute soak remain human acceptance gates.

## Dependency order

This branch depends on #13 / PR #40 for profiles, policy and lifecycle behaviour.
It is not a combined #14/#15 feature branch. Keep the package PR draft while its
runtime dependency or package CI is incomplete. Rebuild and requalify after any
source integration; never relabel an older ZIP with a newer commit SHA.
