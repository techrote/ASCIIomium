# Runtime telemetry and bounded benchmarks

Issue #15 adds a bounded benchmark mode and a versioned measurement section to
the live browser report. It is stacked on #14's differential-output work and
does not depend on the unmerged browser-runtime/profile branch.

## Run

```powershell
.\build\bin\RELEASE\asciiomium_live.exe `
  --fixture motion-scroll --benchmark 30s --report .\build\benchmark.json `
  --emitter differential --colors 1024 --filter box `
  --columns 160 --rows 50 --no-terminal
```

`--benchmark` accepts an integer followed by `ms`, `s` or `m`, from 100 ms to
one hour. Examples: `500ms`, `30s`, `2m`. Missing units, fractions, signs,
overflow, out-of-range values and trailing junk are rejected. A conflicting
`--duration-ms` or any nonzero `--max-frames` is rejected. The default report
path is `benchmark.json` if `--report` is omitted. Ordinary `--duration-ms`
remains supported without implying benchmark mode.

Benchmark mode limits the render-loop measurement interval. It does not turn
startup or browser shutdown into measured render work and does not synthesize
a successful frame if the browser never paints. Exit reason, requested/observed
duration and normal completion are recorded. The interval includes initial
asynchronous page loading; no warm-up frames are silently discarded. CEF initialization, initial page
loading and clean close still use the existing runtime lifecycle. Very short
runs can legitimately end before a useful frame exists and fail qualification.

Without `--no-terminal`, output goes to the real terminal. With it, the same
CEF/conversion/emitter pipeline runs, but actual terminal-write bytes and write
timings are **null**. Serialized byte counts and serialization timings remain
real measurements. CI uses this headless path and does not claim physical
Windows Terminal performance or acceptance.

Use `--emitter full` for the unchanged reference mode. `--compare-emitter`
measures full and differential byte totals from each identical logical frame.
It adds an extra serializer and must not be treated as normal single-mode
throughput. For normal runtime comparisons, run the selected mode without that
flag. Static sources already idle under the existing scheduler; telemetry does
not manufacture repeated source paints.

## Versioning and configuration

The outer report remains `asciiomium-live-v1`. A new additive `telemetry` object
has schema `asciiomium-runtime-telemetry-v1`. #14's `emission` object and the
older standalone `asciiomium-benchmark-v1` offline format remain available.

The telemetry identity includes ASCIIomium version, Git commit/tree, dirty
checkout state, configuration, compiler, architecture, compile/runtime CEF,
Chromium version, header/runtime compatibility and the built fixture manifest's
SHA-256. Source identity is refreshed **at every build**, not only configure:
a generated header is changed only if Git/fixture identity changed, and the
normal compiler header dependency rebuilds BuildInfo. A checked-out source
edit or commit followed by an incremental build therefore updates identity.
Untracked nonignored files make the checkout dirty. Missing Git identity is
reported as null. Identity is a build-time observation; modifying the executable
or fixture files after the build does not produce a new source identity.

The `configuration` object records source kind, fixture ID/version/freeze mode,
source viewport, terminal geometry, encoder, palette, sampling, fit, cell aspect,
frame caps, emitter, full-repaint interval, comparison mode and terminal-output
mode. It does not copy supplied website URLs or browser contents into the new
telemetry section. The legacy URL diagnostic remains governed by the browser
runtime's logging policy.

## Counter and timing definitions

| Measurement | Definition |
|---|---|
| Source view/popup paints | Counters sampled at the start/end of the render loop, before browser shutdown can add more work |
| Presentation generations | Coherent source-store generations; popup presentation changes can also advance them |
| Distinct generations rendered | Generations converted/presented for the first time; forced repeat renders do not increase it |
| Coalesced generations | Generations skipped by the existing newest-frame scheduler, not a separate frame queue |
| Unrendered tail | End presentation generation minus last rendered generation; kept separate from completed coalescing |
| Rendered frames | Successfully completed conversion/emission iterations, including empty deltas |
| Emitted frames | Nonempty serialized payloads successfully handled by the selected output path |
| Empty payload frames | Rendered frames with no terminal bytes; `rendered = emitted + empty` is reported explicitly |
| Bytes serialized | Actual selected VT payload bytes, including SGR/cursor/reconstruction overhead |
| Bytes written to terminal | Completed payload bytes only with terminal output and normal completion; null for headless or incomplete/error runs, whose partial-write counts may be unknown |
| Changed cells/runs | #14's logical differences before optional unchanged-gap bridging; null when diff analysis is disabled |
| Written cells/runs | Actual selected emission, including unchanged cells intentionally rewritten |
| Bytes/rendered frame | Serialized bytes divided by rendered frames, including zero-byte deltas |
| FPS and bytes/second | Counts divided by the actual render-loop measurement duration |

Conversion, differential planning, selected serialization and actual terminal
write are timed separately. Paired comparison reports both serialization costs.
The conversion stage includes popup composition and the existing half-block
conversion, consistent with the previous live report.

Each timing history has fixed capacity **2,048**. `mean_ms` covers all accepted
samples in the interval; p50/p95 cover the most recent retained samples, with
index `ceil(p * (N - 1))` after ascending sort. The report includes total count,
retained count, capacity, both scopes and the percentile formula. No samples
means null timing values; an actual measured zero remains zero. No denominator
means a null rate. NaN/infinite values cannot leak into numeric JSON.

For compatibility, `timing_ms` retains its old numeric field types and adds an
`available` flag. When unavailable/disabled, its numeric zero placeholders are
explicitly labeled with a legacy-placeholder scope; they are not measured zero
latencies. The new `telemetry.timing` fields are authoritative and strictly null
when unavailable. Available legacy means/percentiles use the same windows as the
new section. The final summary prints `unavailable` when collection is disabled.

## Windows process measurements

Only the ASCIIomium browser process is sampled. **CEF renderer, GPU and other
child processes are excluded**, so this is not aggregate Chromium resource use.
Windows `GetProcessTimes` supplies kernel+user CPU ticks, and
`GetProcessMemoryInfo` supplies working set, private bytes and lifetime peak
working set. Start/end memory snapshots describe the render interval's
boundaries; the peak is explicitly a process-lifetime peak and includes startup.
Unavailable APIs/counters remain null.

CPU percentage uses a separate interval measured between steady-clock
midpoints surrounding the two `GetProcessTimes` calls. The report retains that
interval and the endpoint sampling uncertainty, so it does not divide a wider
CPU sampling span by a shorter render-loop duration. `cpu_percent_one_core`
uses one fully occupied logical processor as 100%; it can exceed 100%.
`cpu_percent_machine` divides by the reported active logical processor count
across processor groups. A changing/unknown topology makes the normalized value
unavailable. Invalid/regressing CPU counters or sample clocks do not become
zero usage.

Process APIs are called only at the two endpoints, not per frame. Report JSON
and percentile sorting occur after the measured loop.

## Instrumentation overhead and disabling

`--telemetry off` disables stage timestamp reads, timing-history updates and
process sampling. The clocks needed for frame pacing, time limits and input
handling remain; output correctness/accounting counters also remain. Disabled
measurements are null, and the browser uses the same rendering and emission
paths. No source-frame/event queue is introduced.

Six timing histories occupy a fixed amount of storage, reported using the
actual compiler's `sizeof(TimingSeries)`. A microbenchmark pairs the same
checksum-producing integer workload with and without eight clock reads and six
history updates, alternates run order over seven trials and reports every
observation plus median incremental nanoseconds. It does not time CEF,
conversion or actual terminal I/O, and its result is not an end-to-end speedup.

```powershell
.\build\bin\RELEASE\asciiomium_telemetry_overhead.exe `
  --iterations 100000 --json .\build\telemetry-overhead.json
```

