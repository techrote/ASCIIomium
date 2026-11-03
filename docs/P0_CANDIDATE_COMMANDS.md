# P0 candidate command appendix

These PowerShell commands are scoped to the exact unmerged sources below.
They match the candidate CMake targets, CLI parsers and qualification scripts;
execution results belong to the [checkpoint](P0_CHECKPOINT_2026-11-03.md).
The #16 revision has a confirmed native Unicode test failure. Its commands are
reproduction instructions, not a passing qualification claim.

| Capability | Branch and exact source | Scope |
| --- | --- | --- |
| #13 browser runtime | `issue-13-browser-runtime` / `88195b12f97ffb433aa57ae9afcc7949355ff36e` | Profiles, commands and browser policy; no #14/#15/#16 additions |
| #14 differential output | `issue-14-differential-output` / `1272351b03502e7600c1229da8a50bfa4d433bdb` | Opt-in emitter and paired serialization; no #13 profile/command options |
| #15 structured telemetry | `issue-15-structured-telemetry` / `b041201ed6d06546eb93eda368d3d06284b6321c` | Stacked on #14; adds live `--benchmark` and telemetry, not #13/#16 |
| #16 lifecycle | `issue-16-terminal-lifecycle` / `f321cae8e18a2a791479e0f07b377d775fddd213` | Stacked on #13; native/input/callback/shutdown tests; no #14/#15 |
| #17 portable package | `issue-17-portable-windows` / `36d11ec954fb86051a6240739c23a4a0e120ba38` | Qualified #13 package; no #14/#15/#16 additions |

## Clean source and configuration

Use the Windows build prerequisites in [BUILDING.md](BUILDING.md), plus Python
3.10 or newer for the Python harness/package commands. Create a separate clean
checkout/build directory for each row; do not reuse binaries
from another candidate. For example, from an existing repository with the
candidate commit available locally:

```powershell
git worktree add --detach ..\ASCIIomium-verify-13 88195b12f97ffb433aa57ae9afcc7949355ff36e
if ($LASTEXITCODE -ne 0) { throw 'Cannot create candidate worktree' }
Set-Location ..\ASCIIomium-verify-13
git rev-parse HEAD
git status --porcelain
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
```

For another row, use its full commit and a fresh worktree name in the first
two commands. Confirm the printed revision matches and status is empty before
building. Build/evidence paths below are within the ignored `build` directory.
Keep downloaded package evidence and any profiles outside the package itself.

CTest commands have a 120-second fallback; tighter per-test limits take
precedence. Fixture runs use positive bounded durations and controlled local
pages. The pinned workflows remain the authority for the complete Debug/Release
job, including their outer job deadlines and artifact collection. These focused
commands do not replace that full job or physical Windows Terminal acceptance.

## #13 — Profiles, navigation and policy

At `88195b12f97ffb433aa57ae9afcc7949355ff36e`:

```powershell
cmake --build build --config Release --target asciiomium_live browser_commands_tests profile_session_tests private_process_tests asciiomium_runtime_probe --parallel
if ($LASTEXITCODE -ne 0) { throw 'Runtime build failed' }
ctest --test-dir build -C Release --output-on-failure --timeout 120 -R '^(browser_commands|profile_session|private_process)$'
if ($LASTEXITCODE -ne 0) { throw 'Runtime unit tests failed' }
python tests/verify_browser_runtime.py --exe build/bin/RELEASE/asciiomium_runtime_probe.exe --output build/browser-runtime-evidence.json
if ($LASTEXITCODE -ne 0) { throw 'CEF runtime qualification failed' }
```

