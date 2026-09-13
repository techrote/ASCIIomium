# Agent Context

## Mission

Build **ASCIIomium**: an interactive Chromium/CEF browser whose visual output is converted into a quantised Unicode/VT terminal representation suitable for Windows Terminal.

The core novelty is not HTML parsing. Chromium renders the real webpage; ASCIIomium turns the resulting framebuffer into terminal cells and maps terminal input back into the browser.

## Canonical first vertical slice

```text
CEF OSR -> BGRA frame -> resize/sample -> colour quantise -> half-block cell encode -> VT output
                                                                    ^
Windows Terminal keyboard/mouse -> input parser -> coordinate map -> CEF input events
```

A first useful build must be able to:

1. launch inside Windows Terminal;
2. initialise CEF in windowless/off-screen mode;
3. navigate to a configurable URL;
4. display the live page using coloured Unicode terminal cells;
5. resize with the terminal;
6. accept keyboard input;
7. accept at least click, pointer movement, wheel, and basic modifier input;
8. shut down cleanly and restore terminal state.

## Architectural constraints

- Chromium/CEF owns web semantics and layout. Do not build a parallel HTML/CSS renderer for the baseline.
- Do not inject scripts into pages merely to make rendering work.
- Keep browser integration, image/cell conversion, terminal output, and input mapping as separable modules.
- Start with the CPU `OnPaint` path. Treat CEF accelerated shared-texture painting as a later optimisation, not a prerequisite.
- Use deterministic conversion routines with unit-testable pure functions where possible.
- The baseline cell representation is foreground RGB + background RGB + Unicode half-block glyph.
- Quantised 1024-colour output uses 24-bit SGR after quantisation; indexed SGR mode is limited to standard indexed palettes such as 256 colours.
- Alternate-screen-buffer operation is preferred for the interactive viewer.
- Always restore cursor visibility, mouse modes, colour/style state, and screen-buffer state on normal exit and best-effort abnormal exit.
- Windows Terminal is the primary target. Other terminals are optional compatibility work.

## Development strategy

Work issue-by-issue in dependency order from `ROADMAP.md`. For each issue:

1. read the relevant reference docs;
2. state any assumptions that differ from repository decisions;
3. implement the smallest complete change;
4. add deterministic tests or fixtures where applicable;
5. run the verification commands appropriate to the change;
6. capture benchmark or visual evidence for rendering/performance changes;
7. update docs if implementation reality invalidates an architectural assumption.

Do not silently expand scope into a Windows Terminal fork, custom Chromium fork, semantic DOM renderer, or GPU rewrite before the baseline vertical slice has measurable evidence showing why it is needed.

## Quality priorities

In order:

1. correctness and clean teardown;
2. interactive end-to-end operation;
3. deterministic rendering;
4. visual quality;
5. output bandwidth / frame latency;
6. CPU/GPU efficiency;
7. additional glyph engines and aesthetic modes;
8. experimental semantic/hybrid rendering.

## Terminology

- **source frame**: CEF-rendered browser bitmap/texture.
- **terminal frame**: complete logical grid of terminal cells.
- **cell**: glyph + foreground colour + background colour + optional style metadata.
- **subpixel/sample**: one source-derived colour sample represented inside a cell.
- **quantiser**: maps source RGB into a constrained output gamut.
- **encoder**: maps sampled/quantised values to glyph/foreground/background cells.
- **emitter**: serialises cell deltas into VT/ANSI sequences.
- **OSR**: CEF off-screen/windowless rendering.

## Success criterion for the programme's first milestone

A user can run ASCIIomium in Windows Terminal, browse a normal interactive webpage represented entirely by terminal glyphs and colours, click/type/scroll through it, resize the terminal, and exit without leaving the terminal in a damaged state.