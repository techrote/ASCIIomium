# Building ASCIIomium

Issue #2 establishes a deliberately small Windows x64 bootstrap: a C++20 executable links to the real CEF runtime, reports the compile-time and runtime CEF/Chromium versions, and does **not** initialise a browser yet.

## Pinned dependency

ASCIIomium currently pins the official CEF Windows x64 **standard binary distribution**:

```text
CEF:       151.3.17+gf059e67+chromium-151.0.7922.138
Chromium:  151.0.7922.138
Platform:  windows64
Archive:   cef_binary_151.3.17+gf059e67+chromium-151.0.7922.138_windows64.tar.bz2
Source:    https://cef-builds.spotifycdn.com/
```

The version is intentionally explicit rather than `latest`. CMake downloads the matching upstream `.sha1` sidecar, verifies the archive against it, and extracts the distribution under `third_party/cef/`. That directory is generated dependency state and is not committed.

The CEF automated-builds page identified this build as the preferred current stable Windows 64-bit build when issue #2 was implemented on 2026-09-13.

## Prerequisites

- Windows 10 or Windows 11 x64;
- Visual Studio 2022 with the Desktop development with C++ workload / MSVC x64 tools;
- a Windows 10/11 SDK installed by Visual Studio;
- CMake 3.21 or newer;
- enough free disk space for the CEF archive, extracted distribution, and build output.

CEF's upstream project currently documents Visual Studio 2022 on Windows 10 or newer as the supported Windows build environment for binary-distribution clients.

## Configure

From a normal PowerShell or Developer PowerShell prompt:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
```

The first configure downloads and verifies the pinned CEF distribution. Later configures reuse the extracted files.

If the exact pinned distribution is already available locally, bypass downloading it:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DASCIIOMIUM_CEF_ROOT="D:\deps\cef_binary_151.3.17+gf059e67+chromium-151.0.7922.138_windows64"
```

The supplied directory is validated for the required headers plus Debug and Release `libcef.dll`/`libcef.lib` files.

## Build and test Debug

```powershell
cmake --build build --config Debug --target asciiomium asciiomium_tests --parallel
ctest --test-dir build -C Debug --output-on-failure
.\build\bin\DEBUG\asciiomium.exe --version
```

## Build and test Release

```powershell
cmake --build build --config Release --target asciiomium asciiomium_tests --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\bin\RELEASE\asciiomium.exe --version
```

`--version` loads the staged `libcef.dll` and queries CEF's exported runtime version API. A successful build therefore checks more than header availability: it verifies that the executable links to and can load the pinned CEF runtime.

Expected shape:

```text
ASCIIomium 0.1.0
CEF compile: 151.3.17+gf059e67+chromium-151.0.7922.138
CEF runtime: 151.3.17
Chromium runtime: 151.0.7922.138
Build: Debug
Architecture: x64
CEF ABI/header match: yes
```

The Release output differs only in the build configuration line.

## What issue #2 intentionally does not stage

Only `libcef.dll` is copied beside the bootstrap executable because this issue merely calls CEF version exports. The complete browser runtime payload—resources, locales, graphics DLLs, subprocess handling, and packaging—is introduced when browser initialisation is implemented and later formalised by the packaging issue.

This keeps the bootstrap inert and makes accidental hidden browser startup impossible.

## CI

`.github/workflows/bootstrap-windows.yml` performs the same configure, Debug build/test/version run, and Release build/test/version run on `windows-2022`. The workflow caches the exact pinned CEF directory between runs; the first download still validates the upstream SHA-1 sidecar.

## Troubleshooting

### Wrong generator or architecture

Use Visual Studio 2022 with `-A x64`. The current bootstrap rejects non-Windows, non-MSVC, and 32-bit configurations explicitly.

### Interrupted CEF download

Delete `third_party/cef/cef_binary_151.3.17+gf059e67+chromium-151.0.7922.138_windows64.tar.bz2` and rerun configure. CMake will fetch and hash-check it again.

### Existing local CEF tree rejected

`ASCIIOMIUM_CEF_ROOT` must point at the extracted distribution root containing `include/`, `Debug/`, and `Release/`, not at its parent directory.
