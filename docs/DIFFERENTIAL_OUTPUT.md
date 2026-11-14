# Differential terminal output

Issue #14 adds an opt-in differential emitter while retaining the unchanged
`SerializeFullFrame` implementation as the reference path. The live default is
still `--emitter full`; physical Windows Terminal qualification remains required
before making differential output the default.

## Use

```powershell
.\build\bin\RELEASE\asciiomium_live.exe `
  --fixture motion-scroll --emitter differential --full-repaint-ms 10000
```

`--emitter full` selects the reference output. `--full-repaint-ms N` clears and
reconstructs the screen every N milliseconds (100..3,600,000; 0 disables it).
It also requests a scheduled refresh when the source is static. Resize always
invalidates the reference, independent of this interval. The existing
Ctrl+Alt+Q exit chord and keyboard/mouse parser are unchanged.

Differential mode emits no bytes when a new rendered frame is identical to its
trusted predecessor. `--max-frames` counts **nonempty** payloads, so unattended
`--no-terminal --emitter differential` runs require a positive `--duration-ms`
even when `--max-frames` is supplied. A stable source already idles without
conversion under the existing newest-frame scheduler; diffing does not invent
extra work to claim savings on that idle period.

`--vt-output` always writes a standalone full-frame reference snapshot of the
last logical image. It never writes just the last delta, which would require all
preceding deltas to reconstruct. `runtime.last_vt_bytes` still describes the
last live payload and may legitimately be zero in differential mode.

## Algorithm and ownership

`PlanDifferentialFrame(previous, current, force_full)` compares glyph,
foreground and background. Adjacent changed cells become runs. For gaps within
one row, it compares exact lengths of cursor positioning, RGB/indexed SGR and
UTF-8 glyph bytes against writing through the gap. Both alternatives include
the first following changed cell, so gap colours cannot hide an expensive SGR
transition. The byte estimate is checked against actual serialization in tests.
This is a greedy local choice, not a claim of globally minimal terminal output.

Runs never cross rows. Serialization carries colour state across the runs in
one emission, then returns the cursor home and resets SGR. Cursor movement
cancels pending right-margin autowrap before any later printable byte. No
cross-frame SGR cache is needed. Initial frames, geometry changes and explicit
invalidation begin with SGR reset and a full-screen clear, followed by the
reference-equivalent complete grid. Clearing removes cells outside a shrunken
content rectangle.

The render loop retains exactly one last logical frame, one current frame and
bounded frame-sized plan/output storage. It only commits the new reference
**after the output write succeeds**. Current write errors terminate through the
existing cleanup path. Any future recovery after partial output must invalidate
before continuing. No worker threads, frame queues, GPU path or additional
renderers were introduced.

The terminal-cell contract remains one printable, single-column glyph per cell.
The baseline emits U+2580. Spaces, ASCII and single-column block/Latin glyphs
are exercised; arbitrary emoji, wide characters, combining sequences and IME
layout are not implemented. Control glyphs are rejected by the differential
API. `TerminalCell` has no style field; adding styles later requires extending
both equality and serialization.

External terminal writes invalidate a retained display. All input-debug
writes, including synthetic delivery, trigger reconstruction; geometry-debug output accompanies a resize that
already invalidates. Future browser chrome/status overlays must likewise
invalidate before returning to page rendering. Rejected physical geometry suspends output
and pointer mapping; returning to an accepted geometry reconstructs even if the
logical transform itself is unchanged. Output from unrelated processes
cannot be detected automatically; use the periodic reconstruction option when
needed.

## Measurements and schema

The existing live report remains `asciiomium-live-v1` with an additive
`emission` object, identified as `asciiomium-emission-v1`:

| Field | Meaning |
|---|---|
| `bytes_serialized` | Selected live payload bytes, including cursor/SGR/clear overhead |
| `bytes_written_to_terminal` | Same successful byte count with a real terminal; `null` in headless runs |
| `bytes_per_rendered_frame` | Total payload bytes / all rendered frames, including empty deltas |
| `bytes_per_nonempty_payload` | Total payload bytes / nonempty emissions |
| `bytes_per_second` | Total payload bytes / observed render-loop duration |
| `empty_payload_frames` | Rendered frames that required no output |
| `changed_cells`, `changed_runs` | Actual differing cells and original contiguous changed runs; `null` if diff analysis is disabled |
| `written_cells`, `emitted_runs` | Actual selected output, including unchanged gaps intentionally rewritten |
| `full_reconstructions` | Initial, resize, diagnostic or periodic complete reconstruction count |
| `differential_planning`, `differential_serialization` | Separate elapsed stages; unavailable as `null` |
| `same_frame_full_bytes`, `same_frame_differential_bytes` | Optional paired comparison on the exact same logical frames |

`--compare-emitter` serializes both paths for each logical frame, without an
extra source-frame copy. This removes separate-run CSS scheduling as a cause of
bandwidth differences, but **adds CPU work**. Use the option for byte comparison;
run each selected mode separately without it for normal end-to-end performance.
Averages cover the whole run. p50/p95 use the latest 2,048 samples with index
`ceil(p * (N - 1))`. Actual console write time remains separate. This narrow
emitter instrumentation does not claim completion of issue #15.

## Reproducible qualification

The Debug and Release `differential_frame_emitter` test replays bytes through an
independent VT subset interpreter. It compares the final display against the
full-frame reference after every transition, including 400 seeded random
transitions through all five colour modes, isolated and whole-row edits,
short/long/colour-expensive gaps, Unicode block changes, blanking, shrink/grow,
forced repaint and simulated corruption. The interpreter models delayed wrap
and verifies that an intentionally unsafe bottom-right stream really scrolls.
A separate runtime test verifies that an empty output advances the rendered
source generation without inventing an emitted payload or building a backlog.

For the bounded synthetic benchmark:

```powershell
.\build\bin\RELEASE\asciiomium_differential_benchmark.exe `
  --samples 240 --fps 20 --colors 1024 --json .\build\differential-synthetic.json
```

