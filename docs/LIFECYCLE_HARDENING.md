# Lifecycle hardening: bounded issue #16 slice

This change addresses terminal callback lifetime, write/restore ordering,
truthful cleanup evidence, and two small input/render boundary defects on top
of the issue #13 runtime. It does **not** complete issue #16 or its V7 physical
acceptance gates. It does not merge the issue #14 differential writer or issue
#15 telemetry work.

## Terminal ownership and output ordering

Windows invokes a console control handler on a separate thread; callback use of
the session must therefore be synchronized with normal destruction. The
implementation uses three narrowly scoped gates:

| Gate | Ownership | Ordering rule |
| --- | --- | --- |
| Process registry | Active session pointer and the callback's entire use of it | Callback acquires this before the I/O gate |
| Session I/O | Acquisition flags, a complete output payload, terminal cleanup | Released before normal handler removal or callback drain |
| Session restoration | Public `Restore()` through final result publication | Callback never acquires this gate |

A restoration request rejects later `Write()` calls. An already accepted
payload finishes before the cleanup sequences begin, including all partial
writes. Once cleanup starts, no application payload can be written through
that session. This guarantee applies to `TerminalSession::Write`; direct
diagnostic writes to stdout/stderr remain the caller's responsibility.

Normal restoration attempts VT cleanup while VT output is still enabled,
restores code pages and modes, removes the control handler with neither the
registry nor I/O gate held, then drains callback use under the registry gate.
The ownership slot remains reserved until handler removal, so a replacement
session cannot have its handler removed by the previous session. A delayed
callback sees either the pinned active object or an empty slot. Destruction
cannot finish while a callback still uses the object.

An emergency close callback only performs terminal-state cleanup. It does not
unregister itself and does not wait for CEF shutdown. The cleanup helper uses
fixed VT sequences and scalar error fields, so memory allocation failure
cannot bypass the remaining mode/code-page restoration steps.

Microsoft documents both a separate callback thread and a system-controlled
close timeout. Console APIs may already be unreliable during close/logoff/
shutdown; delivery of some signals also depends on process circumstances.
These paths remain best effort. Hard process termination, a blocked OS output
call, host failure and power loss cannot be repaired by a cleanup guarantee.
Sources: [HandlerRoutine](https://learn.microsoft.com/en-us/windows/console/handlerroutine),
[SetConsoleCtrlHandler](https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler).

## Restoration results and live evidence

`Restore()` returns a `TerminalRestoreResult` without throwing:

- `completed`: all cleanup attempts and callback draining finished;
- `failed_steps`: a bit mask of operations that reported failure;
- `first_error`: the first immediately captured Win32 error, or zero;
- `succeeded()`: completed and no failed steps.

Zero-progress cleanup writes use `ERROR_WRITE_FAULT`. A failed API that leaves
no error code uses `ERROR_GEN_FAILURE`, so a failed step never carries a false
zero success code. Later steps are still attempted. Subsequent calls return
the saved result without replaying VT sequences after output mode restoration.

| Bit value | Failed operation |
| ---: | --- |
| 1 | SGR mouse encoding or mouse tracking disable |
| 2 | SGR reset |
| 4 | Cursor show |
| 8 | Alternate-screen leave |
| 16 | Input code-page restore |
| 32 | Output code-page restore |
| 64 | Input mode restore |
| 128 | Output mode restore |
| 256 | Control-handler removal |

Constructor rollback follows the same path. Its result travels in
`TerminalSessionAcquisitionError` because no session object survives the failed
constructor. The live application records that result before reporting the
startup error.

The live report retains the existing boolean `runtime.terminal_restored` and
adds `runtime.terminal_restore` with `acquisition_attempted`, `acquired`,
`completed`, `failed_steps`, and `first_error`. In a terminal run, the legacy
boolean is true only for an observed successful cleanup. A failed constructor
can have `acquired=false` and `completed=true`: partial state was rolled back.

Headless runs retain `terminal_restored=true` for compatibility, meaning no
owned terminal state needs restoration. Their detail fields are
`acquisition_attempted=false`, `acquired=false`, `completed=false`, with zero
failure/error fields. This distinguishes a headless no-op from observed
restoration. Successful Win32 calls and accepted output bytes do not prove
that the Windows Terminal GUI rendered every cleanup sequence.

Incomplete restoration makes an otherwise successful live run exit with code
11; an earlier failure code is preserved. The diagnostic is emitted after all
cleanup attempts. Constructor failures retain the existing live-loop failure
exit code and their separate restoration result. Other code relying only on
the RAII destructor still receives best-effort cleanup; code needing evidence
must explicitly inspect `Restore()`.

## Focused regression coverage

`terminal_lifecycle_tests` injects console operations rather than sending a
destructive OS close event. It captures and dispatches the production handler
on another thread. Fixed-capacity logging permits allocation rejection during
cleanup. Explicit barriers pause a partial payload, a cleanup write, and
handler removal; bounded waits detect deadlocks instead of hanging CTest.

The suite covers all acquisition calls, each cleanup operation, partial
visual/mouse acquisition, short/failed/zero-progress output, preserved rollback
failure evidence, repeat acquisition after an exception, stable concurrent
results, callback/destructor ordering, and callback dispatch while removal is
paused. The existing real-console smoke test still checks actual Win32 modes
and code pages over ten session lifetimes.

Run the Windows tests in both supported configurations:

```powershell
cmake --build build --config Debug --target terminal_lifecycle_tests terminal_console_smoke input_parser_tests live_runtime_tests
ctest --test-dir build -C Debug --output-on-failure -R "terminal_lifecycle|terminal_console_smoke|input_parser|live_runtime"
cmake --build build --config Release --target terminal_lifecycle_tests terminal_console_smoke input_parser_tests live_runtime_tests
ctest --test-dir build -C Release --output-on-failure -R "terminal_lifecycle|terminal_console_smoke|input_parser|live_runtime"
```

Two portable regressions are included:

- A short malformed UTF-8 prefix previously consumed the following valid key
  on flush, for example `E2 78` (invalid prefix plus `x`). Available
  continuation bytes are now checked before deciding to buffer more input.
  Tests preserve text, Ctrl+C and CSI Up after malformed prefixes at every
  two-part feed split.
- Popup fitting previously added an unrestricted signed coordinate to its
  dimension. `INT_MAX + 1` could overflow. Positive dimensions now determine
  a safe maximum coordinate by subtraction; tests cover both signed extrema,
  oversized popups and one-pixel views.

## Remaining issue #16 work

This slice supplies no claim of completed Windows qualification or physical
soak. Exact-commit Windows Debug/Release compilation and execution must be
recorded separately. Full acceptance still requires:

- ten actual browser launch/normal-quit cycles in Windows Terminal with a
  usable shell afterward;
- a documented emergency-quit and partial-startup check in the physical host;
- at least 30 minutes of navigation, typing, clicking, scrolling and resizing;
- integrated issue #15 telemetry showing bounded application-owned buffers,
  no frame-queue growth, and memory behavior separated from Chromium caches;
- shutdown during pending navigation/resize and process-tree checks on the
  integrated runtime.

Separate source audit findings remain tracked for follow-up: allocation failure
can escape `OnPaint`, and frame publication can change pixels/dimensions before
a dirty-rectangle allocation fails, leaving the prior generation; console,
title and URL callbacks currently convert whole strings before their retained
bounds apply. Those require focused callback/publication changes, not a claim
that this terminal fix resolves them. The current console byte reader also
lacks a hard cancellable/nonblocking-read contract.
