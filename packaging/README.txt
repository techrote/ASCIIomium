ASCIIomium -- unsigned Windows x64 development / qualification package

Extract the entire ZIP into a writable folder. Use Windows Terminal on Windows
x64. From a terminal, run browse.cmd, or browse.cmd --url https://example.com.
The default launch displays the bundled motion-scroll fixture. The real browser
is asciiomium_live.exe; asciiomium.exe supplies version and terminal diagnostics.

Ctrl+Alt+O opens a temporary browser command row. Enter a command and press Enter:
  open https://example.com   back   forward   reload   hard-reload
  stop   status   repaint   quit
Esc dismisses the row. Ctrl+Alt+Q always quits.

Profiles default to %LOCALAPPDATA%\ASCIIomium\Profiles\Default. Use --profile
with a separate directory outside this package, or --private for an ephemeral
session. Private cleanup runs after CEF shutdown; a cleanup failure is an error.
Do not store passwords or sensitive browsing data in a qualification session.

Downloads are disabled unless --downloads DIR is supplied. Enabled downloads
use a unique child directory and download.bin; inspect files before opening.
Popups, external protocols, camera/microphone/geolocation/notification requests,
certificate errors, and unsupported browser dialogs are denied explicitly.

This direct-executable prototype runs CEF without its process sandbox. It is
unsigned and has NOT completed physical Windows Terminal acceptance or the
30-minute interactive soak. It is not a v0.1 release or production browser.

Check identity:
  asciiomium.exe --version
  asciiomium_live.exe --version

With Python 3.10+ available, verify the extracted files:
  python verify_package.py --directory .
Optional automated headless smoke (requires Windows):
  python verify_package.py --directory . --smoke --report ..\package-smoke.json

PROVENANCE.json binds source, build and dependency identities. SHA256SUMS.json
contains every payload file's size and SHA-256 except its own recursive entry.
The distribution's separate CUSTODY.json and ZIP .sha256 file bind the archive.
These checks establish consistency with trusted custody data, not a signature.

CEF-LICENSE.txt and Chromium/third-party CEF-CREDITS.html are under licenses/.
No ASCIIomium-owned license grant is asserted by this development package.

Repository and tracked limitations: https://github.com/techrote/ASCIIomium
