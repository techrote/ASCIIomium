# Lifecycle hardening: bounded issue #16 slice

This change addresses terminal callback lifetime, write/restore ordering,
truthful cleanup evidence, transactional frame publication, bounded browser
diagnostics, and two small input/render boundary defects on top of the issue
#13 runtime. It does **not** complete issue #16 or its V7 physical
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
| 512 | Input reader failed to drain; fatal path, no completed result returned |

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

## Frame publication and callback failures

A source frame is committed only after all required pixel and damage-list
capacity is available. If either reserve fails, pixels, dimensions, damage,
colour/alpha extrema, paint counts and generations retain the previous frame.
A successfully reserved capacity may remain for reuse after a later reserve
fails; this does not publish a new logical frame. Same-size, warmed updates
continue to reuse their existing buffers.

Both CEF damage conversion and retained frame metadata have a limit of 1,024
rectangles. A larger input list becomes one rectangle covering the full frame.
Every update still copies the complete CEF pixel buffer, so this conservative
damage representation preserves the image. It does not discard changed pixels
or create a queue. Each view/popup snapshot retains at most 1,024 rectangles.

`OnPaint` catches allocation, validation and unexpected exceptions inside the
application callback. `CallbackState` records a fatal cause using atomics and
scalar counters; recording an allocation failure requires no allocation. Later
paints are suppressed while the owning main loop starts ordinary browser close,
terminal restoration and profile cleanup. Title, address, load-error,
navigation validation, download completion and shared notice updates use the
same failure boundary. This is a scoped
boundary, not a claim that every CEF callback or operating-system failure is
recoverable.

All three JSON reports (live, capture, runtime probe) add `callbacks`:

| Field | Meaning |
| --- | --- |
| `failed` | A guarded callback failed at least once |
| `first_failure` | `none`, `paint_allocation`, `paint_invalid_input`, `paint_exception`, `diagnostic_allocation`, or `diagnostic_exception` |
| `paint_failures` | Paint operations that threw before publication completed |
| `suppressed_paints` | Later paint callbacks skipped because a fatal cause was already latched |
| `diagnostic_failures` | Guarded diagnostic/navigation updates that threw |

Accepted-frame generations and paint counts exclude rejected and suppressed
paints. A nonzero callback failure cannot pass qualification using a previously
rendered frame: live exits with its existing render-failure code 6, capture
returns a nonzero capture/callback failure, and the probe reports `passed=false`.
Failures observed during close and late IO work are included as well: reports
retain a small callback-state observer independently of the CEF client and take
their final snapshot after `CefShutdown` returns. The callback cause never
stores page data or exception text. Normal network/TLS failures remain ordinary
browser state; they do not set this internal callback-failure latch.

The capture tool also avoids `CefShutdown` when its browser misses the close
deadline. It attempts to write `closed_cleanly=false` evidence, reports any
write failure, then terminates with code 7 without destroying active CEF state.
This is failure containment, not successful graceful shutdown.

CEF's resource-request callbacks can outlive `OnBeforeClose`; shared state
remains owned by the client until its last reference is released, and the scalar
failure observer additionally remains owned by the shutdown/reporting path. No assertion
assumes that all CEF callbacks stop at the first close notification. The precise
thread/lifetime contract is described in the pinned
[CEF life-span handler](https://github.com/chromiumembedded/cef/blob/f059e67/include/cef_life_span_handler.h).

## Bounded text before conversion

Native CEF UTF-16 input is read through a bounded view rather than first copied
and converted in full. UTF-8 limits are 4,096 bytes for titles, 8,192 for displayed
addresses and failed URLs, and 2,048 for load-error descriptions. Conversion
stops at a complete Unicode scalar; isolated UTF-16 surrogates become U+FFFD.
The live report identifies title/address truncation with
`browser.title_truncated` and `browser.url_truncated`.
Overlong failed URLs are omitted from retained diagnostics, and a truncated
display address produces a fully redacted report origin. Parsing a clipped
authority could otherwise miss a later credential delimiter and expose its
prefix as if it were a host name.

Navigation accepts only a complete allowed URL within 8,192 encoded bytes.
It rejects overlong native or encoded input, so an allowed prefix cannot hide
a truncated suffix. Existing scheme/control-character checks still apply.

Ordinary console messages are consumed before conversion when fixture
observations are disabled. The fixture path additionally requires a complete,
exact trusted fixture address, the observation prefix, and both native/encoded
lengths within 16,384. Oversized and untrusted messages are discarded before
conversion; a truncated status address cannot become trusted fixture evidence.

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
cmake --build build --config Debug --target terminal_lifecycle_tests terminal_console_smoke input_parser_tests live_runtime_tests callback_boundary_tests osr_callback_tests
ctest --test-dir build -C Debug --output-on-failure -R "terminal_lifecycle|terminal_console_smoke|input_parser|live_runtime|callback_boundary|osr_callback"
cmake --build build --config Release --target terminal_lifecycle_tests terminal_console_smoke input_parser_tests live_runtime_tests callback_boundary_tests osr_callback_tests
ctest --test-dir build -C Release --output-on-failure -R "terminal_lifecycle|terminal_console_smoke|input_parser|live_runtime|callback_boundary|osr_callback"
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

`callback_boundary_tests` deliberately fails the first and second frame-store
allocations and checks the entire previously committed view/popup state. It
also covers the same-byte-count dimension-change regression, later successful
publication, allocation-free warmed updates, invalid arguments, and damage
lists immediately at/above their bound. Callback failure-latch and Unicode
conversion tests cover unknown exceptions, zero-byte budgets, all UTF-8 scalar
widths, malformed UTF-16, and oversized navigation without prefix acceptance.
An allocation-size ceiling rejects any attempt to convert a one-million-unit
diagnostic in full.

`osr_callback_tests` invokes the production CEF callbacks directly, without
initializing a browser. Allocation rejection targets damage conversion, pixel
reserve and damage reserve inside `OnPaint`; the test verifies both failure
accounting and suppression of later publication. It also proves that excess
CEF damage collapses before allocating its full list, disabled/untrusted/huge
console input allocates nothing, and large titles remain bounded. A separate
thread invokes a late protocol callback after the close notification with
allocations disabled, verifying that shared notice failures remain observable
after the client is released. These are
callback-boundary tests, distinct from real Chromium runtime qualification.

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

The related [input lifecycle slice](TERMINAL_INPUT.md) adds a bounded reader
mailbox, normal cancellation and join, deferred emergency input restoration,
and a fatal drain path that reports incomplete cleanup. It retains the same
physical and integrated telemetry gates. The scoped callback guards do not
claim to contain every allocation in every CEF callback or to survive sustained
process-wide memory exhaustion.
