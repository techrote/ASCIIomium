# CEF and Windows Terminal Reference

This document is a compact implementation reference, not a substitute for upstream documentation. Re-check upstream APIs when pinning a concrete CEF release.

## CEF off-screen rendering

CEF explicitly supports windowless/off-screen rendering. In this mode no native browser window is created; the host supplies a `CefRenderHandler`, receives painted content, and sends input back to the browser.

Primary references:

- CEF General Usage / Off-Screen Rendering: https://chromiumembedded.github.io/cef/general_usage.html#off-screen-rendering
- Current generated CEF API documentation index: https://cef-builds.spotifycdn.com/docs/stable.html
- `CefWindowInfo::SetAsWindowless`: consult the generated API for the pinned release.
- `CefRenderHandler`: consult the generated API for `GetViewRect`, `OnPaint`, popup painting, and accelerated paint.

Important implementation facts:

1. `CefSettings.windowless_rendering_enabled` must be enabled.
2. `CefWindowInfo::SetAsWindowless(...)` selects OSR for a browser.
3. `GetViewRect` supplies the browser view rectangle.
4. CPU OSR delivers pixel buffers through `OnPaint` together with dirty rectangles.
5. Browser resize is signalled through `CefBrowserHost::WasResized()`.
6. Windowless input is delivered via `CefBrowserHost::SendXXX` methods.

## CEF accelerated painting

CEF also exposes accelerated off-screen paint on supported platforms/releases. On Windows, `OnAcceleratedPaint` can provide a shared texture handle that can be opened through D3D11. Recent CEF API documentation notes that the underlying resource comes from a pool and must not be retained beyond the callback; the host should open/copy it into its own resource if needed.

Reference examples:

- `CefRenderHandler::OnAcceleratedPaint`: https://cef-builds.spotifycdn.com/docs/132.0/classCefRenderHandler.html
- Newer generated API versions should be preferred when implementation begins.

ASCIIomium deliberately does **not** start here. CPU paint is the correctness path. Accelerated paint becomes useful only after profiling shows framebuffer transfer/conversion to be a limiting factor.

## CEF browser input

`CefBrowserHost` exposes windowless-browser input methods including:

- `SendKeyEvent`;
- `SendMouseClickEvent`;
- `SendMouseMoveEvent`;
- `SendMouseWheelEvent`.

Reference example:

- https://cef-builds.spotifycdn.com/docs/132.0/classCefBrowserHost.html

Mouse coordinates are relative to the upper-left of the browser view.

## CEF distribution/build strategy

Use upstream CEF binary distributions or the standard `cef-project`/CMake integration rather than building Chromium from source for milestone 1.

Useful starting points:

- CEF project: https://github.com/chromiumembedded/cef
- CEF project template: https://github.com/chromiumembedded/cef-project
- Automated CEF builds: https://cef-builds.spotifycdn.com/index.html

Pin an exact CEF build in build metadata. Do not depend on "latest" at runtime or in reproducibility-sensitive CI.

## Windows Terminal capabilities

Microsoft documents Windows Terminal as supporting Unicode/UTF-8 and GPU-accelerated text rendering:

- Overview: https://learn.microsoft.com/windows/terminal/

That makes block, quadrant, Braille, box-drawing, and ordinary Unicode glyphs viable subject to font coverage.

## VT output and colour

Windows Console/Terminal supports VT control sequences. Relevant output features include cursor positioning, screen modes, SGR styling, and extended colours.

Primary reference:

- https://learn.microsoft.com/windows/console/console-virtual-terminal-sequences

Colour forms used by ASCIIomium:

```text
ESC[38;5;<index>m             indexed foreground (commonly xterm 256)
ESC[48;5;<index>m             indexed background
ESC[38;2;<r>;<g>;<b>m        RGB foreground
ESC[48;2;<r>;<g>;<b>m        RGB background
```

Therefore a deliberately quantised 512/1024-colour mode should quantise RGB in ASCIIomium and then emit those chosen RGB values with `38;2`/`48;2`. It should not invent non-standard indexed palette numbers.

## Alternate screen and terminal hygiene

Interactive full-screen TUIs conventionally use the alternate screen buffer, hide the cursor while drawing, and restore all state during teardown.

ASCIIomium must centralise terminal mode ownership so cleanup occurs on:

- normal exit;
- browser shutdown;
- handled Ctrl+C / termination paths where possible;
- exceptions after terminal mode acquisition.

A development `--no-alt-screen` mode can aid debugging.

## Mouse input

Windows Terminal supports mouse input for applications using VT input; Microsoft explicitly calls out compatibility with applications such as tmux and Midnight Commander.

Reference:

- https://learn.microsoft.com/windows/terminal/selection

The Terminal source contains an SGR mouse-sequence generator and mouse tracking state, which is useful as a behavioural reference:

- https://github.com/microsoft/terminal/blob/main/src/terminal/input/terminalInput.hpp

Historical Windows Terminal/ConPTY issues show that native Windows applications and VT input can differ in how mouse events arrive. Do not assume Win32 `MOUSE_EVENT_RECORD` semantics inside Windows Terminal. Prefer a VT mouse parser for the primary target and retain raw-event diagnostics.

Relevant historical discussion:

- https://github.com/microsoft/terminal/issues/15296

## Font/glyph assumptions

Do not assume every font contains every candidate glyph. The baseline half-block glyph is broadly available and should be the mandatory encoder. Braille and less common geometric glyphs must be feature-tested or documented as requiring a suitable font.

Microsoft DirectWrite supports Unicode shaping broadly, including Braille when a suitable font is present:

- https://learn.microsoft.com/windows/win32/directwrite/introducing-directwrite

## Compatibility rule

Repository documentation records concepts and tested behaviour; the pinned dependency versions in the source tree are authoritative for exact API signatures. When an upstream CEF or Terminal behaviour differs from these notes, update this document alongside the implementation.