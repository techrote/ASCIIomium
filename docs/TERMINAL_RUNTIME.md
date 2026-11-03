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

Issue #12 adds opt-in VT mouse ownership for the live browser. Interactive mode enables SGR encoding (1006) plus all-motion tracking (1003), while ordinary diagnostics keep mouse tracking disabled. Issue #11's raw keyboard mode and #12 pointer mode share the same immediate VT byte stream.

## Teardown order

Restoration is idempotent. A write already accepted by `TerminalSession` drains
its entire payload, including short `WriteFile` iterations, before output cleanup can
start. Restoration closes the output gate to later payloads. Normal teardown
then attempts these operations in order:

1. request stop, cancel any raw input read, join the input reader, then close
   its separately opened `CONIN$` handle while keeping the caller's handle open;
2. disable SGR/all-motion mouse tracking when ASCIIomium enabled it;
3. reset SGR while VT processing is still active;
4. show the cursor when ASCIIomium hid it;
5. leave the alternate screen when ASCIIomium entered it;
6. restore original input/output code pages;
7. restore original input/output console modes;
8. remove the console control handler.

The destructor calls the same restoration path, so exceptions after successful
acquisition do not require call-site cleanup. Concurrent `Restore()` calls wait
for the first call to finish handler removal and callback draining; they return
the same `TerminalRestoreResult`. A failure is retained instead of being turned
into success by a later destructor call. Cleanup continues after each failed
operation and does not allocate memory. It does not retry terminal sequences
after the original console output mode has been restored.

