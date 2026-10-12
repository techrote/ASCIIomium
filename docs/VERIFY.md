# Verification

Verification is staged so a developer can prove each layer independently before blaming the browser, terminal, or renderer for another layer's failure.

## V0 — Build and unit tests

Required for every code change once the build exists:

```text
configure
build
unit tests
```

The concrete commands belong in the root README/build files once selected.

Unit-test pure logic including:

- colour quantisers;
- sRGB/RGB conversion helpers;
- sample-region mapping;
- half-block cell generation;
- terminal-frame equality/diff planning;
- VT SGR serialisation;
- mouse-coordinate transforms;
- VT mouse parser;
- incremental VT keyboard/UTF-8 parser;
- canonical keyboard-event to CEF translation;
- resize geometry.

## V1 — Offline renderer

Without CEF, load a deterministic image fixture and render it to terminal output.

Acceptance:

- correct dimensions;
- no wrap/scroll corruption;
- `▀` half-block mapping visibly correct;
- true-colour mode works;
- 256 and 1024 quantised modes visibly differ as expected;
- resize/re-render works;
- clean terminal teardown.

This stage isolates almost all terminal and image conversion problems from Chromium.

## V2 — Static CEF frame

Launch a local bundled fixture in CEF OSR and display received `OnPaint` pixels.

Acceptance:

- source frame dimensions/channels are correct;
- no obvious vertical inversion/channel swap/alpha corruption;
- CEF resize propagates correctly;
- fixture matches a normal Chromium screenshot closely before terminal quantisation.

## V3 — Live browser output

Run an animated/local fixture.

Acceptance:

- repeated paint callbacks become visible terminal frames;
- frame pacing does not create an unbounded queue;
- process remains responsive;
- full-repaint reference mode works;
- instrumentation reports source/render/emission rates.

Issue #9 adds two automated V3 checks in addition to the interactive Windows Terminal run. The static fixture runs the real CEF→popup-compositor→half-block→full-frame-VT pipeline headlessly for a bounded interval and must settle into idle rather than emitting continuously. The animated fixture runs CEF faster than the terminal cap and must record coalesced presentation generations while output remains within the configured frame-rate tolerance. Both runs emit `asciiomium-live-v1` JSON plus an SVG representation of the final logical terminal frame and the exact final full-frame VT payload.

The automated/headless path intentionally skips only the final console `WriteFile`; it does not replace CEF capture, sampling, quantisation, glyph generation or VT serialization. Actual Windows Terminal visual behaviour remains an interactive V3 observation because a hosted CI runner cannot provide that UI.

### V3 geometry/resize checks

Issue #10 adds a pure geometry test suite covering stretch/contain transforms,
cell aspect, letterbox rejection, corners, inverse mapping, odd sizes, reserved
rows, unsafe dimensions, and geometry generation transitions. The test log
prints deterministic resize transitions including terminal size, browser size,
logical sample lattice, generation and whether a browser resize is implied.

For an interactive Windows Terminal observation, run:

```powershell
.\build\bin\RELEASE\asciiomium_live.exe --geometry-debug
```

Resize repeatedly between narrow/wide and short/tall windows. Expected:
recognisable content is repainted without stale rows/columns; geometry
generations advance only on actual dimension changes; the fixed browser
viewport remains unchanged; `cef_resize=false` is logged for terminal-only
resizes; Ctrl+Alt+Q restores the terminal.

## V4 — Input

Use the dedicated `input-focus` fixture.

Issue #11 automates the keyboard-only portion of V4. `input_parser_tests`
exercise ASCII, 2/3/4-byte UTF-8, arbitrary read fragmentation, editing and
navigation keys, Ctrl/Alt modifiers, function keys, malformed input, and the
bounded pending-state contract. `cef_key_translator_tests` verify the split
between text/CHAR delivery and logical RAWKEYDOWN/KEYUP delivery, including
the no-duplicate-character rule for shortcuts.

CI also runs the real live CEF path headlessly with:

```text
--fixture input-focus --freeze --input-script asciiomium --no-terminal
```

The resulting `asciiomium-live-v1` report must show that the script was
decoded without unsupported events, CEF key events were sent, the fixture's
real DOM `input` listener acknowledged the event through a fixed URL-fragment
marker, and a later presentation generation was observed after injection. The
marker contains no typed value.

Actual Windows Terminal keyboard transport still requires a local observation
because a hosted runner cannot prove which VT bytes the GUI terminal emits.
Run:

```powershell
.\build\bin\RELEASE\asciiomium_live.exe --fixture input-focus --input-debug
```

Verify ordinary ASCII and representative non-ASCII UTF-8 text; Backspace,
Enter, Tab/Shift+Tab, arrows, Home/End, PageUp/PageDown, Delete/Insert where
available; ordinary Escape; Ctrl+A/C/V where CEF/clipboard policy permits; and
Ctrl+Alt+Q teardown. Confirm that the parent prompt and input mode are normal
after exit.

Pointer-specific V4 acceptance remains issue #12:

- pointer movement diagnostics decode correctly;
- click targets work at centre and near each viewport corner;
- wheel scroll works;
- resize does not desynchronise coordinates.

## V5 — Differential emitter

Compare logical output to full-repaint mode.

Acceptance:

- identical `TerminalFrame` input produces the same visible result;
- unchanged static frame emits approximately zero cell payload after stabilisation;
- small animated regions emit substantially fewer bytes than full repaint;
- no stale characters remain after content shrinks or colours/styles change.

## V6 — Real websites

After local fixtures pass, test representative external pages:

- mostly text/document page;
- image-heavy page;
- complex responsive application;
- page with forms and nested scrolling.

Do not make a single specific website a release dependency.

Record limitations rather than adding site-specific hacks.

## V7 — Soak and teardown

At least one 30-minute session with periodic scrolling/navigation.

Acceptance:

- no obvious progressive memory leak in ASCIIomium-owned buffers;
- no frame-queue growth;
- no terminal mode corruption;
- normal quit restores prompt/cursor/input;
- repeated 10x launch/quit cycle leaves terminal usable.

## Visual regression evidence

When changing the renderer:

- run deterministic fixtures;
- preserve configuration metadata;
- capture a Windows Terminal screenshot for at least flat UI + colour ramp + raster image fixtures;
- include benchmark before/after for performance-motivated changes.

## Failure triage order

When the display looks wrong, diagnose in this order:

1. CEF source frame integrity;
2. viewport/resize geometry;
3. sampler;
4. quantiser;
5. glyph encoder;
6. terminal-frame model;
7. VT serialiser/diff emitter;
8. Windows Terminal/font behaviour.

When input is wrong:

1. raw terminal bytes;
2. decoded input event;
3. terminal coordinates/modifiers;
4. coordinate transform;
5. CEF event forwarding;
6. page/browser behaviour.

This ordering should be reflected in diagnostic logging so failures remain localisable.
