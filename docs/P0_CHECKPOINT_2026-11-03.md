# ASCIIomium P0 implementation and acceptance checkpoint

Date: 3 November 2026. This is an evidence checkpoint for #18, not a release
declaration. The branch is documentation-only and does not integrate the feature
branches below. No feature PR has been merged, no v0.1 tag has been created, and
no GitHub Release has been published.

## Accepted source and administration

Accepted `main` was reverified as
`dbae54279129cbe0486cb8be2f7b0c101607301c`, tree
`ab07f9c0e1888dfadcd50d10b7a5d9c1e3a88566`.
Its [post-merge Windows run](https://github.com/techrote/ASCIIomium/actions/runs/41532579548)
passed. PRs #38/#39 remain the accepted keyboard/mouse source.

Issue #12 was closed after reconciling the merged mouse source with the recorded
operator-reported physical acceptance. Raw final-session diagnostics were not
available; that limitation remains explicit. Tracker #1 was reconciled without
closing #9 or #13–18. The new command interface, differential output and final
integrated browser have not inherited physical acceptance from those older runs.

## Implementation/source ledger

| Workstream | Reviewable source | State at this checkpoint |
| --- | --- | --- |
| #13 browser runtime | [PR #40](https://github.com/techrote/ASCIIomium/pull/40), `issue-13-browser-runtime`, head `88195b12f97ffb433aa57ae9afcc7949355ff36e` | Exact-head Windows Debug/Release, 18 tests per configuration, inherited live/input checks and complete real CEF runtime suite passed in run 41556184528. Independent review clear; physical gate open. |
| #14 differential output | [PR #41](https://github.com/techrote/ASCIIomium/pull/41), `issue-14-differential-output`, head `1272351b03502e7600c1229da8a50bfa4d433bdb` | Exact-head Windows Debug/Release/live checks passed. Independent replay/UBSan review passed. Full output remains default; physical gate open. |
| #15 structured telemetry | [PR #42](https://github.com/techrote/ASCIIomium/pull/42), `issue-15-structured-telemetry`, head `b041201ed6d06546eb93eda368d3d06284b6321c` | Stacked only on #41. Exact-head Windows Debug/Release and all six CEF telemetry benchmarks passed in [run 41552670081](https://github.com/techrote/ASCIIomium/actions/runs/41552670081). Source review clear. |
| #16 lifecycle | [PR #44](https://github.com/techrote/ASCIIomium/pull/44), `issue-16-terminal-lifecycle`, head `ae578e5c2c27adc09a150220e0c1aec1bca86e89` | Reviewed terminal ownership/results, transactional bounded callbacks/frames, ten-phase CEF shutdown/provenance harness including pending resize, and bounded cancellable input with conservative rollback. First Windows build exposed test-header macro collisions, now repaired. Run 41559971458 passed Debug compilation but its test step is stalled; a focused fault-injection/diagnostic/deadline repair is under review. The runner cause awaits completed logs. Integrated telemetry/physical soak open. |
| #17 portable package | [PR #43](https://github.com/techrote/ASCIIomium/pull/43), `issue-17-portable-windows`, head `36d11ec954fb86051a6240739c23a4a0e120ba38` | Stacked only on qualified #13. Builder/verifier/clean-extraction job independently reviewed; first Windows run passed 20 tests per configuration then inherited a runtime logging precondition failure. Updated exact-head run 41557252823 passed both build/test and separate clean-extraction jobs; all seven original artifact digests and sizes, complete payload inventory and custody verified. |
| #18 acceptance | This document and issue #18 | Readiness reconciliation in progress; human and integrated-source gates remain open. |

The branches intentionally have different capabilities. In particular, the #17
candidate does not yet contain the unmerged #14/#15 feature branches. A source-
matched pass for one component is not an integrated-browser acceptance pass.
After the applicable component/human gates permit integration, rebuild and
requalify the resulting exact source. Never relabel an older artifact with a
newer SHA.

## Readiness matrix

| Requirement | Automated/source evidence | Physical or integration gate |
| --- | --- | --- |
| Deterministic flat UI, colour ramps, raster, motion, input/focus fixtures | Existing manifest/Chromium checks and CEF capture/live workflow continue. #14 adds same-frame byte comparisons. | Capture actual Windows Terminal screenshots for flat UI, ramps and raster at final source; #9 visual animation gate remains open. |
| Half-block 256, 1024 and true-colour | Portable renderer/golden tests and five-mode differential replay. 1024 remains baseline. | Compare all three modes on the final candidate/font and record any visual limitations. |
| Full and differential output | Independent VT replay, geometry/shrink/Unicode/invalidation tests; paired exact serialized byte evidence on #41. | Check visible stale cells, right margin, resize-away/resize-back, forced repaint and prompt restoration. Full remains default. |
| Keyboard/mouse | Accepted #38/#39; historical physical result is operator-reported with missing raw final logs. Headless CEF input fixtures continue. | Recheck page editing/Ctrl shortcuts, centre/corner targets, hover, wheel and nested scrolling with the new command overlay and integrated emitter. |
| Navigation/status | #13 shared-parser Ctrl+Alt+O commands, real loopback history/reload/stop/network-recovery suite. | Verify Windows Terminal delivers the chord; inspect opening/cancelling commands, URL/title/status, errors and resize while row is visible. |
| Persistent/private profiles | #13 real cookie/localStorage/IndexedDB restart/isolation/ownership suite. Private cleanup is supervised across actual process lifetime. | Verify interactive restart/private UX on a disposable operator profile. No forensic-erasure claim. |
| Browser policy | Explicit popup/new-window/protocol/permissions/TLS/dialog policy; opt-in controlled downloads. Real page outcomes and download bytes are tested. | Confirm policy notices are discoverable and do not disrupt input; document unsupported browser capabilities. |
| Diagnostics/privacy | Origins-only reports, trusted-fixture raw input logging, explicit capture options; early Chromium file-log suppression and absence checks. | Do not enter credentials into raw input diagnostics. Explicit screenshots/VT/SVG capture visible page content and require deliberate operator selection. |
| Telemetry/benchmark | #15 versioned bounded measurements, source/fixture identity, CPU interval semantics, null availability and six-run protocol. | Terminal writes remain unavailable in headless evidence. Collect actual terminal throughput only on a matching interactive host. |
| Lifecycle/restoration | #13 guards private process ownership; #16 implements callback/write/restore ordering, truthful restoration results and bounded input cancellation; native Windows qualification is running. | Ten real launch/quit cycles; close/error recovery; prompt/cursor/mouse/input/alternate-screen restoration. |
| 30-minute soak | Not complete. A short headless benchmark is not a soak or a physical acceptance substitute. | At least 30 minutes with navigation, typing, clicking, scrolling, resizing and idle periods; preserve start/end/interval telemetry and observations. |
| Real websites | No new real-site compatibility acceptance captured in this continuation. | At least four categories: text/document, image-heavy, responsive application, forms/controls; no site-specific workarounds. |
| Portable package | #17 pinned-archive allowlist, source/dependency provenance, hash manifest, standalone verifier and clean Windows job without checkout/cache. | Exact package and clean smoke jobs passed; original ZIP, complete payload inventory and custody verified and preserved. Final candidate must be requalified after integration. |
| Release readiness | No release action. Unsandboxed direct-executable CEF remains explicit; project-owned license decision is unresolved. | Human approval of final #18 evidence, distribution security/license posture and integrated source required before release. |

## Verified browser runtime

[Run 41556184528](https://github.com/techrote/ASCIIomium/actions/runs/41556184528)
passed on head `88195b12f97ffb433aa57ae9afcc7949355ff36e`, tree
`536da6989222c59b1b43502bd46c44a887af96a0`. Debug and Release each passed
all 18 CTest tests, followed by the inherited capture/live/input cases and the
complete 15-record real CEF runtime suite.

| Runtime requirement | Actual qualification result |
| --- | --- |
| Persistent storage | Expiring and session cookies, localStorage and IndexedDB verified by page JavaScript after clean process restarts |
| Profile isolation/private mode | A separate persistent root starts empty; private write then a new private root starts empty; original persistent data remains intact |
| Ownership | Concurrent second owner exits with an explicit refusal; the root reopens after the real holder exits |
| Private cleanup | All private reports show worker exit zero, complete process-tree reaping, no forced descendants, and successful root removal |
| Navigation | Eight observed page transitions cover history/reload/hard reload/stop/redirect and recovery to a distinct successful page after a network failure |
| Popups/permissions | Page observes blocked popup and denied notification/location/media results; host records one popup denial, three permission denials and one media denial |
| Downloads | Default denial leaves the bounded fallback directory empty; opt-in completes one file with exact benign fixture bytes and an application-chosen path, ignoring the remote filename |
| TLS | Untrusted local HTTPS is rejected, its page does not execute, and the host certificate callback records denial |
| Diagnostic files | Persistent roots and executable directory contain neither `chrome_debug.log` nor `debug.log` |

Artifact `13194323404` was downloaded and checked against SHA-256
`c4bea38431f9e7a0095fb23a6b36382762a0cb9097e34c59b9d0edbe7f880b4e`.
Every successful probe, each private-supervisor result and the separate
ownership-holder report were independently checked. The exact summary and
per-file custody hashes are included under `docs/evidence/2026-11-03/`; the
original ZIP and build/failure logs have saved copies. This establishes the
listed automated behaviour, not command-row or physical terminal acceptance.

## Verified differential measurement

[Run 41548940956](https://github.com/techrote/ASCIIomium/actions/runs/41548940956)
passed all 16 CTest tests in both configurations on #41 head `1272351b03502e7600c1229da8a50bfa4d433bdb`, tree
`4d53624bca769624ec08a0736d119e14ebe2d50f`. Both serializers consumed each
identical logical frame in the same headless CEF run at 160×50 cells.

| Live fixture | Paired frames | Full bytes | Differential bytes | Reduction |
| --- | ---: | ---: | ---: | ---: |
| Flat UI startup | 2 | 84,052 | 74,068 | 11.878% |
| Motion/scroll | 67 | 3,441,504 | 261,487 | 92.402% |

| Live fixture | Full bytes/frame | Differential bytes/frame | Full bytes/s | Differential bytes/s | Changed cells / runs |
| --- | ---: | ---: | ---: | ---: | ---: |
| Flat UI startup | 42,026.00 | 37,034.00 | 20,997.55 | 18,503.38 | 13,994 / 168 |
| Motion/scroll | 51,365.73 | 3,902.79 | 858,746.74 | 65,247.95 | 27,480 / 3,202 |

Byte rates use the entire measured interval, including startup/idle time. The
changed-cell/run columns are interval totals. These are serialized payload
rates from a headless process, not measured physical terminal write throughput.

Motion mean planning was 0.066633 ms; differential serialization mean was
0.293767 ms, versus 2.283339 ms for full reference serialization. Comparison
mode adds reference work and is not a single-mode throughput measurement.
The static scheduler already idles after settling, so the separate 240-frame
synthetic static test proves 239 zero-payload repeated frames. The moving 12×5
synthetic patch used 152,804 bytes versus 6,355,680 full bytes (97.60% reduction).

Artifact `13190183078` was downloaded and verified against Actions SHA-256
`139212cae9565ff057f84abb2c6f51ceb7f5eca3b6a4d0f953f4017ea8f8efcd`.
The full ZIP and custody record have a durable saved copy. This branch includes
the exact summary/synthetic JSON and public custody metadata under
`docs/evidence/2026-11-03/`.

## Verified structured telemetry measurement

[Run 41552670081](https://github.com/techrote/ASCIIomium/actions/runs/41552670081)
passed both build configurations, all 18 CTest tests in each, and all six CEF benchmark/invalid-CLI controls
on #42 head `b041201ed6d06546eb93eda368d3d06284b6321c`, tree
`002576133fee8d09716d68eba2bb6e7b0621cfd5`. At 160×50, the 50-frame motion
comparison serialized 191,470 differential bytes versus 2,549,291 full bytes
(92.4893% reduction). Mean planning was 0.064574 ms, p95 0.1191 ms; mean
selected serialization was 0.232718 ms, p95 0.4058 ms. Source generations
partitioned as 172 = 50 distinct rendered + 119 coalesced + 3 unrendered tail.

The Windows microbenchmark measured 219.962 ns median incremental cost for
eight clock reads plus six history updates, 98,496 bytes fixed history storage
on that ABI, and 0.0209 ms for two endpoint process probes. Whole-run means,
rolling percentiles, process counter intervals and null availability are explicit.
Actual terminal writes remain null in headless reports. Independent timer-on/off
runs are observations, not paired causal throughput comparisons.

Artifact `13191874974` was downloaded and verified against SHA-256
`1a3789bf5d128f66e9d8080955d4cff63131c8b495c0c83dda4537af447a2b79`;
all six report hashes and exact clean source identities also verified. The
summary and custody JSON accompany this checkpoint. Exact sample static and
motion reports are committed as `issue-15-sample-static-differential.json` and
`issue-15-sample-motion-differential.json` in the same evidence directory; all
original reports are preserved in the saved evidence ZIP.

## Verified portable Windows package

[Run 41557252823](https://github.com/techrote/ASCIIomium/actions/runs/41557252823)
passed on `36d11ec954fb86051a6240739c23a4a0e120ba38`, tree
`106b38e41488da8b65dd293256eeafe86869530f`. Debug and Release each passed
20/20 CTest tests. The inherited browser runtime suite passed, then the builder
created the archive and a separate Windows runner verified and exercised it
without a source checkout or CEF cache.

The package has 254 files, including both application executables, nine CEF DLLs,
ICU/PAK/V8/Vulkan data, 220 locale files, the exact fixture bundle, CEF LICENSE
and Chromium CREDITS, `browse.cmd`, instructions and verification/provenance
metadata. The payload manifest covers 253 files and excludes only itself.
CEF is `151.3.17+gf059e67+chromium-151.0.7922.138`, full commit
`f059e67fa6aad5e8cce8bebea5df706ffddfb174`.

| Clean-run check | Observed result |
| --- | --- |
| Both version commands | Exit 0; exact clean source/tree; Release/x64; CEF runtime/header and fixture identity match |
| Static `flat-ui` | 2.207129 s, 2 source paints, 2 rendered frames |
| Animated `motion-scroll` | 3.001099 s, 113 source paints, 32 rendered frames |
| Paths and cleanup | Executable-relative assets from an unrelated working directory; clean close; private root removed in one attempt; process tree reaped without forced descendants |
| Inventory and negative control | Package unchanged after browsing; missing `libcef.dll` rejected even with regenerated ordinary manifest |

The [outer Actions artifact](https://github.com/techrote/ASCIIomium/actions/runs/41557252823/artifacts/13195701111)
is 183,620,050 bytes, SHA-256
`4a79008cfc1f5aab34c5404c3cf0724192e0d3466f7a66c75661530589a78336`.
It contains the distributable
`ASCIIomium-0.1.0-windows-x64-dev-36d11ec954fb.zip`, 183,535,791 bytes,
SHA-256 `c849bf5d4c4dddf1eb9171dbc5b7c0c10a80cacc736d37890bdccb06541059c5`,
and external custody, provenance, manifest and verifier. All seven original
run artifacts were downloaded and verified against Actions digests and sizes.
The downloadable package and full evidence have preserved copies; the exact
review, clean-smoke report and custody are under `docs/evidence/2026-11-03/`.

This is an unsigned development candidate under the documented disabled CEF
sandbox. It contains qualified #13, but excludes #14/#15 and the new #16
changes. Legacy headless timing/restoration fields in this package are not
physical terminal measurements. Four source text files use Windows CRLF and
match Git after newline normalization; all fixture bytes match Git exactly.
Normalized ZIP construction does not establish independently reproducible
MSVC binaries. Final distribution and integrated-source qualification remain open.

## Failure evidence and repairs

#13's initial Windows compile found the pinned CEF enum spelling
`CEF_WOD_CURRENT_TAB`; it was corrected. Subsequent Debug builds/tests passed,
but live private cleanup failed with Windows sharing violation 32 after CEF
closed. A bounded retry did not fix the failure. Diagnostic residue showed
Chromium installation/default-profile files and `chrome_debug.log`; it did not
identify a particular blocking handle. The reviewed fix uses a normal OSR worker
and an owning supervisor, retaining a private root lock until the process tree
exits and then verifying deletion. It does not enable Chrome headless mode.

Pinned-source investigation also established that CEF's severity-disable setting
alone still permits a file logging destination. The runtime now supplies an app
callback to browser and child entry points with `disable-logging`, and persistent
root/executable-directory checks verify absence independently of deletion.
The first supervised run passed both builds, all 18 unit tests and inherited
live/input checks, then rejected stale executable-directory logs from the older
capture tool. The capture tool now supplies the same early app policy. The
absence assertion remained intact and passed in the qualified run.

#17's first Windows run likewise passed both builds and all 20 tests in each
configuration, then failed this inherited precondition before building the ZIP.
The package branch then received the qualified runtime dependency while preserving
its history. Exact head `36d11ec954fb86051a6240739c23a4a0e120ba38` passed
[run 41557252823](https://github.com/techrote/ASCIIomium/actions/runs/41557252823),
including package construction and a separate clean-extraction Windows job.
The earlier failed run remains preserved as failure evidence.

#16's allocation-failure audit reproduced mixed frame state: new pixel data with
old generation/count/statistics after dirty-rectangle allocation failed. The
reviewed repair reserves all required storage before publication, bounds damage
metadata, and makes callback failure observable through final CEF shutdown in
all three consumers. Diagnostic size/trust checks precede conversion. The source
repairs and portable fault tests are complete. The reviewed input reader and
mode-rollback follow-up are integrated; Windows qualification is running on
PR #44. Its first Windows run failed during direct-callback test compilation:
Windows `max` and `small` macros collided with CEF headers and a test variable.
The target now defines `NOMINMAX`/`WIN32_LEAN_AND_MEAN`, the variable is renamed,
and no assertions changed. The failed log is preserved. A reviewed follow-up
replaces duplicate ready phases with pending-resize shutdown cases, keeping ten
launches. The next run passed Debug compilation but its test step is stalled.
A source review found that injecting allocation failure into MSVC Debug STL
`noexcept` iterator-proxy construction can terminate before the intended callback
boundary; a focused payload-injection, failure-diagnostic and deadline repair
is under independent review. That source finding does not identify the current
runner failure without its completed log. No native-console result is inferred
from the local Win32 shim tests.

#15's first Windows run passed every benchmark assertion and wrote all reports,
then failed because PowerShell retained exit code 2 from an intentionally invalid
CLI test. The harness now explicitly succeeds only after all negative checks and
evidence writes complete. The corrected exact head is the qualification authority;
the earlier run remains failed despite its valid benchmark observations.

## Physical qualification instructions

Use a disposable profile and the exact source/package being accepted. Record
the executable's version output, source SHA/tree, package SHA-256 if applicable,
Windows/Windows Terminal version, font, display scale, terminal rows/columns and
browser viewport before testing. Keep accepted and candidate executables clearly
separated. `browse.cmd` in the #17 candidate starts the browser from any working
directory; `asciiomium.exe` is the diagnostics/version executable.

1. Verify the package and identity using its included instructions/verifier.
   Save custody, version output and automated reports outside the package.
2. Open each bundled fixture. For the integrated candidate, compare half-block
   256/1024/true-colour and full/differential output at 160×50, then resize smaller
   and larger. Preserve actual terminal screenshots and note the tested settings.
3. On input/focus, type and edit text, test Ctrl shortcuts and navigation keys,
   hover/click centre/corners, and scroll the page plus nested region. Open
   Ctrl+Alt+O during a held mouse gesture and release; verify no stuck button.
4. Exercise open/back/forward/reload/hard-reload/stop/status/repaint/quit, cancel
   the command row, and recover from an unreachable URL. Check current URL/title
   and policy notices without entering credentials into diagnostic logs.
5. Restart a disposable persistent profile and verify benign storage. Start a
   private session, quit normally and verify the finalized cleanup evidence.
   Try simultaneous ownership and an unwritable profile path; failures must be
   explicit without harming the existing profile.
6. Test the four real-site categories and preserve one recognizable terminal
   screenshot. Record exact URLs/settings/results and limitations in the private
   evidence pack; avoid publishing tokens, private content or authentication data.
7. Run ten launch/quit cycles, using Ctrl+Alt+Q and command quit. Check cursor,
   mouse tracking, input/echo, alternate screen and the shell prompt each time.
   Exercise documented error paths and Ctrl+Break separately.
8. Conduct the actual 30-minute interactive soak with a timed sequence of
   navigation, typing, clicks, scrolling, resize and idle periods. Record telemetry
   intervals and process scope; inspect memory/CPU drift and bounded queue counts.
   Mark unperformed cases as not tested rather than passing them.

Acceptance records must identify the operator, start/end time, exact source,
settings, individual pass/fail/not-tested outcomes, evidence filenames and known
defects. Manual screenshots, soak or real-site outcomes cannot be inferred from
headless CEF, synthetic replay, historical acceptance or a green package job.

## Next executable steps

Finish exact-head Windows qualification of #16, repairing actual failures while
retaining their evidence. #13/#14/#15/#17 component automated qualification and
package custody verification are complete.
Update this ledger with actual run/artifact hashes. Then gather the outstanding
physical results before guarded integration; run post-merge main verification
only if a merge is actually performed. Final integrated #16/#18 tests and a fresh
package must follow the final source, not any earlier component candidate.

Research issues #19–25 remain deferred.
