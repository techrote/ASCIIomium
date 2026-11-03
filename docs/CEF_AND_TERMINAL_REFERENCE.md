# CEF and Windows Terminal Reference

This document is a compact implementation reference, not a substitute for upstream documentation. Re-check upstream APIs when pinning a concrete CEF release.

## Pinned P0 candidate API notes, 3 November 2026

The current P0 work retains CEF
`151.3.17+gf059e67+chromium-151.0.7922.138`, Windows x64. The following notes
describe the unmerged [runtime PR #40](https://github.com/techrote/ASCIIomium/pull/40)
and [packaging PR #43](https://github.com/techrote/ASCIIomium/pull/43), not new
accepted-main functionality. Exact qualification states and source revisions
are recorded in [the P0 checkpoint](P0_CHECKPOINT_2026-11-03.md).

- The pinned `WindowOpenDisposition` spelling is `CEF_WOD_CURRENT_TAB`.
  Browser callbacks and overrides are compiled against the pinned headers;
  examples from older generated documentation are conceptual references only.
- Persistent mode selects an absolute `root_cache_path`/`cache_path` outside
  the executable and checkout and explicitly requests session-cookie
  persistence. Private mode leaves `cache_path` empty but still owns a unique
  root. An empty browser cache path is not evidence that Chromium creates no
  installation/profile files. The private supervisor retains ownership until
  its ordinary OSR worker and descendants exit, then verifies root deletion.
- `CefSettings.log_severity = LOGSEVERITY_DISABLE` alone does not disable the
  pinned Chromium file destination. The app supplies `disable-logging` in
  `OnBeforeCommandLineProcessing` at browser and child entry points, and the
  qualification checks for diagnostic log files independently of private-root
  deletion. See the pinned [CEF logging patch](https://github.com/chromiumembedded/cef/blob/f059e67/patch/patches/chrome_common_logging.patch)
  and [Chromium logging initialization](https://github.com/chromium/chromium/blob/151.0.7922.138/chrome/common/logging_chrome.cc).
- Media testing uses fake devices only in the qualification executable. It
  does not pass `enable-media-stream` or a fake permission UI switch: permission
  denial must reach the host and be observed by the page. Production browsing
  uses neither test device nor permission-grant switches.
- Download policy uses host callbacks and application-chosen unique paths.
  A remote filename is not a destination. Qualification checks exact bytes and
  the bounded fallback directory, rather than accepting a callback counter as
  proof that nothing was written.

The direct executable bootstrap remains unsandboxed. These explicit browser
policies and private-root cleanup do not establish a security sandbox or
forensic erasure. No Chrome headless-mode workaround is used to change process
lifetime; the CPU OSR path and normal Chromium lifecycle remain the basis.

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

Mouse coordinates are relative to the upper-left of the browser view. Issue
#12 maps zero-based terminal cells through the authoritative
`ViewportTransform` before constructing `CefMouseEvent`. Move events use
`SendMouseMoveEvent`; press/release use `SendMouseClickEvent` with click
count 1; wheel reports use `SendMouseWheelEvent`. The initial wheel policy maps
one terminal notch to ±120 CEF delta units by default and is configurable.

For the pinned CEF keyboard path, ASCIIomium uses `CefKeyEvent` with `KEYEVENT_RAWKEYDOWN`, `KEYEVENT_KEYUP` and `KEYEVENT_CHAR`. `windows_key_code` carries Win32 `VK_*` values for logical/navigation keys; `native_key_code` is a best-effort synthetic Windows scan/lParam representation because VT input does not preserve a real native key record. CEF modifiers use `EVENTFLAG_SHIFT_DOWN`, `EVENTFLAG_CONTROL_DOWN` and `EVENTFLAG_ALT_DOWN`.

Plain UTF-8 text is forwarded as CHAR events. ASCIIomium deliberately keeps text/codepoint delivery separate from shortcut/navigation key translation so future IME/composition support can be added without replacing the parser.

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

Issue #12's Windows Terminal baseline uses DECSET 1003 (all-motion) together
with 1006 (SGR extended coordinates). The parser accepts the SGR
`CSI < Cb ; Cx ; Cy M/m` form, converts its 1-based cell coordinates to the
project's zero-based canonical convention, and decodes modifier/button/motion/
wheel bits before any CEF-specific translation.

All-motion mode is intentional because web hover is useful without a pressed
button. Terminal selection coexistence is environment-specific: Windows
Terminal's Shift bypass must be checked manually in the real GUI rather than
inferred from hosted CI.

## Font/glyph assumptions

Do not assume every font contains every candidate glyph. The baseline half-block glyph is broadly available and should be the mandatory encoder. Braille and less common geometric glyphs must be feature-tested or documented as requiring a suitable font.

Microsoft DirectWrite supports Unicode shaping broadly, including Braille when a suitable font is present:

- https://learn.microsoft.com/windows/win32/directwrite/introducing-directwrite

## Compatibility rule

Repository documentation records concepts and tested behaviour; the pinned dependency versions in the source tree are authoritative for exact API signatures. When an upstream CEF or Terminal behaviour differs from these notes, update this document alongside the implementation.

## Windows Terminal keyboard stream

Accepted main and the #13/#17 candidates retain issue #11's input transport:
`ENABLE_VIRTUAL_TERMINAL_INPUT`, `CP_UTF8`, and main-thread `ReadFile` after an
input-handle readiness check. Line/echo/processed input are disabled in browser
mode. A signalled handle does not guarantee a character: a retained non-character
record can be consumed by a high-level read that then waits. See Microsoft's
[high-level console input documentation](https://learn.microsoft.com/en-us/windows/console/high-level-console-input-and-output-functions).

The unmerged #16 candidate replaces that readiness/read sequence with one native
`ReadFile` worker and a fixed 4 KiB mailbox. The main loop drains published bytes;
normal teardown cancels and proves reader completion before restoring input
mode/code page. No low-level event reads or flush are mixed into the UTF-8 stream.
The pinned [Terminal input-buffer implementation](https://github.com/microsoft/terminal/blob/385c0c75f7f42a33c43f2508ecc8b7eb5d4f6a0d/src/host/inputBuffer.cpp)
explains why switching read modes can discard cached conversion bytes. Exact-head
native Windows results and physical terminal acceptance are recorded separately
in the [P0 checkpoint](P0_CHECKPOINT_2026-11-03.md); neither is inferred from the
source repair or the portable Win32 shim tests.

Disabling `ENABLE_PROCESSED_INPUT` is intentional in browser mode: Ctrl+C must be available to web content. Browser-mode application quit is Ctrl+Alt+Q. Non-browser diagnostics retain processed-input Ctrl+C handling.
