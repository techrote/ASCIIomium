# CEF Off-Screen Rendering

This document records ASCIIomium's implemented Chromium Embedded Framework (CEF) CPU off-screen-rendering path introduced by issue #8.

## Scope

The issue #8 path is a **source-frame correctness layer**, not yet the complete terminal browser loop. Its responsibilities are:

1. initialise the pinned CEF build in windowless/off-screen-rendering mode;
2. load deterministic local fixtures through a real Chromium renderer;
3. receive CPU `CefRenderHandler::OnPaint` callbacks;
4. preserve the newest browser frame in a bounded host-owned buffer;
5. retain dirty-rectangle and generation metadata;
6. model popup paint separately rather than silently discarding it;
7. support browser-view resize;
8. provide BMP/JSON diagnostics; and
9. shut the browser and CEF runtime down cleanly.

Terminal conversion, frame pacing, differential VT output and browser input are intentionally later integration layers.

## Pinned runtime

The CEF version remains the build pinned by issue #2:

```text
CEF:      151.3.17+gf059e67+chromium-151.0.7922.138
Chromium: 151.0.7922.138
Platform: windows64 x64
```

The browser target uses the official CEF CMake module and `libcef_dll_wrapper` from that exact binary distribution; it does not introduce an independently versioned wrapper.

## OSR configuration

ASCIIomium enables:

```text
CefSettings.windowless_rendering_enabled = true
CefWindowInfo::SetAsWindowless(...)
CefRenderHandler::GetViewRect(...)
CefRenderHandler::OnPaint(...)
```

The source viewport is explicit and initially uses a deterministic device scale factor of **1.0**, so requested view dimensions correspond directly to received paint-buffer dimensions. Later DPI-aware browser integration may make this configurable, but source pixel geometry must always be explicit in diagnostics.

The diagnostic uses the single-threaded external pump style:

```text
CefExecuteProcess
CefInitialize
CreateBrowser
loop: CefDoMessageLoopWork
CloseBrowser
pump until OnBeforeClose
CefShutdown
```

This keeps initial browser lifecycle and resize diagnostics straightforward and avoids introducing a second application event-loop model before the live browser issue needs one.

## CPU pixel contract

For the pinned CEF CPU `OnPaint` path, ASCIIomium treats the callback buffer as:

```text
format:       BGRA8
size:         width * height * 4 bytes
row layout:   tightly packed
origin:       upper-left
ownership:    callback-owned; copy before callback returns
```

`SourceFrameStore` therefore copies the bytes exactly as delivered. It does **not** swap red/blue channels, vertically flip the image, premultiply/unpremultiply, or convert to the renderer's RGBA representation in the callback.

Keeping the source representation intact makes channel/orientation failures independently diagnosable. The later browser→renderer integration boundary is responsible for exposing the buffer as the renderer's existing `PixelFormat::BGRA8` view.

The diagnostic reports the observed minimum and maximum alpha byte. Its baseline browser background is explicitly opaque, so the deterministic fixtures are expected to produce an opaque final view (`255..255`). That assertion validates the chosen baseline; it is not a claim that every future webpage or popup must be opaque.

It also reports the minimum and maximum observed RGB-channel byte across the view. Fixture CI uses the resulting span as a coarse guard against accidentally accepting a uniform pre-navigation/background paint. This is not an image-quality metric; it is a source-frame sanity check.

## Post-load paint barrier

CEF can issue an OSR paint for the empty browser/background before the requested page finishes loading. `OnLoadEnd` and `OnPaint` are separate asynchronous signals, so a capture must not treat an older pre-load paint as valid merely because load completion happened later.

The diagnostic therefore uses this ordering:

```text
wait for main-frame OnLoadEnd
record current PET_VIEW generation
request WasResized (same size or requested new size)
wait for PET_VIEW generation > recorded generation
only then accept capture evidence
```

This explicit post-load paint barrier was added after visual inspection caught a stale uniform background frame that otherwise satisfied the original load/resize counters. CI now requires both a fresh post-load generation and meaningful RGB variation for the deterministic visual fixtures.

## Newest-frame ownership

