# Input Model

## Goal

ASCIIomium should feel like an interactive browser despite the terminal being the display/input transport. Keyboard, pointer, click, wheel, focus, and resize events must be mapped back into CEF without page-specific knowledge.

## Terminal mouse mode

Issue #12 uses terminal mouse reporting rather than Win32 `MOUSE_EVENT_RECORD`
semantics for the Windows Terminal path. While the interactive browser owns the
terminal it enables:

```text
CSI ? 1003 h    all-motion tracking
CSI ? 1006 h    SGR extended mouse encoding
```

All-motion mode is chosen because ordinary browser hover matters even when no
button is held. SGR reports arrive through the **same bounded incremental byte
parser** as #11 keyboard input; there is no second stdin reader.

The parser accepts `CSI < Cb ; Cx ; Cy M/m`, converts 1-based protocol
coordinates to zero-based terminal cells, decodes Shift/Alt/Ctrl, button
press/release, motion and wheel notches, and preserves raw bytes for
diagnostics. Malformed/incomplete reports stay subject to the existing bounded
pending-buffer contract.

Terminal-session RAII disables 1006/1003 again before leaving the alternate
screen. Shift-based Windows Terminal selection bypass is an environment-specific
behaviour and remains a manual acceptance check rather than an automated claim.

## Coordinate transform

Terminal mouse reports are cell-addressed. CEF expects browser-view pixel
coordinates. Issue #10 establishes `geometry::ViewportTransform` as the sole
authority for this mapping; #12 must consume it rather than reproduce formulas.

For stretch mode, the intuitive mapping is still the terminal-cell centre:

```text
x ~= ((cx + 0.5) / cols) * width_px
y ~= ((cy + 0.5) / rows) * height_px
```

The implementation generalises that through the half-block logical sample
lattice and the transform's content rectangle. In contain mode, a cell centre
inside letterbox space has no browser coordinate and is rejected. Browser
right/bottom bounds are exclusive and final integer CEF coordinates are
clamped to `[0,width-1]` / `[0,height-1]`.

Half-block visual sampling doubles logical sample rows, **not mouse rows**:
pointer events remain terminal-cell granular and use the centre spanning both
half-block samples. No sub-cell pointer precision is fabricated.

The reciprocal browser-pixel -> terminal-cell helper exists for diagnostics and
future UI work. Both directions are part of the same immutable transform.

## CEF forwarding

CEF provides host methods for windowless browser input including:

- `SendMouseMoveEvent`;
- `SendMouseClickEvent`;
- `SendMouseWheelEvent`;
- `SendKeyEvent`.

Mouse coordinates are relative to the upper-left corner of the browser view. The input subsystem should create one canonical internal event representation, then translate it into these CEF calls.

Issue #12 implements a transport-neutral `PointerEvent` with move,
button-press, button-release and wheel kinds. `CefPointerDispatch` is a
separate browser-side translation layer. It maps the canonical terminal cell
through the current immutable #10 `ViewportTransform`, then forwards with
`SendMouseMoveEvent`, `SendMouseClickEvent` or `SendMouseWheelEvent`.
Pressed-button and keyboard modifier state are represented in the CEF event
flags. Single clicks use deterministic click-count 1.


## Keyboard path

Issue #11 implements the baseline keyboard path as:

```text
Windows Terminal VT/UTF-8 byte stream
    -> incremental VtInputParser
    -> transport-neutral KeyboardEvent
    -> CEF key translator
    -> CefBrowserHost::SendKeyEvent
```

The parser is incremental and bounded: reads may split UTF-8 code points or VT
escape sequences at arbitrary byte boundaries. An ambiguous lone `ESC` is held
briefly by the live loop, then resolved after a 35 ms quiet period. Parser state
is capped at 64 bytes so malformed/incomplete sequences cannot grow without
bound.

Implemented milestone keys:

- printable UTF-8 text, including non-BMP code points represented to CEF as a
  UTF-16 surrogate pair;
