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

## V4 — Input

Use the dedicated input fixture.

Acceptance:

- pointer movement diagnostics decode correctly;
- click targets work at centre and near each viewport corner;
- wheel scroll works;
- text entry works;
- editing/navigation keys work at least for the documented milestone set;
- focus/caret state remains usable;
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