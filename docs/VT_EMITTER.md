# Reference Full-Frame VT Emitter

Issue #6 establishes ASCIIomium's visual correctness oracle for terminal output. The emitter consumes a completed logical `TerminalFrame`; it does not sample pixels, quantise colours, diff frames, read terminal input, or interact with CEF.

## Data flow

```text
Image / CEF framebuffer
        |
        v
half-block renderer + colour quantiser
        |
        v
TerminalFrame
        |
        v
SerializeFullFrame()
        |
        v
one batched VT byte buffer
        |
        v
TerminalSession::Write()
```

The separation is deliberate. Later differential output must reproduce the same visible logical frame as this straightforward full repaint path.

## Row and cursor policy

The terminal is treated as an explicitly addressed framebuffer, not as a line-oriented text stream.

- row 1 starts with `CUP home` (`ESC[H`);
- every later row starts with explicit `CUP(row, 1)`;
- no `LF`, `CR`, implicit line wrapping, or terminal-side horizontal scrolling is used;
- after the final cell, the emitter immediately emits `CUP home` before any later printable byte;
- SGR state is reset after that final cursor motion.

This is the right-margin/autowrap strategy. ASCIIomium intentionally does **not** toggle DECAWM (`?7h`/`?7l`) because the previous DEC private-mode state is not reliably queryable through the Windows Console API. Instead, no subsequent graphic character is allowed to consume a pending wrap after the final column. The real-console `vt_console_smoke` test fills the complete detected viewport, writes the bottom-right cell, and verifies that the top row did not scroll away and that the cursor returned to home.

## Colour serialisation

`TerminalColor` carries both canonical RGB and its output representation.

Indexed 16/256 colours use standardized indexed SGR:

```text
ESC[38;5;<index>m   foreground
ESC[48;5;<index>m   background
```

True-colour and deliberately quantised 512/1024 RGB modes use 24-bit SGR:

```text
ESC[38;2;<r>;<g>;<b>m
ESC[48;2;<r>;<g>;<b>m
```

The emitter does not reinterpret colour modes. It serialises the representation already selected by the quantiser.

## SGR state compression

Within a full repaint, the emitter remembers the currently selected foreground and background. A new SGR sequence is emitted only when that logical colour changes. This is intentionally modest optimisation: there is still no frame-to-frame diffing in this issue.

For a uniform 160x50 half-block frame (8,000 cells) using one RGB foreground and one RGB background, the reference byte count is **24,377 bytes**: 24,000 UTF-8 glyph bytes plus cursor positioning, two colour selections, final cursor-home and SGR reset. The test records this count so later optimisations have a stable comparison point.

## UTF-8

`EncodeUtf8()` accepts Unicode scalar values through U+10FFFF and rejects surrogate code points. The canonical U+2580 half block serialises as:

```text
E2 96 80
```

## Annotated debug output

The existing structural frame-dump tool can now show the exact VT payload without executing escape sequences:

```powershell
.\build\bin\DEBUG\asciiomium_frame_dump.exe `
  --input .\fixtures\render\quad_2x4.ppm `
  --columns 2 --rows 2 --filter nearest --colors 256 `
  --vt-annotated
```

`ESC` is displayed as `<ESC>` and non-ASCII bytes as `\xHH`, making the output suitable for CI logs and golden inspection.

## Windows Terminal visual preview

`asciiomium_vt_preview` is a deliberately small manual visual harness. It owns an alternate screen through `TerminalSession`, renders to the detected terminal geometry, and rerenders when geometry changes.

Synthetic colour ramp:

```powershell
.\build\bin\DEBUG\asciiomium_vt_preview.exe --ramp --colors 256
.\build\bin\DEBUG\asciiomium_vt_preview.exe --ramp --colors 1024
.\build\bin\DEBUG\asciiomium_vt_preview.exe --ramp --colors true
```

Bundled image fixture:

```powershell
.\build\bin\DEBUG\asciiomium_vt_preview.exe `
  --input .\fixtures\render\quad_2x4.ppm `
  --colors 1024 --filter nearest
```

Resize the Windows Terminal window while a preview is active to exercise hard geometry changes. Press `Ctrl+C` to leave the alternate screen and restore the shell. `--duration-ms N` provides an automatic exit for short checks.

## Scope boundary

This reference emitter deliberately does not implement:

- frame-to-frame cell diffing;
- dirty-rectangle projection;
- cursor-motion optimisation across unchanged regions;
- browser frame pacing;
- CEF paint callbacks;
- mouse/keyboard input.

Those remain later issues. The full repaint path should remain available after those optimisations land so regressions can always be compared against a simple deterministic oracle.
