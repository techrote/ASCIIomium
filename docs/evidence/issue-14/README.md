# Issue #14 portable evidence checkpoint

This branch implements differential output independently of browser-runtime
issue #13, from accepted main `dbae54279129cbe0486cb8be2f7b0c101607301c`.

- `portable-synthetic-1024.json`: actual same-frame serialized byte counts and
  measured CPU-stage timings for 240 static and 240 moving-patch frames at 160x50.
- `portable-environment.json`: compiler, platform, flags, evidence timestamp and
  SHA-256 identities for the tested serializer/test/benchmark source files.

The byte totals are deterministic for these source files. Elapsed timings vary
with hardware and scheduling. The source-file hashes bind the measurements
without attempting to embed a commit's own future hash in its tree.

The portable optimized replay test and updated scheduler test passed. Debug
replay also passed; an independent reviewer ran the replay under UBSan, found
and reviewed fixes for two runtime invalidation omissions, and reported no
remaining blocking finding. A final explicit whole-row test was added in
response to the review and passed in the optimized run.

Windows Debug/Release builds and real CEF fixture qualification are defined in
`bootstrap-windows` and `tools/verify_differential_emission.ps1` but have **not**
been executed in this Linux workspace. Their evidence must come from the exact
published branch head's hosted Windows run. Physical Windows Terminal visual,
resize, interaction and restoration acceptance remains outstanding. Full-frame
output remains the default. No issue closure, merge, release, tag or physical
acceptance is claimed by this checkpoint.

Next executable step: publish this separate branch, open the #14 implementation
PR, run its exact-head Windows workflow, investigate any real failures, preserve
the `asciiomium-differential-evidence` artifact, and retain the manual gate.
