# Console input lifetime

This related issue #16 slice makes main-loop input polling independent of a
blocking console read and drains that read before normal terminal restoration.
It preserves the existing VT/UTF-8 parser and browser key semantics. Windows
qualification, the physical Windows Terminal checks and integrated issue #15
telemetry remain separate gates.

## Why a readiness check was insufficient

The previous implementation called `ReadFile` on the main thread after a
zero-timeout wait reported a signalled input handle. A retained focus, menu or
key-up record can signal the handle while providing no character. The
high-level read discards such records and can then wait for a character,
stopping the browser/render loop. Microsoft describes this behavior in its
[high-level console input documentation](https://learn.microsoft.com/en-us/windows/console/high-level-console-input-and-output-functions).

There is a second constraint: UTF-8 conversion can leave bytes cached between
high-level reads. Switching to `PeekConsoleInput` or `ReadConsoleInput` to
filter records can clear that converted-byte cache. The inspected Microsoft
Terminal source is commit `385c0c75f7f42a33c43f2508ecc8b7eb5d4f6a0d`:
[inputBuffer.cpp, `PeekCached` and `_switchReadingModeSlowPath`](https://github.com/microsoft/terminal/blob/385c0c75f7f42a33c43f2508ecc8b7eb5d4f6a0d/src/host/inputBuffer.cpp)
and [stream.cpp, `GetChar` and `ReadCharacterInput`](https://github.com/microsoft/terminal/blob/385c0c75f7f42a33c43f2508ecc8b7eb5d4f6a0d/src/host/stream.cpp).

## One reader and a fixed mailbox

Raw keyboard mode starts one `TerminalInputReader` as the last acquisition
step. The worker calls explicit `ReadConsoleW` with a 1,024-unit UTF-16 buffer,
then converts into its fixed 4,096-byte mailbox. This avoids the console-side
UTF-8 loss observed in the hosted narrow-read transcript below. The native
capacity and returned count are **UTF-16 units**, while the mailbox and public
read limit remain **bytes**. Microsoft documents the W interface, console-only
handle requirement and shared raw-mode behavior in
[ReadConsole](https://learn.microsoft.com/en-us/windows/console/readconsole).
The session already requires genuine console handles. Existing mode/code-page
ownership remains unchanged; wide input does not depend on the active input
code page. No fallback, A/W switching, readiness gate, low-level event read or
flush is used.

The allocation-free encoder retains at most one high surrogate between native
reads. A matching low surrogate emits the four-byte UTF-8 scalar. An isolated
low surrogate becomes U+FFFD; an unmatched high surrogate becomes U+FFFD and
the following unit is processed again, preserving a following key or ESC.
A trailing high surrogate waits for the next read without a timer flush. A
read that supplies only that high surrogate made native progress and produces
no mailbox bytes yet; it is not EOF. ASCII, NUL and all VT/control bytes retain
their exact values. The worst output from one native read is
`3 * 1024 + 3 = 3075` bytes, including replacement of a carried high surrogate.
Compile-time bounds keep every per-unit write inside the mailbox.

The worker waits until the consumer drains all published bytes before issuing
another native read. This provides backpressure without an application input
queue that grows with typing or mouse traffic. `ReadInputAvailable(max_bytes)`
only takes the mailbox mutex and copies at most the lesser of its requested
limit and the available bytes. It never calls a console input read or waits for
a key. UTF-8 scalars and VT sequences can span consumer drains; the existing
incremental parser remains unchanged.

The mailbox and synchronization state belong to a shared worker state, not to
a raw `TerminalSession` pointer. UTF-16 staging and encoder state live on the
worker's stack. The session owns the worker thread and borrows its input handle;
injected operation contexts and the handle must outlive the joined worker.
Shared-state allocation happens before any terminal mutation. A read error,
zero-unit completion or out-of-range returned count stops the producer and is
reported to the caller; it does not create a busy retry loop. During active operation the
transport preserves the byte order. Once teardown requests stop, unread
application bytes and an incomplete surrogate may be discarded; they are not
replayed into a later shell. A transport error remains an error rather than
being turned into invented replacement input for an incomplete surrogate.

## Stop, cancellation and restoration

Normal `Restore()` first closes admission to new writes and input polls, then
requests reader stop. A producer waiting behind a nonempty mailbox wakes and
exits. For an outstanding read, restoration calls `CancelSynchronousIo` on the
worker's native thread handle and checks for actual completion. None of these
waits holds the terminal callback registry or output gate.

Cancellation success is only a request. `ERROR_NOT_FOUND` can race with entry
into `ReadConsoleW`, so cancellation is retried with completion waits of at most
10 ms. The loop and subsequent native thread-exit wait share a two-second
deadline. Joining occurs only after the native thread handle signals. A failed
cancellation request does not make cleanup fail if the worker nevertheless
finishes. Microsoft explicitly distinguishes cancellation requests from
completion in [CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio)
and [Canceling Pending I/O Operations](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations).

Each native count starts at zero. A reported console cancellation behavior is
`ReadConsoleW` returning `TRUE` without updating that count
([Microsoft Terminal issue 17791](https://github.com/microsoft/terminal/issues/17791)).
After the call the worker checks requested stop before processing or publishing
the result. Actual read/thread completion still determines a successful drain;
it does not require one particular BOOL/error combination. The real-console
suite also checks cancellation followed by retained typeahead and a new session
on the same console, since cancellation's effect on subsequent reads needs
native qualification.

After the join, normal restoration performs visual cleanup, restores original
code pages and modes, removes the handler and drains callback use. A close,
logoff or shutdown callback only requests reader stop. It performs available
output cleanup without cancelling or joining. Input mode and input code-page
restoration are deferred while a read can still be active; if the reader has
already completed its final read, the callback can safely attempt them.
Normal teardown later finishes any deferred steps. The callback never marks
the entire session restoration completed by itself.

If the reader cannot be joined within the deadline, restoration records the
input-drain failure bit (512), attempts output cleanup, emits an explicit
incomplete-cleanup diagnostic and terminates with exit code **12**. It never
returns successful cleanup, detaches a live worker, or frees a buffer/context
that the worker might use. No final live JSON report or completed CEF shutdown
is claimed on this fatal path. The diagnostic conservatively says that input
restoration is not confirmed, because a concurrent emergency callback may
have completed some steps.

The deadline bounds application retries and waits; it cannot forcibly bound
an OS API that itself fails to return, a blocked output call or scheduling
delays. Emergency Windows control signals still have the operating system's
own timeout and best-effort limitations. This is not a hard real-time shutdown
guarantee.

## Setter failure after mutation

Acquisition marks each differing mode/code page as potentially owned **before**
attempting its setter. This allows rollback even if the setter reports failure
after mutating state. This is a concrete Windows console behavior: the pinned
[SetConsoleInputModeImpl implementation](https://github.com/microsoft/terminal/blob/385c0c75f7f42a33c43f2508ecc8b7eb5d4f6a0d/src/host/getset.cpp)
stores the mode before some validation for compatibility.

Restoration retains API failures even if a later readback happens to match the
baseline. An invalid original mode combination can be reinstated while the
setter still reports `FALSE`; a matching bit pattern does not turn that failed
operation into a successful result.

## Focused qualification

`terminal_input_tests` uses the production reader with injected read/cancel
operations and explicit synchronization barriers. It checks:

- main-loop polls complete while the input operation is blocked;
- UTF-8 scalar boundaries, ASCII including NUL, and malformed-surrogate
  recovery preserve a following key or VT sequence;
- one-, two- and three-unit native reads and a pair split at the 1,024-unit
  boundary produce the same exact UTF-8 bytes;
- a high-only read waits for its partner without a false EOF;
- the 3,075-byte maximum expansion and per-unit canaries verify buffer bounds;
- fragmented UTF-8/VT bytes retain their order across repeated mailbox fills;
- the producer cannot overwrite or grow an unread mailbox;
- stop wakes a producer held by backpressure;
- cancellation is repeated across the before-read `ERROR_NOT_FOUND` race;
- successful cancellation alone is not accepted as completion;
- failed cancellation followed by actual completion can still join;
- a timed-out worker retains usable state until a later explicit release;
- TRUE cancellation with an untouched count still drains after requested stop;
- read failure, EOF and invalid counts stop further reads without hiding an
  incomplete surrogate or creating a hot retry loop.

`terminal_input_console` starts fresh hidden Windows console children. The
normal child establishes a known cooked-mode/CP437 baseline, then uses genuine
`WriteConsoleInputW`, `ReadConsoleW`, `CancelSynchronousIo` and console mode APIs.
It verifies retained non-character records cannot block main-loop polling;
BMP and surrogate-pair text, literal VT sequences and pre-acquisition typeahead
survive unchanged; actual native reads limited to one, two and three UTF-16
units preserve pair boundaries without another key while consumer drains
split UTF-8 bytes; and idle/nonempty-mailbox teardown
restores the baseline. It injects setter failure only after a genuine mode
change and directly calls the captured production close handler to verify
deferred input restoration. This direct callback call is not an OS close-event
delivery test.

The normal child also successfully acquires a session with the live visual,
mouse and raw-input options, waits for an active native read, then throws a
distinct startup sentinel. Its outer catch requires exact original modes/code
pages, completed read cancellation and handler removal **before** the fixture's
fallback cleanup can run. It reacquires on the same console and reads retained
Unicode typeahead. The injected lifecycle counterpart verifies the full
mouse/SGR/cursor/alternate-screen cleanup sequence before its outer catch.
These exercise successful-constructor stack unwinding, separately from the
existing failed-constructor rollback tests; no CEF or physical screen behavior
is claimed by these component fixtures.

A separate child has a deliberately non-completing injected read on a real
console. The parent requires an armed marker immediately before explicit
`Restore()`, exit 12, the incomplete-cleanup diagnostic, and no input mode or
code-page setter during the failed drain. An unexpected return exits with a
distinct code without stack unwinding, so a later destructor cannot make the
death test pass. This checks the fatal contract; it does not claim that a real
console `ReadConsoleW` was observed refusing cancellation.

The parent gives the normal child 30 seconds and the fatal child 10 seconds,
then attempts termination and checks its wait outcome. Failures retain the
temporary child log and print bounded output plus exit/wait/termination codes.
CTest has an outer 60-second deadline. Both build configurations include the
new targets:

```powershell
cmake --build build --config Debug --target terminal_input_tests terminal_input_console terminal_lifecycle_tests terminal_console_smoke
ctest --test-dir build -C Debug --output-on-failure -R "terminal_input|terminal_lifecycle|terminal_console_smoke"
cmake --build build --config Release --target terminal_input_tests terminal_input_console terminal_lifecycle_tests terminal_console_smoke
ctest --test-dir build -C Release --output-on-failure -R "terminal_input|terminal_lifecycle|terminal_console_smoke"
```

### Native failure diagnostics

The first hosted result at `ae578e5c2c27adc09a150220e0c1aec1bca86e89`
([run 41559971458](https://github.com/techrote/ASCIIomium/actions/runs/41559971458))
compiled the native test on Windows Server 2022 build 20348. The injected input
and terminal lifecycle tests passed, but the first mixed Unicode/VT native
case failed after 5.06 seconds waiting for its expected byte count. That log
did not contain the returned bytes, so it does not establish whether the
console conversion or application transport caused the failure.

The diagnostic follow-up at `f321cae8e18a2a791479e0f07b377d775fddd213`
([run 41600126282](https://github.com/techrote/ASCIIomium/actions/runs/41600126282))
kept every original input string, exact byte assertion, native byte limit and
five-second deadline. It failed immediately on the wrong prefix and captured
this native result **before mailbox publication**:

```text
injected UTF-16 units: 21; all 21 written
input mode: 0x280; input code page: 65001
ReadFile: requested=4096, ok=1, returned=21, error=0
native:   78000000001B5B411B4F501B5B3C303B333B344D03
expected: 78C2A3E282ACF09F98801B5B411B4F501B5B3C303B333B344D03
```

The expected stream has 26 bytes. The four UTF-16 units for `£`, `€` and the
surrogate pair became four NUL bytes in the native narrow read; the following
VT/control bytes remained intact. This establishes conversion loss before the
application mailbox. It does not identify the exact console-host binary: the
window-owner query in this run named the test executable itself. Microsoft's
[Terminal PR 14745](https://github.com/microsoft/terminal/pull/14745) documents
a related historical narrow-conversion sizing defect; no specific host patch
level is inferred from it.

The repair keeps those exact fixtures and byte assertions and the five-second
deadline. The native limits are now one, two and three **UTF-16 units**, and UI
drains still split at one, two and three bytes. Failure evidence separates the
units returned by `ReadConsoleW` from the encoded expected/received bytes. A
fixed test-only trace retains the first 16 completions, each with at most 32
raw UTF-16 units; byte displays remain capped at 96. Native qualification stays
open until exact-revision Windows assertions pass. No input-event reads,
flushes, fallback transport, skipped fixture or extended deadline are added.

Linux runs using a mutex-backed Win32 type shim exercise the injected tests and
portable concurrency logic only. They do not qualify real console behavior,
Windows thread handles or native cancellation. Exact-revision Windows CI must
record those results separately. Physical prompt recovery, emergency quit,
30-minute interaction/resize acceptance and integrated telemetry remain open.
