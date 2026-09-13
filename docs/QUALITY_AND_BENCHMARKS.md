# Quality and Benchmarks

ASCIIomium needs measurable visual and performance evidence because many later changes will trade fidelity, terminal bandwidth, latency, and CPU cost against each other.

## Benchmark dimensions

Capture at minimum:

- source viewport size;
- terminal columns/rows;
- glyph encoder;
- colour mode / palette size;
- sampling filter;
- frame cap;
- source-frame rate;
- rendered terminal-frame rate;
- emitted terminal-frame rate;
- source frames coalesced/dropped;
- render conversion time;
- diff time;
- VT serialisation time;
- bytes emitted per frame and per second;
- process CPU time / utilisation;
- process working set / private memory where practical;
- end-to-end interaction latency by reproducible proxy.

## Required fixture pages

Create local deterministic HTML fixtures rather than relying only on changing public websites.

### Fixture A: flat UI

Contains:

- solid backgrounds;
- borders;
- buttons;
- text at several sizes;
- common form controls;
- icons/simple SVG.

Purpose: cell edges, text readability, colour transitions.

### Fixture B: gradients and colour ramps

Contains RGB/HSV ramps, low-contrast gradients, neutral greys, skin-tone-like ramps, and saturated patches.

Purpose: quantiser banding and dithering assessment.

### Fixture C: raster imagery

Contains stable bundled images with fine detail, faces/natural content, dark scenes, and high-frequency textures.

Purpose: perceived image fidelity.

### Fixture D: motion/scroll

Contains CSS animation, a scrolling region, moving gradients, and a timer.

Purpose: temporal stability, output bandwidth, dropped frames.

### Fixture E: input

Contains text boxes, buttons, hover targets, draggable/scrollable areas, checkboxes, selects, and keyboard focus indicators.

Purpose: input mapping and focus tests.

## Golden tests

Pure render stages should be deterministic. Store small source images and expected cell-grid outputs for:

- nearest-neighbour half-block mapping;
- box averaging;
- true-colour encoding;
- 256-colour mapping;
- 1024-colour mapping;
- odd/even source dimensions;
- alpha compositing against configured background;
- resize/crop/letterbox transform math.

Do not store giant terminal dumps when a compact structured cell fixture is sufficient.

## Visual evidence

For changes to quantisation, dithering, glyph selection, or sampling, capture:

1. source image/page;
2. terminal-render screenshot;
3. configuration used;
4. benchmark metrics;
5. short interpretation of the tradeoff.

Aesthetic changes should never be accepted solely from an algorithm name or synthetic metric.

## Reference render paths

Maintain:

- a straightforward full-frame terminal emitter;
- a deterministic CPU renderer;
- a no-quantisation true-colour mode.

These are comparison controls for optimised/differential/quantised paths.

## Early performance budgets

These are directional targets, not acceptance gates until measured on representative hardware:

- 160x50 half-block terminal at >= 15 displayed FPS on a typical desktop CPU;
- p95 conversion+diff+serialisation comfortably below one 50 ms frame budget at 20 FPS;
- no unbounded frame queue growth;
- no progressive memory growth during a 30-minute static/scroll test;
- terminal output bandwidth reduced substantially by differential emission on mostly-static pages.

## Interaction quality gates

Before calling milestone 1 usable:

- typing into a text field is reliable;
- click target mapping is accurate enough across all four corners and centre;
- wheel scrolling works in both page and nested scroll regions where CEF supports it;
- browser resize does not leave stale rows/columns;
- Ctrl+C / normal quit restores terminal state;
- repeated launch/quit cycles do not corrupt the shell session.

## Benchmark command philosophy

Benchmarks should be scriptable and machine-readable. Prefer a command such as:

```text
asciiomium --fixture flat-ui --benchmark 10s --renderer halfblock --colors 1024 --json out.json
```

over ad-hoc stopwatch observations.

The exact CLI can evolve, but benchmark data must include enough configuration to reproduce the run.