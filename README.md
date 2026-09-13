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

The implementation now starts with a reproducible Windows x64 C++20/CMake bootstrap. It pins CEF `151.3.17+gf059e67+chromium-151.0.7922.138`, links a deliberately inert `asciiomium.exe` to the real `libcef.dll`, and verifies the compile-time and runtime CEF/Chromium versions before any browser initialisation is introduced.

See [`docs/BUILDING.md`](docs/BUILDING.md) for exact configure, Debug/Release build, test, and `--version` commands.

## Colour model

"ANSI" does **not** mean a 16-colour restriction here. ASCIIomium should support several output modes:

- 16-colour compatibility / deliberate extreme-retro mode;
- xterm 256-colour indexed mode;
- configurable quantised RGB modes such as 512 and **1024 colours**;
- unrestricted 24-bit RGB as a fidelity/reference mode.

A 1024-colour mode is emitted using true-colour SGR sequences after quantisation; terminals do not expose a standardized native 1024-entry indexed palette. This gives the intended slightly posterised/aliased aesthetic while retaining predictable colour quality.

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
- [`docs/INPUT_MODEL.md`](docs/INPUT_MODEL.md) — terminal input to browser-event mapping.
- [`docs/CEF_AND_TERMINAL_REFERENCE.md`](docs/CEF_AND_TERMINAL_REFERENCE.md) — primary technical references and compatibility facts.
- [`docs/QUALITY_AND_BENCHMARKS.md`](docs/QUALITY_AND_BENCHMARKS.md) — measurable quality/performance criteria.
- [`docs/VERIFY.md`](docs/VERIFY.md) — verification ladder.
- [`docs/DECISIONS.md`](docs/DECISIONS.md) — architectural decisions and rationale.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — staged implementation order and issue map.
- [`docs/BUILDING.md`](docs/BUILDING.md) — pinned toolchain and build/test instructions.

## Engineering posture

Prefer evidence over cleverness. Get a CPU implementation working first, retain deterministic fixtures, measure every optimisation, and keep CEF/browser behaviour isolated from the terminal renderer so alternative glyph/quantisation engines can be compared without destabilising navigation or input.

The programme is organised as GitHub issues. Each implementation issue contains enough context and acceptance criteria to be handed to a fresh development agent without requiring this conversation.
