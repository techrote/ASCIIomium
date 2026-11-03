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
step. The worker calls `ReadFile` directly with `CP_UTF8` active. It never uses
input readiness gates, low-level event reads or an input flush. Text retained
before acquisition remains available through the same transport.

The worker owns a fixed 4,096-byte mailbox. It publishes a completed read and
waits until the consumer drains all published bytes before issuing another
read. This provides backpressure without an application input queue that grows
with typing or mouse traffic. `ReadInputAvailable(max_bytes)` only takes the
mailbox mutex and copies at most the lesser of its requested limit and the
available bytes. It never calls a console input read or waits for a key.
UTF-8 scalars and VT sequences may span both native reads and consumer drains;
the existing incremental parser retains that responsibility.

The read buffer and synchronization state belong to a shared worker state, not
to a raw `TerminalSession` pointer. The session owns the worker thread and
borrows its input handle; injected operation contexts and the handle must
outlive the joined worker. Reader-state allocation happens before any terminal
mutation. A read error or zero-byte EOF stops the producer and is reported to
the caller; it does not create a busy retry loop. During active operation the
transport preserves the byte order. Once teardown requests stop, unread
application bytes may be discarded; they are not replayed into a later shell.

## Stop, cancellation and restoration

Normal `Restore()` first closes admission to new writes and input polls, then
requests reader stop. A producer waiting behind a nonempty mailbox wakes and
exits. For an outstanding read, restoration calls `CancelSynchronousIo` on the
worker's native thread handle and checks for actual completion. None of these
waits holds the terminal callback registry or output gate.

Cancellation success is only a request. `ERROR_NOT_FOUND` can race with entry
into `ReadFile`, so cancellation is retried with completion waits of at most
10 ms. The loop and subsequent native thread-exit wait share a two-second
deadline. Joining occurs only after the native thread handle signals. A failed
cancellation request does not make cleanup fail if the worker nevertheless
finishes. Microsoft explicitly distinguishes cancellation requests from
completion in [CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio)
and [Canceling Pending I/O Operations](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations).

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
- fragmented UTF-8/VT bytes retain their order across repeated mailbox fills;
- the producer cannot overwrite or grow an unread mailbox;
- stop wakes a producer held by backpressure;
- cancellation is repeated across the before-read `ERROR_NOT_FOUND` race;
- successful cancellation alone is not accepted as completion;
- failed cancellation followed by actual completion can still join;
- a timed-out worker retains usable state until a later explicit release;
- read failure and EOF stop further reads.

`terminal_input_console` starts fresh hidden Windows console children. The
normal child establishes a known cooked-mode/CP437 baseline, then uses genuine
`WriteConsoleInputW`, `ReadFile`, `CancelSynchronousIo` and console mode APIs.
It verifies retained non-character records cannot block main-loop polling;
BMP and surrogate-pair text, literal VT sequences and pre-acquisition typeahead
survive unchanged; actual native reads limited to one, two and three bytes
drain UTF-8 suffixes without another key; and idle/nonempty-mailbox teardown
restores the baseline. It injects setter failure only after a genuine mode
change and directly calls the captured production close handler to verify
deferred input restoration. This direct callback call is not an OS close-event
delivery test.

A separate child has a deliberately non-completing injected read on a real
console. The parent requires an armed marker immediately before explicit
`Restore()`, exit 12, the incomplete-cleanup diagnostic, and no input mode or
code-page setter during the failed drain. An unexpected return exits with a
distinct code without stack unwinding, so a later destructor cannot make the
death test pass. This checks the fatal contract; it does not claim that a real
console `ReadFile` was observed refusing cancellation.

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

Linux runs using a mutex-backed Win32 type shim exercise the injected tests and
portable concurrency logic only. They do not qualify real console behavior,
Windows thread handles or native cancellation. Exact-revision Windows CI must
record those results separately. Physical prompt recovery, emergency quit,
30-minute interaction/resize acceptance and integrated telemetry remain open.
