# ASCIIomium × TempToPC — first remote Windows acceptance rehearsal

**Status: prepared, NOT executed.** This is a TempToPC infrastructure qualification and a limited ASCIIomium GUI rehearsal, **not** issue #18's physical Windows 11/Windows Terminal acceptance, not a GPU benchmark, and not a v0.1 release.

## Exact source and scope

- ASCIIomium accepted source/candidate: \`54ae4c52c3d9055c73e20621a265d71c79e8327b\`.
- Successful Windows producer workflow: [45124800724](https://github.com/techrote/ASCIIomium/actions/runs/45124800724).
- Producer artifact name: \`asciiomium-portable-windows\`. This build is the same qualified tree as the #45 integrated source.
- TempToPC reusable workflow pinned at \`9715e40344156c99a9c0152c860ee175eddca889\`.
- The workflow on this repository is **manual-only** (\`workflow_dispatch\`); the default inputs target the above already-built candidate. It does not rebuild ASCIIomium.
- Windows VM: GitHub-hosted \`windows-2022\` RDP session on a private Tailscale address. Desktop/renderer and permissions can differ from the owner's physical machine.
- Linux Codespace: XFCE/noVNC on a **private** GitHub port 6080 is the **controller**. Current TempToPC does **not** offer browser-embedded RDP: Windows GUI access is via \`mstsc\` on a separate, tailnet-connected Windows computer.

## One-time requirements (the calling ASCIIomium repository)

Configure these secrets in [ASCIIomium → Settings → Secrets and variables → Actions](https://github.com/techrote/ASCIIomium/settings/secrets/actions):

- \`TAILSCALE_AUTHKEY\`: ephemeral, reusable, restricted tag; with tailnet ACL/grants allowing **only the owner's specific device** to connect to the runner on TCP 3389.
- \`TEMPTOPC_RDP_PASSWORD\`: distinct strong password, at least 20 characters, for the temporary non-admin \`temptopc-test\` account.

Never paste credentials, password, tokens, live site sessions or private tailnet details into GitHub issues or evidence artifacts. Follow the full [TempToPC setup and ACL guide](https://github.com/techrote/temptopc/blob/main/docs/SETUP.md). The workflow itself checks secret presence and does not claim RDP availability merely from dispatch acceptance.

## Launch from the existing TempToPC Linux Codespace

Open the private noVNC desktop; in its terminal:

\`\`\`bash
git pull --ff-only
bash scripts/windows.sh preflight techrote/ASCIIomium
bash scripts/windows.sh start techrote/ASCIIomium 30 45124800724 asciiomium-portable-windows 54ae4c52c3d9055c73e20621a265d71c79e8327b
\`\`\`

The second command dispatches **one** bounded 30-minute Windows runner, not a background job. Do not retry an ambiguous dispatch automatically. Alternatively manually select the [TempToPC Windows project acceptance workflow](https://github.com/techrote/ASCIIomium/actions/workflows/temptopc-acceptance.yml) and use its populated defaults.

Open the new Actions run summary in ASCIIomium, get the **private** Tailscale \`100.x.y.z\` address, connect from the owner's tailnet-connected Windows PC using \`mstsc\`, and sign in as \`temptopc-test\`. If this fails, **record the stage**: dispatch / artifact download / Tailscale / RDP listening / logon / desktop. A rejected logon is not proof that the application failed.

Cancel the Actions run when done rather than leaving the 30-minute reservation idle.

## Stage and verify the prebuilt artifact inside the RDP session

Start a **non-admin PowerShell** in RDP. \`C:\TempToPC\Artifacts\` must contain the downloaded ZIP plus its external custody and verifier files. Paste the following into PowerShell. This verifies source and SHA-256 custody **before** extraction. If the non-admin user cannot access Python, record that as a provisioning blocker; do not skip verification.

\`\`\`powershell
$ErrorActionPreference = 'Stop'
$expectedRevision = '54ae4c52c3d9055c73e20621a265d71c79e8327b'
$artifacts = 'C:\TempToPC\Artifacts'
$feedback = 'C:\TempToPC\Feedback'
$zips = @(Get-ChildItem -LiteralPath $artifacts -Filter 'ASCIIomium-*-windows-x64-dev-*.zip' -File)
if ($zips.Count -ne 1) { throw "Expected exactly one portable ZIP, got $($zips.Count)" }
$zip = $zips[0]
$custody = Get-Content (Join-Path $artifacts 'CUSTODY.json') -Raw | ConvertFrom-Json
if ($custody.source_revision -ne $expectedRevision -or $custody.zip_file -ne $zip.Name) { throw 'Unexpected candidate identity' }
$expectedHash = ([string]$custody.zip_sha256).ToLowerInvariant()
$actualHash = (Get-FileHash -LiteralPath $zip.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
if ($expectedHash -notmatch '^[0-9a-f]{64}$' -or $actualHash -ne $expectedHash) { throw 'ZIP custody mismatch' }

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $python) { throw 'Python missing from RDP user PATH; record environment blocker' }
$verifier = Join-Path $artifacts 'verify_package.py'
$verificationReport = Join-Path $feedback 'asciiomium-package-verification.json'
& $python.Source $verifier --zip $zip.FullName --expected-sha256 $expectedHash --expected-revision $expectedRevision --report $verificationReport
if ($LASTEXITCODE -ne 0 -or -not (Get-Content $verificationReport -Raw | ConvertFrom-Json).success) { throw 'Package verification failed' }

$destination = Join-Path $env:LOCALAPPDATA ('TempToPC\ASCIIomium-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
Expand-Archive -LiteralPath $zip.FullName -DestinationPath $destination
& (Join-Path $destination 'asciiomium.exe') --version | Tee-Object -FilePath (Join-Path $feedback 'asciiomium-version.txt')
if ($LASTEXITCODE -ne 0) { throw 'Version command failed' }
Write-Host "Verified development package ready at $destination"
\`\`\`

The Python verifier performs strict archive-member, manifest, CEF dependency, fixture and source-revision validation; the second extraction goes to the non-admin user's local profile, not into the read-only artifact staging folder. The verifier report is kept in the feedback directory for automatic collection when the Actions run ends.

## Rehearsal (keep source/environment limitations explicit)

1. **Infrastructure first:** confirm an actual Linux noVNC controller session, workflow dispatch, private Tailscale connection, non-admin RDP logon, write access to \`C:\TempToPC\Feedback\`, and at least one disconnect/reconnect.
2. **Windows Terminal preflight:** run \`Get-Command wt.exe -ErrorAction SilentlyContinue\`; record whether Windows Terminal exists in this Server 2022 image. If absent, **do not claim Windows Terminal acceptance**. A PowerShell/console fallback is informative for RDP and basic app behavior only.
3. **Basic visual/UI:** from an actual terminal in the RDP desktop (prefer Windows Terminal if available), go to \`$destination\` and run \`.\browse.cmd --private --url https://example.com\`. Exercise the command row (\`Ctrl+Alt+O\`), Back, Reload and \`Ctrl+Alt+Q\`, keyboard, click, wheel and terminal resize where available. Try bundled fixture mode separately by running \`.\browse.cmd --private\`.
4. **Record observations rather than hiding limitations:** redraw, ANSI colour, clipping, input forwarding, RDP latency, process close, terminal restoration and any errors. A visible webpage and the absence of crashes are helpful, but not a pass for GPU fidelity, terminal performance, or issue #18's 30-minute physical soak.
5. **Save evidence:** redact any personal browsing data and put notes, environment inventory and screenshots under \`C:\TempToPC\Feedback\`. Example environment commands:

\`\`\`powershell
Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion |
  Format-List | Out-File 'C:\TempToPC\Feedback\graphics.txt'
(Get-Command wt.exe -ErrorAction SilentlyContinue | Select-Object Name,Source |
  Format-List | Out-String) | Out-File 'C:\TempToPC\Feedback\windows-terminal.txt'
'Infrastructure / artifact / RDP / terminal / fixture / input / restoration: enter PASS, FAIL or BLOCKED with observations' |
  Set-Content 'C:\TempToPC\Feedback\manual-checklist.txt'
\`\`\`

At the end, TempToPC's workflow uploads **only explicit files** from \`C:\TempToPC\Feedback\` (7-day retention). The Windows machine is ephemeral. Collect screenshots and notes before it closes. Do not upload secrets, profiles, cache contents or full browsing history.

## Decision record

This rehearsal tests whether **TempToPC can make a source-pinned GUI app ergonomically accessible in remote Windows**, not whether ASCIIomium is release-ready. Use FAIL/BLOCKED separately for infrastructure, candidate verification, Windows Terminal availability and actual application behavior. Keep ASCIIomium issue [#18](https://github.com/techrote/ASCIIomium/issues/18) open until genuine physical Windows Terminal and security/distribution gates are satisfied. Any reusable infrastructure fixes belong in **TempToPC**, never in ASCIIomium's CI.
