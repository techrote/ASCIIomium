# Live CEF-to-Terminal Pipeline

Issue #9 connects the previously independent CEF source-frame, half-block renderer and full-frame VT emitter into ASCIIomium's first continuously updating terminal-browser picture.

## Data flow

```text
CEF PET_VIEW / PET_POPUP callbacks
             |
             v
SourceFrameStore
(one newest view + one newest popup)
             |
             | lightweight generation poll
             v
FrameScheduler ---- max FPS / coalescing ----+
             |                                |
             | coherent snapshot when due    |
             v                                |
ComposeViewAndPopup                          |
             |                                |
             v                                |
BGRA ImageView                                |
             |                                |
             v                                |
RenderHalfBlock + selected colour quantizer  |
             |                                |
             v                                |
TerminalFrame                                 |
             |                                |
             v                                |
SerializeFullFrame                            |
             |                                |
             v                                |
TerminalSession::Write -----------------------+
```

The browser, renderer and terminal backend remain separate subsystems. The live loop coordinates them; it does not move browser logic into the renderer or terminal concerns into CEF callbacks.

## Newest-frame scheduling

`OnPaint` remains deliberately cheap: CEF callback-owned bytes are copied into the bounded `SourceFrameStore` and the callback returns. No sampling, quantisation, VT serialization or sleeping occurs in the callback.

`SourceFrameStore` exposes a lightweight `SourceFrameState` containing generations and paint counts without copying image memory. The main loop polls that metadata and snapshots full pixels only when `FrameScheduler` says a render is due.

The scheduler owns only counters and timestamps. It never owns browser-frame objects and never queues generations. If source generation 41 is rendered and Chromium advances through 42, 43 and 44 before the next output slot, the loop snapshots 44 directly and records 42/43 as coalesced. This implements decision D-008 without accumulating latency.

The maximum output frame rate is configurable with `--fps`. Useful early values are 10, 15, 20 and 30. The default is 20 FPS. The next output slot is measured from completion of the previous frame, so a slow frame does not cause a burst of catch-up repaints.

A source that has not changed does not schedule another frame. The loop continues servicing CEF while sleeping briefly between polls. A terminal resize is an explicit forced refresh because the same browser source must be resampled to new terminal geometry.

## Popup composition

CEF supplies browser widgets such as select menus as separate `PET_POPUP` images. Issue #8 retained them independently; issue #9 composes them before terminal sampling.

`ComposeViewAndPopup`:

- begins from the newest complete `PET_VIEW` image;
- repositions popup bounds into the visible view when possible;
- clips oversized popup content to the view;
- treats CEF popup texture channels as premultiplied-alpha BGRA;
- applies source-over composition equivalent to `ONE, ONE_MINUS_SRC_ALPHA`;
- returns an ordinary BGRA image to the browser-agnostic renderer.

Popup visibility/bounds/pixel changes increment the source presentation generation, so opening, moving, repainting or hiding a popup is presentation work even when the underlying view generation is unchanged.

## Rendering and output

The live baseline remains the established reference path:

```text
glyph:      U+2580 upper half block
colour:     deterministic 1024-colour 3/4/3 RGB quantisation
filter:     box average
emitter:    full-frame VT serializer
```

The differential terminal emitter is intentionally not part of issue #9. Every scheduled logical terminal frame is serialized in full so live integration correctness is independent of terminal-diff optimisation.

CEF source dimensions and terminal geometry are decoupled. In terminal mode the current Windows Terminal viewport determines output columns/rows. In `--no-terminal` diagnostic mode `--columns` and `--rows` provide deterministic geometry for CI.

## Running

After a Release build, the default animated fixture can be shown directly in Windows Terminal:

```powershell
.\build\bin\RELEASE\asciiomium_live.exe
```

Equivalent explicit settings are:

```powershell
.\build\bin\RELEASE\asciiomium_live.exe `
  --fixture motion-scroll `
  --width 960 --height 540 `
  --fps 20 --colors 1024 --filter box
```

Press `Ctrl+C` to leave the live loop. `TerminalSession` restores SGR state, cursor visibility, alternate-screen state, code pages and console modes before CEF shutdown completes.

An explicit URL can be used instead of a bundled fixture:

```powershell
.\build\bin\RELEASE\asciiomium_live.exe `
  --url https://example.com/ `
  --fps 15 --colors true
```

Issue #9 does not yet implement browser keyboard/mouse forwarding; that is the next input milestone.

## Diagnostic/headless mode

CI cannot provide a human Windows Terminal window, so the same pipeline can execute without console ownership:

```powershell
.\build\bin\RELEASE\asciiomium_live.exe `
  --fixture motion-scroll `
  --width 640 --height 360 `
  --columns 120 --rows 40 `
  --fps 20 --cef-fps 60 --duration-ms 2000 `
  --no-terminal `
  --report .\build\live.json `
  --evidence-svg .\build\live.svg `
  --vt-output .\build\live.vt
```

This is not a second renderer. It still performs real CEF OSR capture, popup composition, half-block conversion, colour quantisation and the exact full-frame VT serialization. It merely skips the final `WriteFile` call. The SVG represents the colours of the final logical half-block frame and the `.vt` file contains the exact final terminal payload.

## Instrumentation

The `asciiomium-live-v1` JSON report records:

- CEF/Chromium versions and source URL;
- source view/popup paint and generation counts;
- configured source and terminal frame-rate caps;
- rendered and emitted terminal frames;
- coalesced source presentation generations;
- observed source and emitted frame rates;
- average and p95 conversion time;
- average and p95 VT serialization time;
- terminal-write timing where applicable;
- idle-loop iterations;
- popup-composited frame count;
- final VT payload size;
- terminal restoration and CEF close status.

Timing percentile storage is a fixed-size ring. Long sessions therefore do not accumulate an unbounded measurement vector.

## Shutdown order

Normal termination follows this order:

1. leave the live render loop;
2. request CEF browser close;
3. continue pumping until `OnBeforeClose` or the close deadline;
4. restore terminal state;
5. release the CEF client reference;
6. call `CefShutdown`;
7. write/report diagnostic evidence.

The console control handler requests normal loop termination for Ctrl+C/Break. Close/logoff/shutdown retain the `TerminalSession` best-effort emergency restoration path established by issue #3.

## Sandbox status

The live executable inherits issue #8's explicit temporary direct-executable exception:

```text
USE_SANDBOX=OFF
CefSettings.no_sandbox=true
```

This remains a development-bootstrap constraint of the current direct-`libcef` architecture, not an intended distribution security posture. It must be revisited before packaging a general browser build.