The harness owns disposable profile roots and loopback HTTP/HTTPS fixtures. It
checks storage restart/isolation, ownership, navigation, policy outcomes,
download bytes and cleanup. Preserve the JSON and per-case reports. See the
[pinned runtime instructions](https://github.com/techrote/ASCIIomium/blob/88195b12f97ffb433aa57ae9afcc7949355ff36e/docs/BROWSER_RUNTIME.md)
and [full workflow](https://github.com/techrote/ASCIIomium/blob/88195b12f97ffb433aa57ae9afcc7949355ff36e/.github/workflows/bootstrap-windows.yml).

## #14 — Differential reference equivalence and byte measurements

At `1272351b03502e7600c1229da8a50bfa4d433bdb`:

```powershell
cmake --build build --config Release --target asciiomium_live differential_frame_emitter_tests live_runtime_tests asciiomium_differential_benchmark --parallel
if ($LASTEXITCODE -ne 0) { throw 'Differential build failed' }
ctest --test-dir build -C Release --output-on-failure --timeout 120 -R '^(differential_frame_emitter|live_runtime)$'
if ($LASTEXITCODE -ne 0) { throw 'Differential tests failed' }
.\tools\verify_differential_emission.ps1 -BinaryDirectory build/bin/RELEASE -OutputDirectory build/differential-evidence
if ($LASTEXITCODE -ne 0) { throw 'Differential qualification failed' }
```

The script uses 240 bounded synthetic samples, four-second static/motion CEF
cases and shorter repaint/diagnostic controls. Paired byte counts come from
identical logical frames; they are not terminal-write throughput. Preserve the
complete output directory. See [the pinned emitter protocol](https://github.com/techrote/ASCIIomium/blob/1272351b03502e7600c1229da8a50bfa4d433bdb/docs/DIFFERENTIAL_OUTPUT.md).

## #15 — Structured telemetry

At `b041201ed6d06546eb93eda368d3d06284b6321c`:

```powershell
cmake --build build --config Release --target asciiomium_live runtime_telemetry_tests asciiomium_telemetry_overhead --parallel
if ($LASTEXITCODE -ne 0) { throw 'Telemetry build failed' }
ctest --test-dir build -C Release --output-on-failure --timeout 120 -R '^(runtime_telemetry|build_identity_refresh)$'
if ($LASTEXITCODE -ne 0) { throw 'Telemetry tests failed' }
.\tools\verify_runtime_telemetry.ps1 -BinaryDirectory build/bin/RELEASE -OutputDirectory build/telemetry-evidence
if ($LASTEXITCODE -ne 0) { throw 'Telemetry qualification failed' }
```

The script runs six three-second CEF cases, checks invalid CLI combinations and
preserves static/motion full/differential reports with source/fixture identity.
Headless terminal-write measurements are null. See [the pinned measurement
definitions](https://github.com/techrote/ASCIIomium/blob/b041201ed6d06546eb93eda368d3d06284b6321c/docs/RUNTIME_TELEMETRY.md).
This branch's `--benchmark`/`--telemetry` flags must not be copied to #13, #16
or the current #17 package.

## #16 — Native console, callback and real CEF shutdown qualification

At `f321cae8e18a2a791479e0f07b377d775fddd213`, the recorded Debug run failed
`terminal_input_console`; all 24 other tests completed successfully. Preserve
failure output and stop on a failed configuration. A later repair must receive
its own source identity and results.

```powershell
foreach ($configuration in @('Debug', 'Release')) {
  cmake --build build --config $configuration --target terminal_lifecycle_tests terminal_input_tests terminal_input_console callback_boundary_tests osr_callback_tests source_frame_tests asciiomium_runtime_probe --parallel
  if ($LASTEXITCODE -ne 0) { throw "Lifecycle $configuration build failed" }
  ctest --test-dir build -C $configuration --output-on-failure --timeout 120 -R '^(terminal_lifecycle|terminal_input|terminal_input_console|callback_boundary|osr_callback|source_frame|runtime_process_evidence|build_identity_refresh)$' --output-log "$PWD/build/lifecycle-$configuration.log" --output-junit "$PWD/build/lifecycle-$configuration.xml"
  if ($LASTEXITCODE -ne 0) { throw "Lifecycle $configuration tests failed; retain build/lifecycle-$configuration.*" }
}
python tests/verify_browser_runtime.py --exe build/bin/RELEASE/asciiomium_runtime_probe.exe --output build/browser-runtime-evidence.json --lifecycle-cycles 10
if ($LASTEXITCODE -ne 0) { throw 'Real CEF lifecycle qualification failed' }
```

The final command also reruns browser-runtime cases. Its ten extra private
launches cover ready, before-created, queued navigation, held response and pending
resize twice. Preserve the summary, `build/runtime-probes/`, CTest logs and JUnit.
See [the source-pinned phase contract](https://github.com/techrote/ASCIIomium/blob/f321cae8e18a2a791479e0f07b377d775fddd213/docs/LIFECYCLE_PROBES.md)
and [native test scope](https://github.com/techrote/ASCIIomium/blob/f321cae8e18a2a791479e0f07b377d775fddd213/docs/TERMINAL_INPUT.md).
Neither this process matrix nor isolated native console tests prove parent-shell
usability or a physical 30-minute soak.

## #17 — Build a source-bound development package

At `36d11ec954fb86051a6240739c23a4a0e120ba38`, after the source's full build/test
qualification, the packaging commands are:

```powershell
cmake --build build --config Release --target asciiomium asciiomium_live --parallel
if ($LASTEXITCODE -ne 0) { throw 'Package binary build failed' }
python tests/package_inventory_test.py
if ($LASTEXITCODE -ne 0) { throw 'Package inventory controls failed' }
python tools/package_windows.py --binaries build/bin/RELEASE --cef-archive 'third_party/cef/cef_binary_151.3.17+gf059e67+chromium-151.0.7922.138_windows64.tar.bz2' --output build/package --expected-revision 36d11ec954fb86051a6240739c23a4a0e120ba38
if ($LASTEXITCODE -ne 0) { throw 'Package construction failed' }
```

Preserve the generated ZIP and its own `CUSTODY.json`; a new local build need
not have the recorded hosted binary/ZIP hashes. The [pinned clean-extraction
job](https://github.com/techrote/ASCIIomium/blob/36d11ec954fb86051a6240739c23a4a0e120ba38/.github/workflows/bootstrap-windows.yml)
performs the separate no-checkout/no-cache gate: verify external custody, then
run `verify_package.py --zip ... --expected-sha256 ... --expected-revision ...
--smoke --report ...`. Use that complete job's custody checks and produced
values. Its version/private static/motion subprocesses have bounded waits.

The [package instructions](https://github.com/techrote/ASCIIomium/blob/36d11ec954fb86051a6240739c23a4a0e120ba38/docs/WINDOWS_PACKAGE.md)
identify `asciiomium_live.exe`/`browse.cmd` as the browser and `asciiomium.exe` as
the diagnostics executable. This unsigned development package retains the
documented sandbox/license gates and does not contain differential-emitter,
structured-telemetry or new lifecycle/input options from the other candidates.

## Evidence and outstanding acceptance

Record the exact source/tree, configuration, commands, exit results and artifact
digests for every new run. Use the [physical procedure](P0_CHECKPOINT_2026-11-03.md#physical-qualification-instructions)
for visual/interaction/restoration evidence and the actual 30-minute soak.
Final integration requires fresh automated and package qualification of the
combined source; no component result transfers automatically to it.
