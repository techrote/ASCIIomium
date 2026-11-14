# P0 integration candidate — owner-gated acceptance ledger

This document records the P0 integration of the five preserved Draft/HOLD feature PRs. **It is not a release approval**. The physical Windows Terminal tests in issue #18 remain mandatory.

## Source genealogy

- Accepted main at integration start: `dbae54279129cbe0486cb8be2f7b0c101607301c`.
- #40 browser profiles/navigation/policies: `88195b12f97ffb433aa57ae9afcc7949355ff36e`.
- #41 differential VT reference and replay: `1272351b03502e7600c1229da8a50bfa4d433bdb`.
- #42 telemetry (stacked on #41): `b041201ed6d06546eb93eda368d3d06284b6321c`.
- #43 packaging (stacked on #40): `36d11ec954fb86051a6240739c23a4a0e120ba38`.
- #44 terminal/lifecycle (stacked on #40): `79c9bab35b37c509c0233038da8156b749919d56`.
- Mechanical merges have actual Git ancestry; the original five PR branches and their evidence remain unchanged.
- Final qualified source commit, CI run IDs, artifact SHA-256 and payload inventory **must be filled from exact-head evidence after all repairs**; no earlier sibling-branch evidence qualifies this integrated source.

## Reconciliation record

- Browser profile ownership, command row, explicit policy handling and bounded diagnostic strings stay in the hardened main loop.
- Terminal reader ownership, callback failure propagation, CEF early-close ordering and truthful restoration evidence take precedence over older pre-hardening call sites.
- The telemetry input dispatcher calls the browser-command-aware `ForwardInput` once for native and synthetic events, retaining debug-triggered full repaint.
- The interactive status overlay and periodic differential reconstruction are both retained; a logical frame is measured after overlay composition, and the full emitter remains default.
- Differential planning/reference ownership and telemetry samples preserve their existing separate contracts. Headless serialized bytes are not terminal throughput.
- Packaging uses the executable-relative fixture resource path, locked CEF archive, external custody metadata and a separate no-checkout/no-cache extraction job.
- CMake and Windows CI register the union of the inherited native suites, replay tests, telemetry protocols, 10 real-CEF shutdown cases, and package extraction.
- The original P0 tests and failing historical workflow runs remain preserved.

## Acceptance matrix

| Gate | Authority | Status |
| --- | --- | --- |
| Debug/Release native CTest + fixture/CEF pipeline | Exact-head Windows CI | Pending final-source pass |
| Real CEF browser runtime and 10 shutdown variants | Exact-head Windows CI + runtime JSON | Pending final-source pass |
| Differential emitter replay and real CEF pairwise serialization | Exact-head CI | Pending final-source pass |
| Telemetry sample/disabled/identity benchmarks | Exact-head CI | Pending final-source pass |
| Portable package, custody hashes, clean extraction | Exact-head CI + verifier | Pending final-source pass |
| Source/integration review | Independent comparison against all PRs | Pending final disposition |
| Physical Windows Terminal, representative public sites, all render modes, keyboard/mouse/resize/profile, 10 relaunches, 30-minute interactive soak | Owner | **HOLD — not delegated to CI** |
| Security, licensing, sandbox and public-distribution approval | Owner | **HOLD** |

## Explicit limitations

The current CEF bootstrap retains its documented unsandboxed development configuration. This candidate must not be described as a hardened public release. User Terminal and external-site screenshots, actual physical input/resize/restoration and full 30-minute soak are not captured by hosted CI. Use the acceptance procedure in this PR; record site URLs, Windows Terminal version, screenshots, limitations and observed session duration without uploading credentials or browsing data.
