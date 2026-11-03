# Issue #15 portable implementation checkpoint

Dependency: issue #14 / PR #41, exact accepted implementation head
`1272351b03502e7600c1229da8a50bfa4d433bdb`. This branch does not include #13.

- `portable-overhead.json` is an actual paired measurement of the same integer
  workload with/without eight clock reads and six bounded timing-history
  updates, seven alternating-order trials. It is not a CEF or terminal test.
- `portable-environment.json` records compiler/platform, source-file SHA-256
  identities, report digest and completed local verification.

Completed: portable optimized/debug/UBSan tests, independent JSON parsing,
shared-helper real incremental-build provenance regression, workflow inventory
validation and independent technical review. Review corrected stale
configure-only identity, clarified legacy placeholders, tied Windows CPU usage
to its own counter sample interval and made partial-write byte uncertainty
explicit. No browser/terminal acceptance is inferred from these checks.

The hosted workflow is prepared to produce real static and animated benchmark
reports, full/differential paired comparisons, timing-on/off observations,
Windows process data and an exact-source summary. Those results have not been
executed in this Linux workspace. Preserve the actual hosted `static-full.json`,
`static-differential.json`, `motion-full.json`, `motion-differential.json`,
`motion-timers-on.json`, `motion-timers-off.json`, `overhead.json` and `summary.json`
from `asciiomium-runtime-telemetry-evidence` with their tested commit/digests.
Source-bound evidence can be committed separately from the implementation head
so recording results does not silently change the source being qualified.

Next action: publish this branch as a PR explicitly stacked on #41, run exact-head
Windows CI, investigate any real failure and archive its actual reports. The
Windows checks remain outstanding until observed. #14's physical Windows
Terminal gate is inherited; #16 soak and #18 release acceptance remain open.
No merge, issue closure, tag, release or new physical acceptance is claimed.
