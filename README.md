# ASCIIomium

**A real Chromium browser, rendered through terminal cells.**

ASCIIomium is an experimental Windows terminal browser: Chromium Embedded Framework (CEF) renders a normal webpage off-screen; ASCIIomium converts the resulting pixels into quantised colour plus Unicode glyphs; Windows Terminal displays the result using VT/ANSI escape sequences; keyboard and mouse input are mapped back into CEF.

The project intentionally begins with the simplest end-to-end path that can become genuinely interactive:

```text
CEF / Chromium
    |
    | off-screen paint callbacks
    v
BGRA framebuffer
    |
    | resample + quantise + cell encode
    v
terminal cell grid
(glyph + foreground + background)
    |
    | VT/ANSI output
    v
Windows Terminal
    |
    | keyboard + VT mouse reports
    v
input mapper
    |
    v
CEF browser events
```

The initial visual baseline is **half-block rendering** (`▀`/`▄`): each terminal cell represents two vertically stacked colour samples, one in the foreground colour and one in the background colour. This provides useful apparent resolution while remaining deterministic, simple, and fast enough to optimise.

## Project intent

ASCIIomium is not a text-mode HTML renderer and does not reinterpret page semantics. Chromium remains authoritative for HTML, CSS, JavaScript, layout, fonts, canvas, SVG, media, networking, cookies, storage, and browser behaviour. ASCIIomium is a strange display-and-input adapter attached to a real browser engine.

That distinction is deliberate. The first proof of concept should be able to display and interact with arbitrary ordinary webpages before the project experiments with smarter glyph selection, perceptual quantisation, dithering, DOM-aware text recovery, or GPU-assisted conversion.

## Current bootstrap

The implementation now has a reproducible Windows x64 C++20/CMake foundation, an RAII terminal-runtime layer, a deterministic offline framebuffer-to-cell renderer, explicit true/16/256/512/1024 colour modes, and a reference full-frame VT serializer. It pins CEF `151.3.17+gf059e67+chromium-151.0.7922.138`, verifies the real CEF runtime, can take temporary ownership of a Windows console session, can convert RGBA/BGRA images into fixed-size Unicode half-block `TerminalFrame`s, and can serialize those frames into real indexed/true-colour VT output.

CEF browser initialisation remains deliberately deferred to issue #8.

After building, exercise the terminal layer from Windows Terminal:

```powershell
.\build\bin\DEBUG\asciiomium.exe --terminal-diagnostics
```

Render the synthetic colour ramp directly into Windows Terminal:

```powershell
.\build\bin\DEBUG\asciiomium_vt_preview.exe --ramp --colors 1024
```

Or render the bundled image fixture:

```powershell
.\build\bin\DEBUG\asciiomium_vt_preview.exe `
  --input .\fixtures\render\quad_2x4.ppm `
  --colors 1024 --filter nearest
```

`Ctrl+C` exits and restores the original screen. Resize the window while the preview is active to exercise full-frame geometry changes.

Inspect the logical cells structurally:

```powershell
.\build\bin\DEBUG\asciiomium_frame_dump.exe `
  --input .\fixtures\render\quad_2x4.ppm `
  --columns 2 --rows 2 --filter nearest --colors 1024
```

Or inspect the exact escaped VT payload without executing it:

```powershell
.\build\bin\DEBUG\asciiomium_frame_dump.exe `
  --input .\fixtures\render\quad_2x4.ppm `
  --columns 2 --rows 2 --filter nearest --colors 256 `
  --vt-annotated
```

Generate an SVG comparison of all colour modes with:

```powershell
.\build\bin\DEBUG\asciiomium_color_ramp.exe --output .\build\color-modes.svg
```

See [`docs/BUILDING.md`](docs/BUILDING.md), [`docs/TERMINAL_RUNTIME.md`](docs/TERMINAL_RUNTIME.md), [`docs/OFFLINE_RENDERER.md`](docs/OFFLINE_RENDERER.md), [`docs/COLOR_MODES.md`](docs/COLOR_MODES.md), and [`docs/VT_EMITTER.md`](docs/VT_EMITTER.md) for the current implementation contracts.

## Colour model

"ANSI" does **not** mean a 16-colour restriction here. ASCIIomium implements:

- 16-colour indexed compatibility / deliberate extreme-retro mode;
- xterm 256-colour indexed mode;
- deterministic 512-colour 3/3/3 RGB quantisation;
- deterministic **1024-colour 3/4/3 RGB quantisation**;
- unrestricted 24-bit RGB as a fidelity/reference mode.

The 512/1024 modes remain RGB colours and use true-colour SGR after quantisation; terminals do not expose standardized 512/1024-entry indexed palettes. Indexed metadata is retained only for the real 16/256 modes so the reference emitter can choose `38;5`/`48;5` where appropriate.

## Non-goals for the first proof of concept

The first milestone does **not** need:

- a Windows Terminal fork;
- HTML/CSS-to-glyph semantic rendering;
- DOM injection or page modification;
- perfect text recovery;
- perfect IME/accessibility support;
- video-grade frame rates;
- arbitrary terminal-emulator compatibility;
- GPU shader conversion;
- custom Chromium patches.

Those are later research or productisation paths after an interactive vertical slice exists.

## Reference pack

Start here before implementation:

- [`docs/AGENT_CONTEXT.md`](docs/AGENT_CONTEXT.md) — compact project authority and execution rules.
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — component boundaries and data flow.
- [`docs/RENDERING_MODEL.md`](docs/RENDERING_MODEL.md) — framebuffer-to-cell algorithms and colour/glyph strategy.
- [`docs/OFFLINE_RENDERER.md`](docs/OFFLINE_RENDERER.md) — implemented image/frame contracts, filters, aspect handling, fixtures, and hard viewport bounds.
- [`docs/COLOR_MODES.md`](docs/COLOR_MODES.md) — implemented palettes, indexed/RGB representation and quantisation definitions.
- [`docs/VT_EMITTER.md`](docs/VT_EMITTER.md) — reference full-frame serialization, SGR compression, UTF-8 and right-margin policy.
- [`docs/INPUT_MODEL.md`](docs/INPUT_MODEL.md) — terminal input to browser-event mapping.
- [`docs/CEF_AND_TERMINAL_REFERENCE.md`](docs/CEF_AND_TERMINAL_REFERENCE.md) — primary technical references and compatibility facts.
- [`docs/QUALITY_AND_BENCHMARKS.md`](docs/QUALITY_AND_BENCHMARKS.md) — measurable quality/performance criteria.
- [`docs/VERIFY.md`](docs/VERIFY.md) — verification ladder.
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — architectural decisions and rationale.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — staged implementation order and issue map.
- [`docs/BUILDING.md`](docs/BUILDING.md) — pinned toolchain and build/test instructions.
- [`docs/TERMINAL_RUNTIME.md`](docs/TERMINAL_RUNTIME.md) — console ownership, resize detection, diagnostics, and teardown.

## Engineering posture

Prefer evidence over cleverness. Get a CPU implementation working first, retain deterministic fixtures, measure every optimisation, and keep CEF/browser behaviour isolated from the terminal renderer so alternative glyph/quantisation engines can be compared without destabilising navigation or input.

The programme is organised as GitHub issues. Each implementation issue contains enough context and acceptance criteria to be handed to a fresh development agent without requiring this conversation.
