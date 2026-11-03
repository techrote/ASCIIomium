# Browser runtime (#13)

## Profiles

`asciiomium_live` uses `%LOCALAPPDATA%\ASCIIomium\Profiles\Default` by
default. `--profile DIR` selects another persistent profile. The path is
canonicalized, must be outside the source/application directories, and is
locked exclusively until CEF has shut down. A second owner fails clearly;
a stale lock filename after a crash does not prevent the next launch.

`--private` uses a unique temporary root with an empty CEF cache setting and
session-cookie persistence disabled. It cannot be combined with `--profile`.
The ordinary OSR browser runs in a worker process in the same console. A small
supervisor owns the root and exclusive lock, waits for worker/CEF descendant
process exit, and then removes the entire root, including installation/default
profile data Chromium may create despite the empty cache setting. No headless
Chrome mode or metrics/permission workaround is enabled.

This process boundary is necessary because CEF shutdown is not process exit:
pinned Chromium creates process-lifetime objects, and Windows CI demonstrated
sharing violations after completed CEF shutdown. Increasing an in-process wait
did not resolve them. A synthetic Windows unit deliberately retains a writable
file mapping until process exit and verifies real root removal afterward.
The observed residue also exposed `chrome_debug.log`; the startup logging policy
below disables that destination independently rather than relying on deletion.

The worker receives the supervisor-created root through a restricted inherited
pipe using a one-use environment entry. There is no arbitrary-path deletion
argument, and user arguments are not rewritten. Only standard handles and the
pipe are inherited. Assignment to a kill-on-close Windows job precedes worker
execution. Normal CEF shutdown remains mandatory, followed by proof that the
job has no live processes before deletion. A remaining descendant is terminated
and treated as an error. The parent consumes Ctrl+C/Break while the worker's
existing control handler performs teardown, then reaps and cleans.

Only the supervisor finalizes private cleanup evidence after actual deletion;
intermediate worker reports mark cleanup deferred. Cleanup retries have a fixed
two-second budget and preserve attempt count/OS error. Unicode temporary paths,
literal `--report` input values, retained mappings, failed workers and report-write
errors have Windows controls. Hard termination of the supervisor, system shutdown
or power loss can still leave its temporary root; this is not forensic erasure.

The pinned CEF 151 settings use the same absolute directory for persistent
`root_cache_path` and `cache_path`; Chromium owns its `Default` child. Session
cookies are deliberately persisted in persistent mode alongside expiring
cookies, local storage and IndexedDB. Separate roots do not share this state.

## Commands

Press **Ctrl+Alt+O**, type a command, and press Enter. Escape cancels. The
command occupies a temporary bottom row; the browser geometry does not change.
Normal page text and Ctrl shortcuts continue through the shared VT parser.
**Ctrl+Alt+Q** remains available both in a page and in command entry.

| Command | Behaviour |
| --- | --- |
| `open https://example.com/` | Navigate the current browser; HTTP, HTTPS, local file URLs and `about:blank` are supported |
| `back` / `forward` | Use Chromium's history |
| `reload` / `hard-reload` | Reload normally / bypass Chromium's cache |
| `stop` | Stop the current load; aborted loads do not terminate the application |
| `status` | Show the current URL, title, loading state and latest policy notice temporarily |
| `repaint` | Request fresh rendering from the newest available source frame |
| `quit` | Close CEF and restore the terminal |

Interactive network errors leave command navigation available. Bounded
headless runs still fail on a requested load error. Host status text is bounded
and stripped to printable ASCII before becoming terminal cells, so a page
title cannot inject terminal escape sequences. Non-ASCII URL/title characters
are shown as `?` in the small status surface; page rendering is unchanged.

## Policies

| Capability | Policy |
| --- | --- |
| Popup/new window, `window.open`, modified-click new tabs | Deny with a notice; one browser owns the viewport. Ordinary same-tab navigation remains available. OSR dropdown surfaces still render normally. |
| External protocols | Deny; never launch an operating-system handler |
| Downloads | Disabled by default with a notice. Explicit `--downloads DIR` permits at most four active downloads. Each receives its own new subdirectory and the fixed name `download.bin`; remote names never become paths. No overwrite or automatic execution. Completion shows the path. |
| Camera/microphone/screen capture | Explicitly cancel media access |
| Geolocation, notifications and other permission prompts | Explicit deny callback, with a notice |
| Clipboard prompts | Denied through the generic permission callback; ordinary CEF keyboard shortcuts retain Chromium's own rules |
| Invalid TLS certificate | Cancel; no bypass option |
| HTTP/proxy auth and client-certificate selection dialogs | Unsupported; cancel credentials / supply no client certificate. Ordinary website forms and redirects use CEF normally. |
| JavaScript modal dialogs | Suppress; before-unload navigation proceeds without a confirmation dialog |
| File picker/upload selection | Cancel with an unsupported notice |

