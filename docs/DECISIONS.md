# Architecture Decisions

This log records decisions that should not be repeatedly rediscovered by implementation agents. Amend it when evidence changes a decision.

## D-001 — Use CEF rather than a custom Chromium fork for milestone 1

**Status:** accepted.

CEF already provides Chromium/Blink/V8 plus supported off-screen rendering and browser input APIs. The experiment is about terminal representation, not maintaining a browser engine fork.

A custom Chromium or Windows Terminal fork remains a future option only if a demonstrated limitation requires it.

## D-002 — Start with C++

**Status:** accepted.

The primary implementation language should be modern C++ (C++20 or the highest level comfortably supported by the selected CEF/toolchain). CEF's native API, deterministic performance work, direct Windows/VT integration, and potential D3D11 path all favour C++.

Scripting languages may be used for fixture generation, analysis, or benchmark tooling, but should not become the core browser/render loop without a measured reason.

## D-003 — CPU `OnPaint` is the correctness path

**Status:** accepted.

Start with CEF CPU off-screen paint callbacks. Do not make accelerated shared textures a prerequisite for first pixels.

Rationale: easiest path to validate channel ordering, dirty rectangles, sampling, colour quantisation, tests, and cross-module boundaries.

## D-004 — Half-block is the baseline encoder

**Status:** accepted.

Use a single Unicode half-block cell to represent two vertical colour samples with independent foreground/background colour.

Rationale: excellent complexity-to-quality ratio, broad font support, deterministic mapping, easy mouse-coordinate model, useful reference for more sophisticated encoders.

## D-005 — 1024-colour mode is quantised RGB, not a fake indexed palette

**Status:** accepted.

The terminal's standardized extended indexed mode covers palettes such as xterm 256. For 512/1024-colour aesthetic modes ASCIIomium quantises source colours into a fixed gamut, then emits those resulting RGB values with 24-bit SGR.

## D-006 — Keep true colour as the fidelity control

**Status:** accepted.

Even if the preferred aesthetic is 512/1024-colour quantisation, an unrestricted true-colour path is necessary to determine whether visible errors come from quantisation, sampling, glyph geometry, or the terminal itself.

## D-007 — Full-frame emitter before delta emitter

**Status:** accepted.

Implement the simplest complete repaint path first. Then add a differential emitter and verify equivalent logical terminal frames.

This avoids coupling rendering correctness to cursor-motion/SGR optimisation.

## D-008 — Prefer newest-frame semantics over queued-frame completeness

**Status:** accepted.

ASCIIomium is interactive. If CEF paints faster than terminal conversion/output, coalesce/drop stale visual frames rather than accumulating latency.

## D-009 — Do not begin with DOM/HTML semantic rendering

**Status:** accepted.

A deterministic HTML+CSS-to-glyph renderer is a separate and dramatically larger problem. Chromium remains the semantic renderer.

A later hybrid mode may recover actual text/accessibility information for crisp terminal text, but it must layer on top of a working raster-derived browser.

## D-010 — Windows Terminal is the primary terminal target

**Status:** accepted.

Use Windows Terminal's Unicode, VT, true-colour, and mouse capabilities as the reference environment. Avoid lowest-common-denominator design for legacy terminals during milestone 1.

## D-011 — Input coordinates are cell-granular initially

**Status:** accepted.

Windows Terminal's established VT mouse model reports terminal cell coordinates. A pixel-addressed SGR mouse mode is not a baseline assumption. Map cell centres (or a defined cell-local policy) into browser viewport coordinates and document precision limits.

## D-012 — No page-specific DOM injection required for core operation

**Status:** accepted.

Core browsing/render/input must work without injecting JavaScript or rewriting page DOM/CSS. Experimental later features may interact with browser semantic/accessibility APIs, but the baseline should remain a generic browser-display adapter.

## D-013 — Terminal state restoration is correctness, not polish

**Status:** accepted.

Alternate-screen state, cursor visibility, input/mouse modes, and SGR state must be restored reliably. Teardown bugs are release blockers for an interactive terminal application.

## D-014 — Optimisations require measurements and a reference path

**Status:** accepted.

Dirty-region conversion, SIMD, threads, D3D shared textures, GPU compute, and custom frame pacing are welcome only when they preserve testable reference behaviour and have benchmark evidence.