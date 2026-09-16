# Deterministic fixtures and benchmark contract

Issue #7 establishes the local test corpus and machine-readable benchmark surface used before public websites enter the verification loop.

## Fixture bundle

The bundle lives under `fixtures/web/` and is versioned as **`web-fixtures-v1`**. `MANIFEST.json` records the exact Git blob SHA-1 for every source file and SHA-256 values for the embedded raster PNG payloads. The verifier compares the manifest against the checked-out Git objects, so line-ending conversion in the working tree cannot create false hash failures.

The bundle contains:

| Fixture | File | Purpose |
| --- | --- | --- |
| A | `flat-ui.html` | solid surfaces, borders, text sizes, form controls and inline SVG |
| B | `colour-ramps.html` | grey/RGB/hue/low-contrast/warm ramps, saturated patches and boundary steps |
| C | `raster-imagery.html` | embedded PNG landscape, face-like portrait, dark scene and high-frequency texture |
| D | `motion-scroll.html` | CSS motion, moving gradient, timer and nested scrolling |
| E | `input-focus.html` | keyboard/focus/hover/drag/forms/nested scroll plus mapping targets at all four corners and centre |

`index.html` links the complete set for manual browsing.

Every page is self-contained apart from `common.css` and `common.js`, both in the same directory. There are no remote fonts, scripts, styles, images, fetches, WebSockets or external APIs. Raster samples are embedded as `data:image/png;base64,...` payloads.

For stable Chromium screenshots append `?freeze=1`; shared fixture bootstrap then pauses CSS animations and the motion timer reports `frozen`.

## Fixture verification

From the repository root:

```powershell
pwsh ./tools/verify_web_fixtures.ps1
```

The verifier:

1. checks `MANIFEST.json` against Git blob hashes;
2. scans HTML/CSS/JS for forbidden remote/network patterns;
3. verifies the five coordinate-mapping targets and nested scroll region in the input fixture;
4. locates Edge or Chrome;
5. renders the index and all five fixture pages headlessly at 1280×900;
6. requires a non-empty PNG screenshot for every page.

Use `-SkipBrowser` when only integrity/static checks are wanted.

## Offline benchmark

`asciiomium_benchmark` is the first implementation of a benchmark runner abstraction that later live CEF pipeline work can feed with source/diff/emission counters. Today it measures the deterministic CPU renderer and reference full-frame VT serializer against a generated `synthetic-rgba-v1` source.

Typical run:

```powershell
.\build\bin\RELEASE\asciiomium_benchmark.exe `
  --samples 120 `
  --source-width 1280 --source-height 720 `
  --columns 160 --rows 50 `
  --filter box --colors 1024 `
  --json .\build\benchmark.json
```

The synthetic source is deterministic for a given width/height and its FNV-1a 64-bit content hash is included in the result. The fixture bundle version and manifest path are also recorded.

### Default colour mode

The benchmark defaults to **1024-colour 3/4/3 RGB**. This is the project’s practical reference appearance after direct 256-vs-1024 Windows Terminal comparison showed 256-colour output to be substantially too coarse for gradients. True colour remains the fidelity oracle; 256 remains compatibility/testing coverage.

## JSON schema

The result schema identifier is `asciiomium-benchmark-v1`. A result records:

- fixture bundle version and manifest path;
- deterministic source ID/hash and source viewport size;
- terminal columns/rows;
- glyph encoder, colour mode and sampling filter;
- sample count, frame cap and total wall time;
- source/rendered/emitted frame rates;
- coalesced/dropped source-frame counts;
- min/mean/p50/p95/max render-conversion timing;
- diff timing when a differential path exists;
- min/mean/p50/p95/max VT serialization timing;
- bytes per frame and per second;
- process CPU time, normalized CPU utilization, working set and private bytes where Windows exposes them;
- reproducible interaction-latency proxy when a live/input harness exists.

Metrics that do not exist in the current offline path are represented as JSON `null`, not invented zeroes. In particular, `diff` and `interaction_latency_ms` remain `null` until those pipeline stages are implemented.

## Why the fixture corpus is separate from browser integration

These pages are generic local Chromium fixtures, not CEF-specific tests. Issue #7 proves their existence, integrity and normal Chromium renderability before #8 introduces CEF OSR. Later stages should reuse the same pages rather than creating ad-hoc browser-only examples.

When #8 lands, the fixture pages become the source for static CEF frame validation. Motion and input work later reuse fixtures D and E without changing their stable IDs.
