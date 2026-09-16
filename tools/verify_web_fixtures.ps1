param(
  [string]$OutputDir = "build/fixture-screenshots",
  [switch]$SkipBrowser
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$fixtureRoot = Join-Path $repoRoot "fixtures/web"
$manifestPath = Join-Path $fixtureRoot "MANIFEST.json"
$manifest = Get-Content -Raw $manifestPath | ConvertFrom-Json

if ($manifest.schema -ne "asciiomium-web-fixture-manifest-v1") {
  throw "Unexpected fixture manifest schema: $($manifest.schema)"
}
if ($manifest.bundle_version -ne "web-fixtures-v1") {
  throw "Unexpected fixture bundle version: $($manifest.bundle_version)"
}
if ($manifest.hash_kind -ne "git-blob-sha1") {
  throw "Unexpected fixture hash kind: $($manifest.hash_kind)"
}

foreach ($entry in $manifest.files.PSObject.Properties) {
  $relative = "fixtures/web/$($entry.Name)"
  $actual = (& git -C $repoRoot rev-parse "HEAD:$relative").Trim()
  if ($LASTEXITCODE -ne 0 -or $actual -ne [string]$entry.Value) {
    throw "Fixture hash mismatch for $relative. expected=$($entry.Value) actual=$actual"
  }
}

$fixtureTextFiles = Get-ChildItem $fixtureRoot -File | Where-Object {
  $_.Extension -in @(".html", ".css", ".js")
}
$forbidden = @(
  '(?i)\bhttps?://',
  '(?i)(?:src|href)\s*=\s*["'']//',
  '(?i)\bfetch\s*\(',
  '(?i)\bXMLHttpRequest\b',
  '(?i)\bWebSocket\s*\(',
  '(?i)@import\s+'
)
foreach ($file in $fixtureTextFiles) {
  $text = Get-Content -Raw $file.FullName
  foreach ($pattern in $forbidden) {
    if ($text -match $pattern) {
      throw "Remote/network dependency pattern '$pattern' found in $($file.Name)"
    }
  }
}

$inputHtml = Get-Content -Raw (Join-Path $fixtureRoot "input-focus.html")
foreach ($target in @("corner-top-left", "corner-top-right", "center", "corner-bottom-left", "corner-bottom-right")) {
  if ($inputHtml -notmatch [regex]::Escape("data-target-id=`"$target`"")) {
    throw "Input fixture missing mapping target: $target"
  }
}
if ($inputHtml -notmatch 'data-scroll-region="nested"') {
  throw "Input fixture missing nested scroll region"
}

$pages = @(
  "index.html",
  "flat-ui.html",
  "colour-ramps.html",
  "raster-imagery.html",
  "motion-scroll.html",
  "input-focus.html"
)
foreach ($page in $pages) {
  if (-not (Test-Path (Join-Path $fixtureRoot $page))) {
    throw "Missing fixture page: $page"
  }
}

Write-Host "Fixture manifest, network isolation and input target checks passed."

if ($SkipBrowser) {
  exit 0
}

$candidates = @(
  (Join-Path ${env:ProgramFiles(x86)} "Microsoft/Edge/Application/msedge.exe"),
  (Join-Path $env:ProgramFiles "Microsoft/Edge/Application/msedge.exe"),
  (Join-Path $env:ProgramFiles "Google/Chrome/Application/chrome.exe")
) | Where-Object { $_ -and (Test-Path $_) }

if ($candidates.Count -eq 0) {
  throw "No Chromium-family browser found for fixture render smoke test"
}
$browser = $candidates[0]
$outputPath = Join-Path $repoRoot $OutputDir
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$profilePath = Join-Path $outputPath "chromium-profile"
New-Item -ItemType Directory -Force -Path $profilePath | Out-Null

foreach ($page in $pages) {
  $fullPath = (Resolve-Path (Join-Path $fixtureRoot $page)).Path
  $uri = [System.Uri]::new($fullPath).AbsoluteUri + "?freeze=1"
  $screenshot = Join-Path $outputPath ($page.Replace(".html", ".png"))
  $arguments = @(
    "--headless=new",
    "--disable-gpu",
    "--disable-background-networking",
    "--disable-default-apps",
    "--disable-sync",
    "--metrics-recording-only",
    "--no-first-run",
    "--no-sandbox",
    "--hide-scrollbars",
    "--run-all-compositor-stages-before-draw",
    "--window-size=1280,900",
    "--user-data-dir=$profilePath",
    "--screenshot=$screenshot",
    $uri
  )
  $process = Start-Process -FilePath $browser -ArgumentList $arguments -Wait -PassThru
  if ($process.ExitCode -ne 0) {
    throw "Chromium render failed for $page with exit code $($process.ExitCode)"
  }
  if (-not (Test-Path $screenshot) -or (Get-Item $screenshot).Length -lt 1024) {
    throw "Chromium did not produce a usable screenshot for $page"
  }
  Write-Host "Rendered $page -> $screenshot"
}

Remove-Item -Recurse -Force $profilePath -ErrorAction SilentlyContinue
Write-Host "All deterministic fixture pages rendered successfully in $browser"