The portable GCC 13.3.0 `-O2` sample under `docs/evidence/issue-15/` measured
roughly **0.25 microseconds** of incremental clock/history work per iteration,
about **0.0005% of a 50 ms frame budget**, with 98,592 bytes for six histories on
that ABI. These are machine observations, not portable performance guarantees.
Windows API endpoint overhead is measured separately on Windows and remains
null in the Linux sample. Separate live timer-on/off runs are retained for
practical comparison, with browser scheduling variation acknowledged.

## Qualification

`runtime_telemetry_tests` exercises duration bounds/overflow, cumulative versus
rolling timing windows, unavailable versus measured-zero fields, JSON escaping,
rate denominators, paired compression ratios, frame accounting and Windows CPU
arithmetic. An isolated real CMake build test uses the same identity helper as
the application: clean build, source edit/rebuild, commit/rebuild, untracked
file/rebuild and return to clean. It checks that unchanged identity does not
rewrite the generated header. It never modifies the development checkout.

Hosted qualification runs:

```powershell
.\tools\verify_runtime_telemetry.ps1 `
  -BinaryDirectory build/bin/RELEASE -OutputDirectory build/telemetry-evidence
```

The script records six bounded 3-second real CEF runs at 640x360 source,
160x50 terminal, box/contain/1024 colours and 20 FPS: frozen flat UI and motion,
full and differential with paired byte comparisons, then motion/differential
with timers on/off without the paired serializer. It verifies exact source and
fixture identity, configuration, sample/counter semantics, null headless writes,
Windows process data, normal duration completion, valid JSON and CLI rejection
cases. It preserves all reports, source-bound summary and report SHA-256 values
as `asciiomium-runtime-telemetry-evidence` in the existing Windows job.

This provides no physical terminal-write latency, screenshots or human soak
acceptance. Issues #16 and #18 retain those gates. Full-frame remains the default;
this change does not publish a release or finish #14's physical gate.
