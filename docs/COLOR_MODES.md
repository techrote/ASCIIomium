# Colour modes

ASCIIomium keeps sampling/glyph selection separate from colour representation. The renderer first produces an RGB sample, then a `ColorQuantizer` maps that sample into a `TerminalColor`.

`TerminalColor` always carries canonical RGB for tests and non-terminal tooling. It also records whether the future VT emitter should serialize the value as an indexed terminal colour or as 24-bit RGB.

## Modes

| CLI | Logical representation | Definition | Intended VT form |
| --- | --- | --- | --- |
| `true` | RGB | identity RGB888 | `38;2` / `48;2` |
| `16` | indexed | nearest canonical xterm base colour, index 0..15 | indexed terminal colour |
| `256` | indexed | nearest xterm 256 palette entry, index 0..255 | `38;5` / `48;5` |
| `512` | RGB | fixed 3/3/3-bit cube: 8 x 8 x 8 | `38;2` / `48;2` |
| `1024` | RGB | fixed 3/4/3-bit cube: 8 x 16 x 8 | `38;2` / `48;2` |

The 512/1024 modes deliberately do **not** invent terminal palette indices. This implements decisions D-005 and D-006: quantised RGB is an aesthetic mode, while unrestricted true colour remains the fidelity control.

## Canonical xterm palette

The indexed modes use a deterministic xterm-compatible reference palette.

Indices 0..15 are:

```text
0  #000000    8  #808080
1  #800000    9  #ff0000
2  #008000   10  #00ff00
3  #808000   11  #ffff00
4  #000080   12  #0000ff
5  #800080   13  #ff00ff
6  #008080   14  #00ffff
7  #c0c0c0   15  #ffffff
```

Indices 16..231 form the standard 6x6x6 cube using channel levels:

```text
0, 95, 135, 175, 215, 255
```

Index formula:

```text
16 + 36*r + 6*g + b
```

where each component coordinate is 0..5.

Indices 232..255 are grayscale levels:

```text
8, 18, 28, ... 238
```

The quantizer minimizes squared Euclidean distance in encoded RGB space. Ties select the numerically lower index. This makes colours that are duplicated between the base palette and cube deterministic; for example exact black maps to index 0 and exact bright red maps to index 9.

A user's terminal theme may redefine the visual RGB values associated with indices 0..15. The canonical RGB stored in `TerminalColor` is therefore a reference/test value, while the eventual indexed VT output intentionally permits the terminal's configured base palette to determine display colour.

## Fixed RGB cubes

### 512 colours

```text
R: 3 bits -> 8 levels
G: 3 bits -> 8 levels
B: 3 bits -> 8 levels
8 * 8 * 8 = 512
```

### 1024 colours

```text
R: 3 bits -> 8 levels
G: 4 bits -> 16 levels
B: 3 bits -> 8 levels
8 * 16 * 8 = 1024
```

The extra green precision is the initial reference allocation, not a claim that it is perceptually optimal. Later palette research may introduce alternatives while retaining these deterministic baseline modes.

Uniform channel quantisation uses integer nearest-level rounding and exact endpoint reconstruction. For 8 levels, output values are:

```text
0, 36, 73, 109, 146, 182, 219, 255
```

For 16 levels:

```text
0, 17, 34, 51, 68, 85, 102, 119,
136, 153, 170, 187, 204, 221, 238, 255
```

Therefore both 0 and 255 survive exactly.

## CLI / offline inspection

`asciiomium_frame_dump` accepts:

```powershell
--colors true
--colors 16
--colors 256
--colors 512
--colors 1024
```

Indexed cell colours are dumped as, for example:

```text
idx:9/rgb:255,0,0
```

RGB modes are dumped as:

```text
rgb:255,0,0
```

This exposes the representation that issue #6's VT emitter will consume.

## Visual comparison evidence

`asciiomium_color_ramp` generates an SVG containing five identical source ramps passed through 16, 256, 512, 1024 and true-colour quantizers:

```powershell
.\build\bin\DEBUG\asciiomium_color_ramp.exe --output .\build\color-modes.svg
```

The ramp contains grayscale, red/green, green/blue and mixed-channel sections. The green-heavy sections intentionally make the difference between 3/3/3 and 3/4/3 quantisation visible.

CI generates the comparison in both Debug and Release and retains the SVG files as the `asciiomium-colour-mode-ramp` workflow artifact.

## Determinism tests

Unit tests cover:

- exact true-colour identity;
- xterm base, cube and grayscale entries;
- tie-breaking for duplicated xterm colours;
- 512/1024 endpoints and threshold boundaries;
- saturated primaries and representative intermediate colours;
- renderer switching between RGB/indexed representation without changing glyph geometry;
- 1024 deterministic pseudo-random RGB vectors per mode, summarized by fixed FNV-1a hashes.

No palette code depends on VT serialization, CEF, Windows console APIs, or frame timing.
