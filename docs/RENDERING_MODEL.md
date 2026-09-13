# Rendering Model

## Baseline: half-block cells

The canonical first renderer uses the Unicode upper-half block `▀` (U+2580).

For each terminal cell:

- sample/aggregate one colour for the upper half;
- sample/aggregate one colour for the lower half;
- encode upper colour as foreground;
- encode lower colour as background;
- emit `▀`.

This produces two independently coloured vertical samples per terminal cell with no dependency on custom fonts.

A lower-half-block implementation is equivalent if foreground/background assignment is swapped. Keep one convention globally to simplify diffs and tests.

## Sampling

Do not simply pick one source pixel unless operating in a diagnostic nearest-neighbour mode. Source pixels should be mapped into terminal sample regions and reduced using a configurable filter.

Initial filters:

1. nearest neighbour — diagnostic/reference for exact coordinate tests;
2. box average — baseline quality mode;
3. optional bilinear resize — useful comparison.

For terminal geometry `C x R`, half-block mode has a logical sample lattice of `C x (2R)`.

The browser image should be mapped to that lattice using the viewport's full visible area unless cropping/letterboxing is explicitly selected.

## Cell aspect ratio

Terminal cells are not square. Correct visual proportions require a scale parameter.

Do not hard-code a universal ratio. Support:

```text
--cell-aspect <width/height>
```

and a practical default tuned for common Windows Terminal monospace fonts. Later work may infer actual pixel dimensions through terminal/window APIs or user calibration.

The renderer must keep coordinate mapping and visual sampling consistent with the chosen geometry.

## Colour modes

### True colour reference

No quantisation beyond 8-bit RGB. Emit colours with:

```text
ESC[38;2;<r>;<g>;<b>m
ESC[48;2;<r>;<g>;<b>m
```

This is the fidelity/control mode.

### 256 indexed

Map source colours to the standard xterm-style 256-colour palette and emit `38;5;n` / `48;5;n`.

This is both a compatibility target and a deliberately stronger retro aesthetic.

### 512 / 1024 quantised RGB

Quantise to a fixed deterministic gamut, then emit the resulting quantised RGB values using true-colour SGR.

The initial 1024-colour candidate should be benchmarked against perceptual alternatives. A simple baseline is a 3/4/3-bit RGB cube:

```text
R: 8 levels
G: 16 levels
B: 8 levels
= 1024 colours
```

Do not treat that allocation as sacred; perceptual or approximately uniform-lightness palettes may look better. Preserve the original/simple quantiser as a reference.

## Gamma / colour space

Averaging encoded sRGB channel values is not physically or perceptually correct. The project should eventually compare:

- direct sRGB averaging (cheap baseline);
- linear-light averaging followed by sRGB encoding;
- perceptual colour-space quantisation for palette selection.

The first POC may use direct channel averaging for simplicity, but rendering tests must make the choice explicit so later changes are measurable rather than accidental.

## Dithering

Dithering is a later mode, not a baseline requirement.

Candidate modes:

- none;
- ordered Bayer matrices;
- blue-noise thresholding;
- error diffusion (Floyd-Steinberg or similar).

Temporal stability matters. Error diffusion or adaptive palettes can produce distracting shimmer on moving/scrolling pages. Prefer deterministic spatial dithering and compare frame-to-frame stability.

## Future glyph encoders

All must implement the same cell-frame interface so they can be A/B tested.

### Quadrant blocks

Use Unicode quadrant/block combinations to represent a 2x2 occupancy/pattern with two colours. Potentially better edge representation at higher CPU cost.

### Braille

Unicode Braille offers a 2x4 binary dot pattern per cell. This can increase apparent spatial resolution, but each glyph still has one foreground/background relationship, making colour assignment a constrained optimisation problem.

### Density / ASCII-like glyphs

Use luminance/density glyph ramps for explicit ASCII-art aesthetics. This is intentionally stylistic rather than the default fidelity path.

### Adaptive glyph selection

Choose from a bounded glyph atlas based on local edge/coverage error. Candidate cell selection should minimise a cost function combining colour reconstruction error, edge error, and temporal instability.

### Hybrid text-aware rendering

Future research may obtain actual textual/accessibility information from Chromium and substitute real terminal text for rasterised text regions while retaining raster-to-glyph graphics elsewhere. This is not required for the first usable browser and must not become a dependency of the core renderer.

## Terminal-frame diffing

A logical `TerminalFrame` should be generated independently of output serialisation.

The emitter compares current and previous cells and may:

- skip unchanged cells;
- group contiguous changed runs;
- reposition the cursor rather than rewrite intervening unchanged cells;
- carry foreground/background SGR state across cells;
- avoid re-emitting identical colours;
- reset styles only when needed.

A full repaint mode must remain available for debugging.

## Dirty rectangles

CEF supplies invalidated regions for paint callbacks. Do not optimise around them until the baseline is correct.

Later, dirty regions can be projected into terminal-cell space and used to limit sampling and diff work. Because downsampling kernels may span neighbouring pixels, dirty rectangles should be expanded by the filter support radius before mapping to cells.

## Frame pacing

Do not attempt to render every CEF callback when conversion/output cannot keep up. The viewer should favour low latency:

- retain newest completed source frame;
- coalesce invalidations;
- render at a configurable maximum FPS;
- drop stale intermediate frames;
- record dropped/coalesced frame counts for diagnostics.

Suggested early targets: 10, 20, and 30 FPS modes. A highly readable 15–20 FPS browser is a more useful first target than saturating the terminal with redundant 60 FPS output.