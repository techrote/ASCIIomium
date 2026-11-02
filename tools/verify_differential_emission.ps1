param(
  [string]$BinaryDirectory = 'build/bin/RELEASE',
  [string]$OutputDirectory = 'build/differential-evidence'
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path $BinaryDirectory).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
$revision = (git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine source revision' }

& (Join-Path $binary 'asciiomium_differential_benchmark.exe') --samples 240 --fps 20 --colors 1024 --json (Join-Path $output 'synthetic.json')
if ($LASTEXITCODE -ne 0) { throw 'Synthetic differential benchmark failed' }
$synthetic = Get-Content -Raw (Join-Path $output 'synthetic.json') | ConvertFrom-Json
if ($synthetic.schema -ne 'asciiomium-differential-benchmark-v1') { throw 'Wrong differential benchmark schema' }
if ($synthetic.results[0].steady_differential_bytes -ne 0 -or $synthetic.results[0].empty_frames -ne 239) { throw 'Static frames did not become empty deltas' }
if ($synthetic.results[1].differential_to_full_ratio -ge 0.2) { throw 'Synthetic motion did not reduce actual output bytes' }

$results = @()
foreach ($fixture in @('flat-ui', 'motion-scroll')) {
  $report = Join-Path $output "$fixture.json"
  $snapshot = Join-Path $output "$fixture.vt"
  $arguments = @('--fixture', $fixture, '--width', '640', '--height', '360',
    '--columns', '160', '--rows', '50', '--fps', '20', '--cef-fps', '60',
    '--duration-ms', '4000', '--emitter', 'differential', '--compare-emitter',
    '--no-terminal', '--quiet', '--report', $report, '--vt-output', $snapshot)
  if ($fixture -eq 'flat-ui') { $arguments += '--freeze' }
  & (Join-Path $binary 'asciiomium_live.exe') @arguments
  if ($LASTEXITCODE -ne 0) { throw "Live differential fixture failed: $fixture" }
  $data = Get-Content -Raw $report | ConvertFrom-Json
  if (-not $data.load.complete -or -not $data.runtime.closed_cleanly) { throw "Fixture did not load/close: $fixture" }
  if ($data.renderer.terminal_columns -ne 160 -or $data.renderer.terminal_rows -ne 50) { throw 'Wrong comparison geometry' }
  if ($data.emission.schema -ne 'asciiomium-emission-v1' -or $data.emission.mode -ne 'differential') { throw 'Wrong live emitter mode/schema' }
  if (-not $data.emission.comparison_enabled) { throw 'Same-frame comparison was not enabled' }
  if ($data.emission.same_frame_full_bytes -le 0) { throw 'Full reference emitted no bytes' }
  if ($data.emission.bytes_serialized -ne $data.emission.same_frame_differential_bytes) { throw 'Measured output differs from selected differential output' }
  if ($data.scheduler.rendered_frames -ne ($data.scheduler.emitted_frames + $data.emission.empty_payload_frames)) { throw 'Render/nonempty/empty frame accounting mismatch' }
  if ($null -ne $data.emission.bytes_written_to_terminal) { throw 'Headless run fabricated terminal writes' }
  if ($data.emission.full_reconstructions -lt 1) { throw 'No initial reconstruction' }
  if ((Get-Item $snapshot).Length -le 24000) { throw 'Standalone full-frame snapshot missing/too small' }
  $ratio = $data.emission.same_frame_differential_bytes / $data.emission.same_frame_full_bytes
  if ($fixture -eq 'motion-scroll' -and ($data.scheduler.rendered_frames -lt 10 -or $ratio -ge 0.9)) {
    throw "Motion fixture did not demonstrate actual bandwidth savings: frames=$($data.scheduler.rendered_frames) ratio=$ratio"
  }
  $results += [ordered]@{
    fixture = $fixture
    rendered_frames = $data.scheduler.rendered_frames
    emitted_frames = $data.scheduler.emitted_frames
    full_reference_bytes = $data.emission.same_frame_full_bytes
    differential_bytes = $data.emission.same_frame_differential_bytes
    differential_to_full_ratio = $ratio
    full_bytes_per_rendered_frame = $data.emission.same_frame_full_bytes / $data.scheduler.rendered_frames
    differential_bytes_per_rendered_frame = $data.emission.bytes_per_rendered_frame
    full_bytes_per_second = $data.emission.same_frame_full_bytes / $data.runtime.duration_seconds
    differential_bytes_per_second = $data.emission.bytes_per_second
    changed_cells = $data.emission.changed_cells
    changed_runs = $data.emission.changed_runs
    planning = $data.emission.differential_planning
    serialization = $data.emission.differential_serialization
    reference_serialization = $data.emission.same_frame_full_reference_timing
    snapshot_sha256 = (Get-FileHash -Algorithm SHA256 $snapshot).Hash.ToLowerInvariant()
  }
}

# Explicit invalidation must refresh even when Chromium stops painting.
$repaintReport = Join-Path $output 'forced-repaint.json'
& (Join-Path $binary 'asciiomium_live.exe') --fixture flat-ui --freeze --width 640 --height 360 --columns 160 --rows 50 --duration-ms 1800 --fps 20 --emitter differential --full-repaint-ms 300 --no-terminal --quiet --report $repaintReport
if ($LASTEXITCODE -ne 0) { throw 'Forced reconstruction run failed' }
$repaint = Get-Content -Raw $repaintReport | ConvertFrom-Json
if ($repaint.emission.full_reconstructions -lt 2 -or $repaint.scheduler.forced_refreshes -lt 1) { throw 'Periodic invalidation did not refresh a static page' }
if (-not $repaint.runtime.closed_cleanly) { throw 'Forced reconstruction did not close cleanly' }

# Debug diagnostics can write into the terminal without causing a page paint.
# Synthetic and real input use one dispatch wrapper and must both invalidate.
$diagnosticReport = Join-Path $output 'diagnostic-repaint.json'
& (Join-Path $binary 'asciiomium_live.exe') --fixture flat-ui --freeze --width 640 --height 360 --columns 160 --rows 50 --duration-ms 2200 --fps 20 --emitter differential --input-debug --input-hex 78 --no-terminal --quiet --report $diagnosticReport
if ($LASTEXITCODE -ne 0) { throw 'Diagnostic reconstruction run failed' }
$diagnostic = Get-Content -Raw $diagnosticReport | ConvertFrom-Json
if (-not $diagnostic.input.hex_sent -or $diagnostic.input.decoded_events -lt 1) { throw 'Synthetic diagnostic input was not delivered' }
if ($diagnostic.emission.full_reconstructions -lt 2 -or $diagnostic.scheduler.forced_refreshes -lt 1) { throw 'Input diagnostics did not invalidate the retained display' }
if (-not $diagnostic.runtime.closed_cleanly) { throw 'Diagnostic reconstruction did not close cleanly' }

[ordered]@{
  schema = 'asciiomium-differential-qualification-v1'
  source_revision = $revision
  configuration = Split-Path $binary -Leaf
  qualification = 'automated headless CEF and exact serialized bytes; physical Windows Terminal acceptance outstanding'
  pairing = 'Full and differential serializers consume each identical logical frame in one run; comparison adds CPU work.'
  static_caveat = 'The live scheduler already idles a stable source; synthetic repeated frames separately prove zero delta payload.'
  fixtures = $results
  periodic_full_reconstructions = $repaint.emission.full_reconstructions
  diagnostic_full_reconstructions = $diagnostic.emission.full_reconstructions
} | ConvertTo-Json -Depth 12 | Set-Content -Encoding utf8 (Join-Path $output 'summary.json')
$results | Format-Table fixture, rendered_frames, full_reference_bytes, differential_bytes, differential_to_full_ratio
