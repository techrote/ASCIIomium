# Real CEF lifecycle phase qualification

Issue #16 extends the real loopback runtime suite with ten fresh private
process launches. This is a bounded shutdown regression, not a physical Windows
Terminal or integrated-telemetry soak. It does not satisfy the human V7 gate.

## Phase matrix

Run the following five cases twice, in order:

| Phase | Required state before normal shutdown | What this establishes |
| --- | --- | --- |
| `ready` | Main fixture title and completed load observed | Repeated clean creation/normal close |
| `before-created` | `CreateBrowser` accepted, creation ownership registered, no message-loop pump yet, no created-browser callback observed | Close requested while asynchronous creation is pending |
| `navigation-requested` | First page settled, next `open` request accepted, no intervening pump | Close immediately after queued navigation; no network-start claim |
| `held-load` | Unique `LOADING:<run-id>` title observed while aggregate loading is true and main load completion is false | Close while an actual committed HTTP response body is incomplete |
| `ready` | Fresh process again reaches the settled page | Normal close after the exceptional phases |

The held-response server flushes more than 4 KiB of HTML containing its unique
title, declares the total content length, and withholds the suffix on an event.
The harness releases that event only after the probe process exits or its
failure cleanup finishes. A 40-second safeguard is a failure if reached; elapsed
time alone cannot pass the phase. The response's flushed/released/finished and
expiry fields are preserved in the summary.

Every successful case must report its exact reached phase and complete report
stage, clean CEF close, supervisor worker exit zero, complete process-tree
reaping without forced descendants, and successful private-root removal. The
probe writes a failed pre-shutdown checkpoint first, so an exhausted CEF close
deadline can still leave phase evidence for its supervisor. That checkpoint is
never accepted as a completed case.

The existing persistent write/restart/ownership-release cases separately cover
reuse of a persistent profile lock. The ten extra cases use private roots to
keep ownership, descendant lifetime and cleanup independently observable.

## Source and failed-run evidence

`asciiomium_runtime_probe --identity` reports compiled revision/tree, dirty and
availability flags, configuration, CEF compile identity and runtime ABI match.
The harness verifies these against its checkout and verifies the same identity
in each probe report. It also records the executable SHA-256. The existing
source-identity helper is reused exactly from the #15/#17 candidates; no
differential or telemetry feature is brought into this branch by that reuse.

Each run is recorded before launch. Its exit/timeout/start error, elapsed time,
report presence/parse failure and complete stdout/stderr files survive an
assertion failure. Process waits use the process handle and file redirection,
so an inherited output pipe cannot prevent the harness from observing leader
exit. Timeout cleanup selects only the PID held by its own `Popen` object and
its descendants; it never selects arbitrary processes by executable name.

Six portable process-evidence controls verify success, nonzero exit with
partial output, timeout/reaping, missing-executable retention, and suite-level
missing-probe/loopback-bind setup failures. They do not
pretend to execute Chromium. The real phase matrix runs on Windows against
the built CEF executable, with assertions intact on any failure.

## Reproduce

From a clean checkout on Windows:

```powershell
cmake --build build --config Release --target asciiomium_runtime_probe
python tests/verify_browser_runtime.py `
  --exe build/bin/RELEASE/asciiomium_runtime_probe.exe `
  --output build/browser-runtime-evidence.json `
  --lifecycle-cycles 10
```

Keep the JSON summary and `build/runtime-probes/` files with the source SHA and
CI run. The full P0 acceptance still requires ten actual interactive browser
launch/quit cycles, terminal input/visual restoration, the 30-minute Windows
Terminal soak, and telemetry on the final integrated source. No headless result
supplies those observations.
