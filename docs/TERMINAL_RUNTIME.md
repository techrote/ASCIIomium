# Terminal runtime

Issue #3 introduces ASCIIomium's Windows Terminal ownership layer. It is deliberately independent of CEF/browser startup.

## Owned state

`TerminalSession` is an RAII owner for the process's active console session. On acquisition it records:

- input console mode;
- output console mode;
- input code page;
- output code page;
- current viewport geometry.

It then enables:

- `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on output;
- `ENABLE_VIRTUAL_TERMINAL_INPUT` on input;
- UTF-8 (`CP_UTF8`) input/output code pages;
- optional alternate screen (`CSI ? 1049 h`);
- optional hidden cursor (`CSI ? 25 l`).

Mouse tracking is intentionally **not** enabled by issue #3. That state belongs to the later input implementation.

## Teardown order

Restoration is idempotent. Normal teardown occurs in this order:

1. reset SGR while VT processing is still active;
2. show the cursor when ASCIIomium hid it;
3. leave the alternate screen when ASCIIomium entered it;
4. restore original input/output code pages;
5. restore original input/output console modes;
6. remove the console control handler.

The destructor calls the same restoration path, so exceptions after successful acquisition do not require call-site cleanup.

## Console-control handling

`Ctrl+C` and `Ctrl+Break` set a stop flag and suppress default immediate termination. The diagnostics loop notices that flag and returns normally, allowing RAII teardown on the main thread.

`CTRL_CLOSE_EVENT`, logoff and shutdown are different: Windows may terminate the process shortly after invoking the handler. ASCIIomium therefore performs synchronous best-effort restoration from the handler before returning. This is a fallback, not a guarantee against forced process termination.

No application can repair terminal state after hard termination such as `TerminateProcess`, host failure, power loss, or comparable destruction that prevents cleanup code from running.

## Geometry and resize detection

Viewport dimensions come from `GetConsoleScreenBufferInfo(...).srWindow`, not from the backing buffer's total allocation size.

`GeometryTracker` publishes:

```text
columns
rows
generation
```

The generation increments only when valid dimensions actually change.

The issue #3 diagnostics loop refreshes geometry every 100 ms and sleeps between refreshes. This is intentionally a bounded timed refresh rather than a hot poll. It avoids consuming stdin events before the dedicated input subsystem exists. Later browser/input work can replace the diagnostics-loop policy while retaining the same geometry abstraction.

## Diagnostic mode

From Windows Terminal:

```powershell
.\build\bin\DEBUG\asciiomium.exe --terminal-diagnostics
```

The default mode:

- enters the alternate screen;
- hides the cursor;
- displays current columns/rows and geometry generation;
- displays an animated frame counter;
- displays 24-bit RGB samples;
- displays UTF-8 Unicode upper-half-block samples;
- refreshes after resize;
- exits with `Ctrl+C` and restores the previous terminal state.

For an automatically terminating run:

```powershell
.\build\bin\DEBUG\asciiomium.exe --terminal-diagnostics --duration-ms 3000
```

For a non-destructive one-shot snapshot that does not enter/clear the alternate screen:

```powershell
.\build\bin\DEBUG\asciiomium.exe --terminal-diagnostics --no-alt-screen
```

The one-shot path leaves the cursor visible and returns immediately after writing the sample.

## Automated verification

`terminal_logic_tests` verifies pure behaviour:

- acquire/restore VT sequences;
- SGR RGB sequences;
- cursor positioning;
- geometry generation changes;
- diagnostic frame properties;
- the no-alternate-screen snapshot never enters or clears the alternate screen.

`terminal_console_smoke` exercises real Win32 console state. It opens or allocates a console and performs ten acquire/restore cycles. Each cycle verifies:

- VT input mode is active while owned;
- VT output mode is active while owned;
- UTF-8 code pages are active while owned;
- geometry is valid;
- original input/output modes are restored exactly;
- original input/output code pages are restored exactly.

The first cycle additionally calls `Restore()` twice to prove idempotence; subsequent cycles rely on destructor-driven cleanup.

## Manual Windows Terminal acceptance

CI can validate Win32 console state and VT construction, but it does not render through the Windows Terminal GUI. Before treating the visual portion of issue #3 as accepted on a release machine, run the diagnostic in actual Windows Terminal and verify:

1. colour bars render in colour;
2. upper-half-block glyphs render as glyphs rather than replacement boxes;
3. resizing updates the displayed dimensions/generation;
4. no uncontrolled scrolling/wrapping occurs at ordinary sizes;
5. `Ctrl+C` returns to the previous screen/prompt with a visible cursor;
6. repeat launch/exit ten times and confirm the shell remains usable.