- Enter, Tab, Backspace and ordinary Escape;
- arrows, Home/End, PageUp/PageDown, Insert/Delete;
- Shift+Tab;
- xterm-style modifier parameters for navigation keys;
- Ctrl control-byte combinations including Ctrl+A/C/V;
- Alt-prefixed printable/control combinations;
- F1-F12 through common SS3/CSI encodings.

The terminal transport does **not** report native physical key-up transitions
or a complete hardware scan-code history. ASCIIomium therefore emits synthetic
RAWKEYDOWN/KEYUP pairs for logical/navigation/shortcut keys and does not claim
physical-key fidelity. Plain Unicode text stays a separate CHAR path; it is not
reconstructed into fake physical keystrokes.

Ordinary printable Shift state is generally already consumed by the terminal
when it produces the resulting character, so `A` may arrive as text without a
distinct Shift modifier. AltGr/IME composition fidelity is deferred to #24.

### Application quit chord

Live browser mode uses immediate/raw VT keyboard input.
`ENABLE_PROCESSED_INPUT` is disabled there so Ctrl+C reaches Chromium instead
of being consumed by the Windows console control handler. The browser-mode
application quit chord is **Ctrl+Alt+Q** (normally encoded as `ESC` + Ctrl+Q).

The non-browser terminal diagnostic retains its existing Ctrl+C control-handler
exit path.

A future Windows-specific path may use richer console APIs when launched outside
ConPTY, but it must not complicate the Windows Terminal baseline.

## Text input and IME

IME/composition is explicitly deferred from the first interactive milestone. CEF exposes composition-related hooks, but a terminal transport may not preserve enough native key/composition information for transparent IME behaviour.

The architecture must not preclude later IME work; keep key decoding and text injection separate.

## Focus

On startup/focus acquisition, notify CEF that the browser is focused. On terminal suspension or loss of active viewing state where detectable, forward focus loss.

Focus bugs are especially visible in text fields, keyboard shortcuts, and caret rendering; include focus cases in manual verification.

## Scrolling

SGR wheel reports are canonicalised as logical notches. The live default maps
one notch to a CEF delta of **120**, configurable with `--wheel-delta N`.
Vertical and horizontal logical wheel axes are represented separately and the
event is delivered at the browser coordinate under the reported terminal cell.

Keyboard PageUp/PageDown are forwarded to Chromium rather than consumed by the
terminal application while browser mode is active.

## Terminal resize

The milestone-1 policy keeps the CEF viewport fixed while terminal rows/columns
change.

On resize:

1. validate the new terminal dimensions against bounded geometry limits;
2. build a new immutable `ViewportTransform`;
3. publish it through `ViewportState`, advancing the geometry generation only
   when something changed;
4. force a full terminal refresh using that snapshot;
5. leave CEF pixel dimensions unchanged for terminal-only resizes;
6. request `NotifyScreenInfoChanged()/WasResized()` only if a future geometry
   policy actually changes browser width/height.

Input code snapshots the same published transform, preventing render/input
coordinate desynchronisation. Temporary invalid/zero geometry is ignored rather
than propagated into CEF or pathological allocations.

## Clipboard

Milestone 1 may rely on browser keyboard shortcuts where supported. Native terminal selection/copy and browser-page selection can conflict while mouse tracking is enabled.

Later work should explicitly define:

- browser copy/paste;
- terminal-output selection/copy;
- a modifier to temporarily bypass browser mouse capture;
- clipboard permission handling in CEF.

## Diagnostics

`--input-debug` covers the unified keyboard/pointer stream. Pointer diagnostics
show:

```text
raw SGR bytes
  -> canonical pointer event + zero-based terminal cell/modifiers
  -> viewport geometry generation
  -> browser coordinate or letterbox rejection
  -> CEF move/click/wheel dispatch
```

The generic `--input-hex` test aid can inject exact byte sequences through the
same parser in headless CI; it is not a mouse-specific bypass. `--input-script`
continues to exercise the ordinary UTF-8 keyboard path. The two injection modes
are mutually exclusive.

The debug log contains immediate operator bytes escaped for visibility. It does
not inspect cookies or page secrets, but typed input itself can be sensitive;
do not enable it while entering credentials.