`TerminalRestoreResult::completed` means the cleanup path and callback drain
finished. `succeeded()` additionally requires that every attempted operation
succeeded. `failed_steps` and the immediately captured `first_error` identify
incomplete cleanup. A constructor that changes console state and then fails
throws `TerminalSessionAcquisitionError`, which retains its rollback result.
Mode and code-page ownership is recorded before setter attempts because a
failed setter can still change state. A reader that cannot be joined is a
fatal incomplete-cleanup outcome: output cleanup is attempted, input changes
are deferred, and the process exits with code 12 without freeing active reader
state. Failure to close the joined reader's owned input handle is separately
reported and also exits 12 with input restoration deferred; it does not claim
an active reader. Open/thread-start failures run the same checked rollback.
See [input lifetime and cancellation](TERMINAL_INPUT.md) and
[the issue #16 scope and result contract](LIFECYCLE_HARDENING.md).

## Console-control handling

In the terminal diagnostic/default cooked-input mode, `Ctrl+C` and `Ctrl+Break` set a stop flag and suppress default immediate termination. The diagnostics loop notices that flag and returns normally, allowing RAII teardown on the main thread.

Live browser keyboard mode is different: it disables `ENABLE_PROCESSED_INPUT` so Ctrl+C is delivered to the VT parser and forwarded to CEF. That mode uses **Ctrl+Alt+Q** as the application quit chord.

`CTRL_CLOSE_EVENT`, logoff and shutdown are different: Windows may terminate the
process shortly after invoking the handler. ASCIIomium therefore attempts
synchronous terminal-state cleanup from the handler before returning. The
handler pins the session's lifetime while using it and shares the output gate
with normal cleanup. It requests input stop but never cancels or joins the
reader or closes its handle. Until the owned handle is released, the callback
limits cleanup to output state and defers input mode/code-page restoration to
normal teardown, even if the read has finished. It never
unregisters itself or waits for the CEF/main loop. Normal teardown joins the
reader and closes its handle outside callback/output gates, removes the handler outside these gates,
then waits for any dispatched callback to finish using the object.

This is a best-effort fallback. Microsoft documents a system-controlled close
timeout (normally five seconds), unreliable console APIs during close/logoff/
shutdown, and circumstances where logoff/shutdown callbacks are not delivered.
See [HandlerRoutine](https://learn.microsoft.com/en-us/windows/console/handlerroutine)
and [SetConsoleCtrlHandler](https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler).

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

The first cycle additionally calls `Restore()` twice to prove idempotence; subsequent cycles rely on destructor-driven cleanup. The final cycle enables raw keyboard mode and verifies that line, echo and processed-input flags are disabled while owned and then restored exactly. Another cycle enables 1003+1006 mouse tracking so the RAII path exercises mouse-mode acquire/disable together with exact Win32 mode/code-page restoration.

`terminal_lifecycle_tests` uses an injected console API table to fail individual
acquisition/restoration operations and pause real production callbacks and
writes at defined barriers. It verifies partial startup rollback, partial and
zero-progress output, allocation-free cleanup, stable failure results,
write/restore ordering, callback/destructor lifetime, concurrent restoration,
handler removal while a callback runs, and full visual cleanup before an outer
catch handles an exception thrown after successful acquisition. The callback is invoked directly by
the test; no OS close signal is sent. Both Windows build configurations include
this target, and CTest gives it a 30-second deadline.

`terminal_input_tests` injects blocked reads, mailbox backpressure, cancellation
races and failures. `terminal_input_console` uses fresh hidden console children
and genuine `ReadConsoleW`/`WriteConsoleInputW`/`CancelSynchronousIo` calls. It checks
retained non-character records, UTF-16 at actual one-to-three-unit native read
boundaries with exact UTF-8 byte delivery, pre-acquisition typeahead,
idle/full-mailbox shutdown, conservative mode rollback, and deferred emergency
input restoration. A successful-acquisition startup exception must unwind the
native reader/console before its outer catch, followed by reacquisition and
retained Unicode input on the same console. Separate children
check fatal drain and owned-handle close failures. The parent bounds child lifetime and retains
failure diagnostics. See [test scope and commands](TERMINAL_INPUT.md).

These tests cover program behavior and Win32 state. Ten sessions within one
test process are not evidence of ten actual browser launches or a physical
30-minute soak.

## Manual Windows Terminal acceptance

CI can validate Win32 console state and VT construction, but it does not render through the Windows Terminal GUI. Before treating the visual portion of issue #3 as accepted on a release machine, run the diagnostic in actual Windows Terminal and verify:

1. colour bars render in colour;
2. upper-half-block glyphs render as glyphs rather than replacement boxes;
3. resizing updates the displayed dimensions/generation;
4. no uncontrolled scrolling/wrapping occurs at ordinary sizes;
5. `Ctrl+C` returns to the previous screen/prompt with a visible cursor;
6. repeat launch/exit ten times and confirm the shell remains usable.


## Raw keyboard reads

`TerminalSessionOptions::raw_keyboard_input` is an issue-#11 opt-in. It
requires VT input, disables cooked line/echo/processed input plus unused
mouse/window console records, and exposes `ReadInputAvailable()`. One dedicated
reader calls `ReadConsoleW` directly, converts a bounded UTF-16 chunk to UTF-8
with at most one carried high surrogate, and publishes into a fixed 4 KiB mailbox. The
main-loop method only drains already-published bytes; it never reads or waits
on the console handle. Backpressure pauses the producer until the mailbox is
empty, without growing a queue or discarding active-session bytes.

A signalled input handle can contain only non-character records that a
high-level read discards before waiting for a character. Older console hosts
can also lose valid non-ASCII input in a narrow read despite CP65001, as the
hosted native transcript in `TERMINAL_INPUT.md` demonstrated. Explicit Unicode
reads and bounded application conversion avoid that path without mixing in
low-level event reads or flushing typeahead.
[Microsoft documents the high-level read behavior](https://learn.microsoft.com/en-us/windows/console/high-level-console-input-and-output-functions).
Cancellation, reader join and owned-handle closure precede normal input mode/code-page
restoration; [the focused input design](TERMINAL_INPUT.md) describes the entry
race, fatal failure contract and qualification limits.

The method is intentionally unavailable outside raw keyboard mode. Parser state
and CEF translation live outside `TerminalSession`; the terminal layer owns
only console state and byte transport. The original console modes and UTF-8
code-page changes are restored through the existing idempotent RAII path.


## VT mouse ownership

`TerminalSessionOptions::mouse_tracking` is independent of parser/browser
logic. It can be disabled, button-event (1002), or all-motion (1003); any
enabled mode is paired with SGR extended coordinates (1006). The live browser
uses all-motion mode to support hover.

Acquisition writes the mouse-mode VT sequence only after terminal ownership is
established. `Restore()` emits the matching disable sequence before cursor/
alternate-screen restoration, and the same idempotent path is used by normal
teardown and destructor cleanup. Headless `--no-terminal` runs never acquire
or change terminal mouse modes.

The terminal layer does not decode pointer semantics. Mouse reports remain
ordinary bytes returned by `ReadInputAvailable()` and are decoded by the
shared bounded `VtInputParser`.
