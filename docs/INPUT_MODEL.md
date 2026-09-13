# Input Model

## Goal

ASCIIomium should feel like an interactive browser despite the terminal being the display/input transport. Keyboard, pointer, click, wheel, focus, and resize events must be mapped back into CEF without page-specific knowledge.

## Terminal mouse mode

Windows Terminal supports mouse input for VT applications. The programme should use terminal mouse tracking rather than Win32 `MOUSE_EVENT_RECORD` assumptions for the primary Windows Terminal path.

Baseline target:

- request SGR mouse reporting (`1006`);
- use button-event tracking (`1002`) or all-motion tracking (`1003`) when pointer hover/move is needed;
- parse press/release/move/wheel reports from stdin;
- preserve `Shift` as the user's terminal-selection escape hatch where Windows Terminal provides that convention.

Because Windows Terminal/ConPTY input behaviour has had historical edge cases, keep input parsing isolated and add an explicit diagnostics mode that prints decoded events instead of forwarding them.

## Coordinate transform

Terminal mouse reports are cell-addressed. CEF expects browser-view coordinates.

Let:

```text
terminal cell = (cx, cy)
terminal grid = (cols, rows)
CEF viewport  = (width_px, height_px)
```

A baseline mapping is:

```text
x = ((cx + 0.5) / cols) * width_px
y = ((cy + 0.5) / rows) * height_px
```

This must use the same effective visible viewport transform as the renderer. If the renderer letterboxes, crops, or reserves UI rows, input mapping must apply the inverse transform and reject coordinates outside the browser viewport.

Half-block visual sampling does not double mouse rows: pointer events still originate at terminal-cell resolution unless a future pixel-addressed mouse protocol is available.

## CEF forwarding

CEF provides host methods for windowless browser input including:

- `SendMouseMoveEvent`;
- `SendMouseClickEvent`;
- `SendMouseWheelEvent`;
- `SendKeyEvent`.

Mouse coordinates are relative to the upper-left corner of the browser view. The input subsystem should create one canonical internal event representation, then translate it into these CEF calls.

## Keyboard path

The keyboard parser must distinguish:

- printable UTF-8 text;
- Enter/Tab/Backspace/Escape;
- arrows/Home/End/PageUp/PageDown;
- function keys where practical;
- Ctrl/Alt/Shift modifiers;
- key press/release limitations imposed by terminal protocols.

Do not try to perfectly emulate native Chromium keyboard semantics in milestone 1. Establish correct typing, editing keys, navigation keys, and common modifiers first, and document remaining gaps.

A future Windows-specific path may use richer console APIs when launched outside ConPTY, but it must not complicate the Windows Terminal baseline.

## Text input and IME

IME/composition is explicitly deferred from the first interactive milestone. CEF exposes composition-related hooks, but a terminal transport may not preserve enough native key/composition information for transparent IME behaviour.

The architecture must not preclude later IME work; keep key decoding and text injection separate.

## Focus

On startup/focus acquisition, notify CEF that the browser is focused. On terminal suspension or loss of active viewing state where detectable, forward focus loss.

Focus bugs are especially visible in text fields, keyboard shortcuts, and caret rendering; include focus cases in manual verification.

## Scrolling

Mouse wheel events should map to CEF wheel events. Provide a configurable scale because terminal wheel deltas and Chromium expectations may feel too coarse or too fine.

Keyboard PageUp/PageDown should normally be forwarded to Chromium rather than consumed by the terminal application while browser mode is active.

## Terminal resize

When columns/rows change:

1. recompute terminal render geometry;
2. decide whether CEF viewport dimensions remain fixed or change according to configured browser-pixels-per-cell policy;
3. call `WasResized()` when the CEF view dimensions change;
4. invalidate/re-render the entire terminal frame;
5. update the coordinate transform atomically so input never uses stale geometry.

## Clipboard

Milestone 1 may rely on browser keyboard shortcuts where supported. Native terminal selection/copy and browser-page selection can conflict while mouse tracking is enabled.

Later work should explicitly define:

- browser copy/paste;
- terminal-output selection/copy;
- a modifier to temporarily bypass browser mouse capture;
- clipboard permission handling in CEF.

## Diagnostics

Provide an input debug mode displaying at minimum:

```text
raw bytes -> decoded event -> terminal coordinates -> browser coordinates -> CEF event type/modifiers
```

This is essential because terminal input failures otherwise appear indistinguishable from browser/UI failures.