It creates 160x50 synthetic static frames and a moving 12x5-cell patch, measures
both emitters on each identical frame, and retains only the current/previous
frames plus fixed-capacity timing histories. Samples are limited to 2,000, so
all timing samples fit. `--fps` specifies **nominal playback bandwidth**, not an
actual paced terminal run. Measured compute duration is recorded separately.
The synthetic JSON has its own `asciiomium-differential-benchmark-v1` schema;
the earlier offline benchmark format is unchanged.

For real CEF fixture measurements and periodic invalidation:

```powershell
.\tools\verify_differential_emission.ps1 `
  -BinaryDirectory build/bin/RELEASE -OutputDirectory build/differential-evidence
```

The script runs `flat-ui --freeze` and `motion-scroll` at 640x360 source and
160x50 terminal geometry for four seconds each, using 1024-colour box/contain
rendering and a 20 FPS cap. Full and differential byte totals are calculated
from the same rendered frames. It checks valid reports, source closure, accounting,
standalone snapshot output, actual motion-byte savings, and reconstruction of a
static source every 300 ms. A separate synthetic-input diagnostic run verifies
that diagnostic output requests reconstruction even without periodic invalidation. The summary includes the actual checked-out source
SHA and snapshot SHA-256 values. CI uploads the complete reports, summary and
snapshots as `asciiomium-differential-evidence`.

This is automated CEF/serialization qualification. The required physical test
is still to run both modes in Windows Terminal with 256, 1024 and true-colour
fixtures, resize through shrinking/growing geometries, interact/scroll and
confirm display equivalence and clean terminal restoration. Headless evidence
must not be recorded as that physical pass.

## Current portable measurement

The committed [sample JSON](evidence/issue-14/portable-synthetic-1024.json) was
measured with GCC 13.3.0, C++20, `-O2` on Linux. It is portable serializer
evidence, not a Windows or CEF browser benchmark.

| 240 frames, 160x50 | Full bytes | Differential bytes | Reduction |
|---|---:|---:|---:|
| Repeated static grid | 6,275,040 | 26,154 | 99.58% |
| Moving 12x5 patch | 6,355,680 | 152,804 | 97.60% |

The static first reconstruction is 26,154 bytes (8 bytes more than the reference
because it resets/clears); the following 239 payloads are empty. Motion averages
26,482 full bytes/frame versus 636.68 differential bytes/frame, equivalent to
529,640 versus 12,733.67 bytes per **nominal** second at 20 FPS. This machine's
measured planner means were approximately 0.03–0.04 ms/frame; those timings are
observations, not performance gates or predictions for another machine.