CEF may paint faster than terminal conversion or output. The browser layer therefore implements decision D-008 directly: **newest-frame semantics, not queued-frame completeness**.

`SourceFrameStore` owns at most:

- one current view pixel vector;
- one current popup pixel vector;
- dirty rectangles and small metadata for each.

A new view paint overwrites/reuses the current view storage. There is no historical-frame queue. Tests perform 1,000 repeated same-size updates and require vector capacity not to grow after warm-up.

Each view update increments:

```text
generation
paint_count
```

The later consumer can compare generations and simply skip superseded frames.

## Dirty rectangles

CEF dirty rectangles are retained as metadata with the copied newest frame. Issue #8 still copies the entire callback buffer deliberately: CPU `OnPaint` supplies a complete image buffer, and full-buffer copy is the simplest correctness path.

Later measured optimisation may use dirty rectangles to restrict conversion or copying. It must preserve the full-frame reference behaviour.

## Popup policy

CEF exposes popup widgets through separate `PET_POPUP` paints. Issue #8 does **not** silently drop them and does not prematurely fold them into the base frame.

The source store retains:

- popup visibility;
- popup bounds in view coordinates;
- newest popup BGRA pixels;
- popup dirty rectangles;
- popup generation/paint count.

The later live integration can composite the popup onto the logical browser source at the correct bounds before terminal sampling. Retaining the view and popup independently keeps this issue's source-frame evidence unambiguous.

## Resize

`OsrClient::Resize` changes the requested view dimensions and notifies the browser host with:

```text
NotifyScreenInfoChanged()
WasResized()
```

Acceptance requires a subsequent `PET_VIEW` generation with the **new** dimensions, not merely mutation of local width/height state. CI exercises 640×360 → 800×450 and rejects a stale pre-resize or pre-load frame.

## Diagnostic capture

After building:

```powershell
.\build\bin\DEBUG\asciiomium_cef_capture.exe `
  --fixture colour-ramps --freeze `
  --width 640 --height 360 `
  --resize-width 800 --resize-height 450 `
  --min-paints 2 `
  --output .\build\cef-colour.bmp `
  --report .\build\cef-colour.json
```

The BMP is a top-down 32-bit image written directly from the captured BGRA bytes. It exists only as visual/debug evidence; BMP is not part of the runtime rendering architecture.

The JSON report records:

- CEF compile/runtime version;
- Chromium runtime version;
- loaded URL;
- pixel-format contract;
- post-load baseline generation and fresh-paint evidence;
- view dimensions, generation, paint count and dirty-rect count;
- RGB byte range/span and alpha range;
- popup state and generations;
- storage capacities;
- resize evidence;
- load status/error;
- shutdown result; and
- sandbox status.

The animated fixture is also used to require multiple paint generations without any frame queue.

## Sandbox exception

Issue #2 intentionally bootstrapped ASCIIomium as a conventional executable linked directly to `libcef`. With the pinned CEF 151 Windows distribution, the official sandbox-enabled CMake path uses CEF's newer bootstrap executable/client-DLL architecture.

To avoid turning issue #8 into a packaging/bootstrap migration, the OSR diagnostic currently uses:

```text
USE_SANDBOX=OFF
CefSettings.no_sandbox=true
```

This exception is **explicit and temporary**. It is acceptable for the local milestone diagnostic because it is documented and bounded; it is not the intended distribution security posture. Before packaging a general browser build, either migrate to the supported sandbox/bootstrap architecture or record a separately reviewed reason not to do so.

Do not generalise this exception into disabling Chromium security features for ordinary browser operation.

## Verification order

When an OSR source frame looks wrong, diagnose in this order:

1. CEF load/lifecycle status;
2. post-load paint generation freshness;
3. callback width/height;
4. BGRA channel interpretation;
5. upper-left orientation;
6. alpha/background assumption and RGB variation;
7. resize generation;
8. popup state;
9. only then the ASCIIomium sampler/quantiser/glyph/VT layers.

This preserves V2 in `VERIFY.md` as an independent source-frame checkpoint before Chromium output is connected to Windows Terminal.
