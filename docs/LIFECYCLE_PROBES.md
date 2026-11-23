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
| `resize-requested` | Settled page and valid 640×360 paint observed; request 800×450 resize and do not explicitly pump again before shutdown | Close with a pending viewport resize request; no target-paint claim |

The held-response server flushes more than 4 KiB of HTML containing its unique
title, declares the total content length, and withholds the suffix on an event.
The harness releases that event only after the probe process exits or its
failure cleanup finishes. A 40-second safeguard is a failure if reached; elapsed
time alone cannot pass the phase. The response's flushed/released/finished and
expiry fields are preserved in the summary.

The resize case records its initial paint generation, initial/requested sizes
and accepted request. It requires no target-size paint before shutdown: waiting
for one would duplicate the existing capture tool's settled resize test. The
shutdown path itself pumps CEF until close, and any resulting callback failure
still fails the case. Both resize cases retain the same cleanup requirements.

Every successful case must report its exact reached phase and complete report
stage, clean CEF close, supervisor worker exit zero, complete process-tree
reaping without forced descendants, and successful private-root removal. The
probe writes a failed pre-shutdown checkpoint first, so an exhausted CEF close
deadline can still leave phase evidence for its supervisor. That checkpoint is
never accepted as a completed case.

Both checkpoint and completed report retain `browser_created_before_shutdown`.
The harness requires it to be false for `before-created` and true for the
other four phases. This is a snapshot before shutdown starts; it does not keep
the client alive across `CefShutdown` or claim that creation stayed pending
while the shutdown loop pumped.

The existing persistent write/restart/ownership-release cases separately cover
reuse of a persistent profile lock. The ten extra cases use private roots to
keep ownership, descendant lifetime and cleanup independently observable.

## Closing while creation is pending

The first real matrix at `c4a506b7` passed `ready`, then the `before-created`
worker exited with access violation `0xC0000005`. Its preserved checkpoint
reported the reached phase with no callback failure. The supervisor reaped
the process tree and removed the private root, but had to force descendants;
that cleanup did not qualify the browser shutdown. See
[run 41607422994](https://github.com/techrote/ASCIIomium/actions/runs/41607422994).

The pinned CEF source establishes a synchronous reentrancy hazard in the old
late-creation close path. `SetAsWindowless` selects Alloy. Alloy's
`CreateInternal` calls `OnAfterCreated`, then uses `platform_delegate_` again.
Calling `CloseBrowser(true)` from that callback can synchronously destroy the
windowless browser and reset that delegate before the creation continuation
uses it. This source path is consistent with the observed access violation;
the retained evidence has no crash stack proving the exact instruction.
Primary sources at CEF `f059e67fa6aad5e8cce8bebea5df706ffddfb174`:

- [Windowless runtime selection](https://github.com/chromiumembedded/cef/blob/f059e67fa6aad5e8cce8bebea5df706ffddfb174/include/internal/cef_win.h#L126-L129).
- [Creation and synchronous close/destruction](https://github.com/chromiumembedded/cef/blob/f059e67fa6aad5e8cce8bebea5df706ffddfb174/libcef/browser/alloy/alloy_browser_host_impl.cc#L201-L224), including `CloseBrowser`, `CloseContents` and `DestroyBrowser` in the same file.
- [Delegate reset during destruction](https://github.com/chromiumembedded/cef/blob/f059e67fa6aad5e8cce8bebea5df706ffddfb174/libcef/browser/browser_host_base.cc#L322-L330).
- [Public close contract permitting synchronous completion](https://github.com/chromiumembedded/cef/blob/f059e67fa6aad5e8cce8bebea5df706ffddfb174/include/cef_browser.h#L340-L362).

An early close request now remains latched through `OnAfterCreated`; that
callback records the browser without focusing or closing it during shutdown.
Each owning close loop (live, capture and `CefRuntimeSession`) services the
request immediately after `CefDoMessageLoopWork` returns, when creation has
unwound. This adds no posted task or callback allocation. Close intent is still
recorded before the first explicit pump in the pending-creation case, the existing
five-second deadline remains, and `OnBeforeClose` is still required before
`CefShutdown`. The unchanged real phase is the regression test for this CEF
continuation; exact-revision Windows qualification remains necessary.

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
