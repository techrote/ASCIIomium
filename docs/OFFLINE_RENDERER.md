# Offline half-block renderer

Issue #4 establishes the deterministic browser-independent image-to-cell core.
It deliberately has no CEF, Win32 console, stdout, VT, timing, or input dependency.

## Pipeline

```text
RGBA8/BGRA8 ImageView
        |
        | source mapping + filter
        v
columns x (rows * 2) logical colour samples
        |
        | alpha composite + optional quantizer
        v
TerminalFrame(columns, rows)
        |
        `-- every cell is U+2580 `▀`
            foreground = upper sample
            background = lower sample
```

`TerminalFrame` is a hard viewport. Its storage is exactly `columns * rows` and
out-of-range lookup is rejected. Content beyond that logical viewport cannot
exist in a rendered frame. This is the renderer-side half of the no-wrap
contract discovered during the deliberately tiny Windows Terminal diagnostics
check.

The later VT emitter must preserve the same contract: it must clip before
serialisation and must never rely on the terminal's implicit right-margin wrap
to advance rows. Whether it disables terminal autowrap or instead guarantees
safe explicit cursor positioning is an emitter/session policy; this renderer
does not mutate terminal state.

## Pixel formats

`ImageView` currently accepts packed four-byte pixels in either:

- `RGBA8`;
- `BGRA8` (the useful CEF CPU-paint shape for later integration).

Rows have an explicit byte stride. Source alpha is composited onto the
configured opaque RGB background before filtering output becomes a cell colour.

## Sampling

Two filters are implemented and selected explicitly in `RenderConfig`.

### Nearest

Nearest sampling maps each logical sample centre into source coordinates. This
is primarily a diagnostic/reference path because exact source-to-cell mapping is
obvious in tests.

### Box average

Box average is the baseline quality path. For each logical output sample it:

1. maps the sample's unit rectangle into continuous source coordinates;
2. computes exact rectangular overlap weights with every intersected source
   pixel;
3. alpha-composites each source pixel against the configured background;
4. averages encoded 8-bit sRGB channels using those area weights;
5. mixes the configured background for any fraction of a sample lying in a
   letterbox region.

This is intentionally direct encoded-sRGB averaging for milestone 1. Linear
light/perceptual averaging remains a later measured comparison rather than an
implicit behaviour change.

The area-weighted implementation handles downsampling, upsampling, odd source
dimensions and one-pixel images through the same code path.

## Geometry and cell aspect

The logical half-block lattice is `columns x (rows * 2)`.

Two fit modes are explicit:

- `Stretch` (default): maps the full source to the full lattice. This matches the
  current architecture rule that the complete browser viewport is visible and
  intentionally ignores physical cell aspect because distortion is the chosen
  policy.
- `Contain`: preserves source aspect ratio using letterboxing. The caller's
  `cell_aspect` (`cell width / cell height`) is used when converting between the
  logical half-row lattice and physical terminal proportions.

There is no universal font-cell ratio hard-coded into the renderer. The default
configuration uses `0.5` as a practical starting value, but callers can supply a
measured/calibrated value.

## Colour quantization boundary

Sampling is colour-mode neutral. `RenderConfig::quantizer` accepts a
`ColorQuantizer`; null means the identity/true-colour reference path.

The 256/512/1024-colour work can therefore quantize final sample colours without
changing source mapping, box filtering, alpha behaviour, cell geometry or
golden fixtures.

## Deterministic fixture format

Tests use ASCII PPM (`P3`) files. This is intentional rather than a temporary
PNG decoder:

- tiny fixtures remain human-readable;
- fixtures are ordinary text diffs;
- there is no image-codec dependency in the renderer foundation;
- test inputs can describe exact RGB values without metadata or colour-profile
  ambiguity.

`LoadPpmP3` converts fixtures to RGBA8 with alpha 255. Runtime browser frames do
not use this loader.

## Debug utility

Build `asciiomium_frame_dump`, then for example:

```powershell
.\build\bin\DEBUG\asciiomium_frame_dump.exe `
  --input .\fixtures\render\quad_2x4.ppm `
  --columns 2 --rows 2 --filter nearest
```

The tool prints structured cells (`coordinate`, `U+2580`, `fg`, `bg`) rather
than terminal escape sequences. That keeps image/sampling failures distinct from
future VT-emitter failures.

Useful switches:

```text
--filter nearest|box
--fit stretch|contain
--cell-aspect <width/height>
```

## Golden coverage

`render_golden_tests` checks:

- exact upper/lower colour assignment from the bundled fixture;
- nearest source-coordinate mapping;
- area-weighted box averaging;
- RGBA alpha composition;
- BGRA input ordering;
- odd source dimensions;
- 1x1 source upscaling;
- aspect-correct contain/letterbox geometry;
- pluggable quantizer behaviour;
- deterministic repeated output;
- hard right/bottom/negative frame bounds.