Explicitly enabled downloads survive private-mode exit: they are an intentional
user-selected output, not private browser storage. Interrupted downloads may
leave incomplete files in that destination. To inspect a downloaded file,
first identify its content and rename it deliberately; filenames/extensions
from a remote server are not trusted.

CEF's startup app adds `--disable-logging` for browser and child processes before
Chromium selects a log destination. `LOGSEVERITY_DISABLE` alone maps to a fatal
minimum in this pin and did not prevent a startup `chrome_debug.log`; the live
qualification now checks absence of diagnostic files in persistent test roots.
Reports include origins and numerical
error/policy state, never URL credentials/path/query/fragment or page titles.
Arbitrary page console messages are consumed. Existing raw `--input-debug`
is limited to the explicitly selected bundled fixture and stops recording
after navigation away; command text is not sent to that diagnostic logger.
Screenshots/SVG/VT exports can of course contain whatever page the user elects
to capture; they are explicit output options.

The pre-existing **unsandboxed direct-executable CEF bootstrap remains**. These
policies do not turn it into a hardened general-purpose browser. Reworking that
distribution security posture remains a packaging/release gate; no v0.1 claim
is made by this implementation.

## Reproducible automated qualification

Build `browser_commands_tests`, `profile_session_tests`, `private_process_tests`, and
`asciiomium_runtime_probe` as well as `asciiomium_live`. CTest covers command
interception through real VT bytes, bounds, URL/log sanitization, exclusive
profile ownership, restart, private cleanup and failure paths.

Pinned lifecycle/logging references: [CEF Chrome delegate](https://github.com/chromiumembedded/cef/blob/f059e67/libcef/common/chrome/chrome_main_delegate_cef.cc),
[CEF Chrome logging patch](https://github.com/chromiumembedded/cef/blob/f059e67/patch/patches/chrome_common_logging.patch),
[Chromium startup](https://github.com/chromium/chromium/blob/151.0.7922.138/chrome/app/chrome_main_delegate.cc),
[global histogram allocator lifetime](https://github.com/chromium/chromium/blob/151.0.7922.138/base/metrics/persistent_histogram_allocator.cc).

Run from the repository on Windows:

```powershell
python tests/verify_browser_runtime.py `
  --exe build/bin/RELEASE/asciiomium_runtime_probe.exe `
  --output build/browser-runtime-evidence.json
```

The Python standard-library harness serves loopback HTTP and HTTPS fixtures,
launches separate CEF processes, verifies expiring/session cookies, local
storage and IndexedDB after clean restarts, checks isolation/private reset,
tests a genuine simultaneous profile owner, and drives browser navigation.
Page markers prove navigation/history/reload; actual JavaScript results prove
popup/permission denial. The test executable supplies synthetic capture devices
without granting permissions, requires a real media-denial callback and checks
`NotAllowedError`, so missing runner hardware cannot produce a false pass.
The denied-download test bounds Chromium's fallback destination and verifies
that it remains empty; enabled download bytes are verified outside CEF. A deliberately
untrusted test-only certificate proves TLS rejection. Its checked-in private
key is public fixture material and must never be used by a service.

The probe is restricted to loopback origins. Results record the tested Git
revision, individual scenarios, close/cleanup results and HTTP request counts.
CI checks out the exact PR head, builds Debug and Release, runs CTest in both,
and runs the live runtime suite in Release. The existing keyboard/mouse/live
fixtures continue to run; their headless sessions now use private profiles.

## Physical gate

Automated CEF success cannot prove Windows Terminal delivers Ctrl+Alt+O or
that the transient row is usable. Before merging #13, physically check opening
commands, cancelling, all navigation commands, network-error recovery, page
typing/Ctrl shortcuts, mouse release across command entry, resizing while the
row is visible, download notices and repeated Ctrl+Alt+Q restoration. Preserve
this gate separately from automated results. Final #16 soak and #18 real-site
acceptance remain outstanding.
