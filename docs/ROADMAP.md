# Roadmap

ASCIIomium is organised around one bounded v0.1 milestone followed by optional research/optimisation lanes. The ordering deliberately proves each transport layer independently before combining them.

## Phase A — Build and terminal foundation

- **#2** — bootstrap C++20/CMake and pin CEF.
- **#3** — terminal session ownership, VT setup, geometry, alternate-screen lifecycle.
- **#4** — pure offline framebuffer-to-half-block renderer and terminal-cell model.
- **#5** — deterministic 16/256/512/1024/true-colour quantisers.
- **#6** — full-frame VT emitter/reference path.
- **#7** — deterministic local web fixtures and benchmark schema.

### Phase-A exit

Before CEF is required, ASCIIomium can take an ordinary image and display it correctly as coloured half-block terminal output in Windows Terminal with clean teardown.

## Phase B — Browser pixels

- **#8** — CEF windowless/OSR CPU paint capture.
- **#9** — live CEF-to-terminal pipeline with newest-frame/coalescing semantics.
- **#10** — one authoritative resize/viewport/aspect/coordinate transform.

### Phase-B exit

An animated local webpage renders live as terminal glyphs. Input is not yet required, but resize and frame pacing are controlled and diagnosable.

## Phase C — Browser interaction

- **#11** — keyboard/UTF-8/VT input decoding and CEF key forwarding.
- **#12** — VT SGR mouse parsing, click/move/wheel mapping.
- **#13** — browser profile persistence, navigation, popup/download/permission policy.

### Phase-C exit

A user can interact with fixture pages: type, click, hover where supported, scroll, navigate, and retain/clear profile state intentionally.

## Phase D — Runtime efficiency and evidence

- **#14** — differential terminal-frame emitter.
- **#15** — structured runtime telemetry and benchmark reporting.
- **#16** — lifecycle hardening and 30-minute soak.
- **#17** — Windows CI and distributable packaging.
- **#18** — v0.1 integration/acceptance/evidence gate.

### v0.1 definition of done

A user can build or extract ASCIIomium, run it inside Windows Terminal, browse an ordinary webpage represented entirely by Unicode/VT terminal output, type/click/scroll/navigate/resize, and exit cleanly. The result has reproducible fixtures, benchmark data, screenshots, known limitations, and a portable Windows artifact.

## Phase E — Visual research

These issues do **not** block v0.1.

- **#19** — perceptual colour, gamma-correct sampling, stable dithering.
- **#20** — quadrant/Braille/density/adaptive glyph encoders.
- **#22** — hybrid raster + semantic text research.

The governing rule is visual evidence plus temporal/output-cost evidence. The theoretically most accurate algorithm is not automatically the preferred aesthetic.

## Phase F — Performance and usability research

- **#21** — CEF `OnAcceleratedPaint` / D3D11 shared-texture evaluation.
- **#23** — dirty-region projection, SIMD, bounded parallel CPU conversion.
- **#24** — clipboard/context menus/richer keys/IME/terminal-selection coexistence.

These should be driven by measurements from #15 and limitations observed at #18.

## Phase G — Separate native-pane experiment

- **#25** — investigate a Windows Terminal fork that hosts CEF/ASCIIomium as a first-class pane type.

This is deliberately distinct from ASCIIomium's core identity. Even if a native pane proves excellent, the terminal-stream Unicode/ANSI renderer remains a feature rather than a temporary bootstrap mechanism.

## Dependency sketch

```text
#2
|\
| +--> #4 --> #5 --+
|        \           |
|         +--------> #6 ----+
|                         \  |
+--> #3 -------------------\-+--> #9 --> #10 --> #12 --+
|                           /       |       \            |
+--> #7 --> #8 ------------+        +------> #11 --------+--> #13
                                      
#6 + #9 ------------------------------> #14 --> #15 --> #16
#2 + settled runtime -------------------------------> #17
#2..#17 -------------------------------------------> #18

#18 --> #19/#20/#21/#22/#23/#24
#18 (+ #21 context) -------------------------------> #25
```

The ASCII sketch is illustrative rather than exhaustive; individual issue bodies carry authoritative dependency notes.

## Parallelism

After #2, several lanes can proceed concurrently:

- terminal runtime (#3);
- pure renderer (#4 then #5);
- fixture/benchmark work (#7);
- CEF browser integration (#8 once the pinned dependency is available).

Converge those lanes at #9. Do not start broad optimisation work merely because one lane is temporarily blocked; use the dependency graph to advance another independently verifiable subsystem.

## Review rule

If implementation evidence invalidates a roadmap assumption, update `docs/DECISIONS.md`, the affected issue(s), and this roadmap in the same change. Do not preserve obsolete planning text for appearance's sake.