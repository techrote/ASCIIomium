# Building ASCIIomium

ASCIIomium currently has two verified bootstrap layers: the pinned CEF/C++20 foundation from issue #2 and the Windows terminal ownership/diagnostics runtime from issue #3. CEF is linked and version-checked, but no browser is initialised yet.

## Pinned dependency

ASCIIomium currently pins the official CEF Windows x64 **standard binary distribution**:

```text
CEF:       151.3.17+gf059e67+chromium-151.0.7922.138
Chromium:  151.0.7922.138
Platform:  windows64
Archive:   cef_binary_151.3.17+gf059e67+chromium-151.0.7922.138_windows64.tar.bz2
SHA-1:     f21afbaaeb82c02a9e13dbb012c6fd0b2f11f005
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

The repository CI acceptance environment currently reports CMake 3.31.6, MSVC 19.44.35228.0 and Windows SDK 10.0.26100.0. Those are evidence of the tested environment, not additional hard-coded minimums.

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

The supplied directory is validated for the required headers plus Debug and Release runtime/import-library files.

## Build and test Debug

```powershell
cmake --build build --config Debug --target asciiomium asciiomium_tests terminal_logic_tests terminal_console_smoke --parallel
ctest --test-dir build -C Debug --output-on-failure
.\build\bin\DEBUG\asciiomium.exe --version
```

## Build and test Release

```powershell
cmake --build build --config Release --target asciiomium asciiomium_tests terminal_logic_tests terminal_console_smoke --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\bin\RELEASE\asciiomium.exe --version
```

`ctest` now covers the CEF build-info smoke test, pure terminal logic, and a real Win32 console acquire/restore smoke test with ten lifecycle cycles.

`--version` loads the staged CEF runtime and queries CEF's exported runtime version API. A successful build therefore checks more than header availability: it verifies that the executable links to and can load the pinned CEF runtime.

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

## Terminal diagnostics

Run this from Windows Terminal after building:

```powershell
.\build\bin\DEBUG\asciiomium.exe --terminal-diagnostics
```

Press `Ctrl+C` to exit. The command enters the alternate screen, hides the cursor, shows live geometry, RGB bars and Unicode half blocks, and then restores the previous terminal state on exit.

Useful variants:

```powershell
# Automatically exit after three seconds
.\build\bin\DEBUG\asciiomium.exe --terminal-diagnostics --duration-ms 3000

# One-shot output without entering or clearing the alternate screen
.\build\bin\DEBUG\asciiomium.exe --terminal-diagnostics --no-alt-screen
```

See [`TERMINAL_RUNTIME.md`](TERMINAL_RUNTIME.md) for ownership, resize and teardown details.

## Runtime DLL staging

The Windows loader resolves CEF side-by-side DLL dependencies before `main()` even though the current executable only calls version exports. The bootstrap therefore stages the DLL set declared by the pinned CEF release for Windows x64:

```text
chrome_elf.dll
d3dcompiler_47.dll
libcef.dll
libEGL.dll
libGLESv2.dll
vk_swiftshader.dll
vulkan-1.dll
dxil.dll
dxcompiler.dll
```

This does **not** initialise a browser and does not yet stage browser resources such as `.pak` files, locales, `icudtl.dat`, snapshot data, subprocess policy, or packaged application metadata. Those enter when browser initialisation and final packaging require them.

## CI

`.github/workflows/bootstrap-windows.yml` performs configure, Debug build/test/version, and Release build/test/version runs on `windows-2022`. The workflow caches the exact pinned CEF directory between runs; the first download still validates the upstream SHA-1 sidecar.

The Windows Terminal GUI-specific appearance of diagnostic output remains a manual acceptance check because GitHub-hosted Windows runners do not provide the Windows Terminal UI. The Win32 state lifecycle itself is exercised in CI by `terminal_console_smoke`.

## Troubleshooting

### Wrong generator or architecture

Use Visual Studio 2022 with `-A x64`. The current bootstrap rejects non-Windows, non-MSVC, and 32-bit configurations explicitly.

### Interrupted CEF download

Delete `third_party/cef/cef_binary_151.3.17+gf059e67+chromium-151.0.7922.138_windows64.tar.bz2` and rerun configure. CMake will fetch and hash-check it again.

### Existing local CEF tree rejected

`ASCIIOMIUM_CEF_ROOT` must point at the extracted distribution root containing `include/`, `Debug/`, and `Release/`, not at its parent directory.

### Terminal diagnostics report no console handles

Run ASCIIomium inside Windows Terminal, `conhost`, or another environment that exposes Windows console handles. Redirecting stdin/stdout to arbitrary files or pipes is not the interactive terminal path.
