# Architecture

## System decomposition

ASCIIomium is a browser/display adapter with four primary subsystems.

```text
+----------------------+      +----------------------+      +----------------------+
| Browser / CEF OSR    | ---> | Render pipeline      | ---> | Terminal backend     |
|                      |      |                      |      |                      |
| navigation           |      | sample/rescale       |      | cell diff            |
| cookies/storage      |      | quantise             |      | VT/ANSI emit         |
| JS/CSS/layout        |      | glyph selection      |      | alt screen           |
| OnPaint callbacks    |      | terminal frame       |      | cursor/mouse modes   |
+----------+-----------+      +----------------------+      +----------+-----------+
           ^                                                                  |
           |                                                                  v
           |               +----------------------+                 Windows Terminal
           +---------------| Input subsystem      |<-------------------------+
                           |                      |
                           | VT key parser        |
                           | mouse parser         |
                           | coordinate transform |
                           | CEF SendXXX events   |
                           +----------------------+
```

## 1. Browser subsystem

CEF should initially run in windowless/off-screen rendering mode.

Responsibilities:

- initialise/shutdown CEF;
- create one browser instance;
- maintain browser viewport dimensions;
- receive `CefRenderHandler::OnPaint` callbacks;
- expose the latest source frame plus dirty regions to the render pipeline;
- forward keyboard, mouse, wheel, focus, and resize events;
- handle navigation and a minimal set of browser lifecycle events.

The first implementation should use the CPU paint callback rather than accelerated shared-texture rendering. The CPU path is easier to inspect, test, and reason about. `OnAcceleratedPaint` is a later optimisation lane once conversion itself is proven.

## 2. Render pipeline

The render pipeline must be browser-agnostic. It consumes an image plus target terminal geometry and produces a logical terminal frame.

Suggested interfaces:

```cpp
struct Rgba8 { uint8_t r, g, b, a; };

struct TerminalCell {
    char32_t glyph;
    Rgba8 fg;
    Rgba8 bg;
};

struct TerminalFrame {
    int columns;
    int rows;
    std::vector<TerminalCell> cells;
};

TerminalFrame render_frame(
    ImageView source,
    TerminalGeometry target,
    const RenderConfig& config);
```

The first encoder should be half-block based. Later encoders can implement quadrants, Braille, density glyphs, edge-aware glyph selection, or hybrid text-aware behaviour behind the same interface.

## 3. Terminal backend

Responsibilities:

- detect usable terminal dimensions;
- enable virtual terminal output;
- enter/leave alternate screen;
- hide/show cursor;
- configure/restore terminal input modes;
- emit SGR foreground/background colour;
- position cursor efficiently;
- diff current and previous terminal frames;
- avoid redundant SGR state changes and unchanged cells;
- respond to resize.

Do not tie rendering correctness to one optimisation strategy. A deliberately inefficient full-frame emitter should exist early as a reference implementation; differential emission can then be verified against it.

## 4. Input subsystem

Responsibilities:

- decode keyboard input received through the terminal;
- decode Windows Terminal VT mouse reports when mouse tracking is enabled;
- translate terminal cell coordinates into browser viewport coordinates;
- apply modifiers/button state;
- call CEF host input APIs;
- correctly handle wheel input and focus;
- restore terminal mouse/input modes on exit.

The mapping must account for the logical browser viewport and terminal cell geometry, not assume one terminal cell equals one browser pixel.

## Viewport model

The browser viewport and terminal grid are deliberately decoupled.

Example:

```text
terminal:       160 columns x 50 rows
half-block:     160 x 100 logical colour samples
CEF viewport:   1280 x 800 pixels
```

The renderer resamples the CEF viewport into its logical sample grid. Mouse coordinates map through the inverse transform.

The browser viewport may initially use a fixed virtual resolution. A later mode may derive a target viewport from terminal dimensions and configurable browser-pixels-per-cell.

## Frame ownership and concurrency

CEF callbacks and terminal rendering should not share mutable frame memory without an explicit ownership model.

Recommended baseline:

- `OnPaint` copies/updates into a browser-frame buffer guarded by a small synchronisation primitive;
- render loop snapshots a frame generation counter and obtains a stable view/copy;
- terminal emitter owns previous/current terminal frames;
- input runs independently but reads immutable geometry state.

Avoid unbounded frame queues. If conversion cannot keep up with Chromium, drop stale visual frames and render the newest state. This is an interactive display, not a video encoder.

## Process model

CEF retains Chromium's normal multi-process architecture. ASCIIomium should not disable the CEF/Chromium sandbox or process separation merely to simplify the POC unless CEF's supported sample/bootstrap requirements force a temporary development exception that is documented and removed before a distributable build.

## Future optimisation lanes

Only after baseline profiling:

- dirty-rectangle-aware conversion;
- SIMD resampling/quantisation;
- worker-thread tile conversion;
- `OnAcceleratedPaint` / D3D11 shared-texture path;
- compute-shader/GPU cell analysis;
- external begin-frame pacing;
- Windows Terminal integration/fork experiments;
- direct terminal graphics protocols as comparison baselines.

Each optimisation must preserve a reference path for visual/correctness comparison.