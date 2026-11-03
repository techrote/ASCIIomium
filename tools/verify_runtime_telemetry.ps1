param(
  [string]$BinaryDirectory = 'build/bin/RELEASE',
  [string]$OutputDirectory = 'build/telemetry-evidence'
)
$ErrorActionPreference = 'Stop'
$binary = (Resolve-Path $BinaryDirectory).Path
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
$output = (Resolve-Path $OutputDirectory).Path
$revision = (git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine benchmark source revision' }
$tree = (git rev-parse 'HEAD^{tree}').Trim()
$fixtureHash = (Get-FileHash -Algorithm SHA256 'fixtures/web/MANIFEST.json').Hash.ToLowerInvariant()

& (Join-Path $binary 'runtime_telemetry_tests.exe') --report (Join-Path $output 'unit-report.json')
if ($LASTEXITCODE -ne 0) { throw 'Telemetry unit tests failed' }
$unit = Get-Content -Raw (Join-Path $output 'unit-report.json') | ConvertFrom-Json
if ($unit.schema -ne 'asciiomium-runtime-telemetry-v1' -or $null -ne $unit.output.bytes_written_to_terminal) { throw 'Unit report schema/availability invalid' }

& (Join-Path $binary 'asciiomium_telemetry_overhead.exe') --iterations 100000 --json (Join-Path $output 'overhead.json')
if ($LASTEXITCODE -ne 0) { throw 'Telemetry overhead measurement failed' }
$overhead = Get-Content -Raw (Join-Path $output 'overhead.json') | ConvertFrom-Json
if ($overhead.schema -ne 'asciiomium-telemetry-overhead-v1' -or $overhead.trials -ne 7) { throw 'Invalid overhead report' }
if ($overhead.history_retained_samples_each -ne 2048) { throw 'Telemetry history did not remain bounded' }
# Timing observations are retained without a flaky machine-speed threshold.

$cases = @(
  @{ Name='static-full'; Fixture='flat-ui'; Mode='full'; Timers='on'; Compare=$true },
  @{ Name='static-differential'; Fixture='flat-ui'; Mode='differential'; Timers='on'; Compare=$true },
  @{ Name='motion-full'; Fixture='motion-scroll'; Mode='full'; Timers='on'; Compare=$true },
  @{ Name='motion-differential'; Fixture='motion-scroll'; Mode='differential'; Timers='on'; Compare=$true },
  @{ Name='motion-timers-on'; Fixture='motion-scroll'; Mode='differential'; Timers='on'; Compare=$false },
  @{ Name='motion-timers-off'; Fixture='motion-scroll'; Mode='differential'; Timers='off'; Compare=$false }
)
$summaries = @()
foreach ($case in $cases) {
  $path = Join-Path $output "$($case.Name).json"
  $arguments = @('--fixture', $case.Fixture, '--width', '640', '--height', '360',
    '--columns', '160', '--rows', '50', '--fps', '20', '--cef-fps', '60',
    '--colors', '1024', '--filter', 'box', '--fit', 'contain', '--benchmark', '3s',
    '--emitter', $case.Mode, '--telemetry', $case.Timers, '--no-terminal', '--quiet', '--report', $path)
  if ($case.Fixture -eq 'flat-ui') { $arguments += '--freeze' }
  if ($case.Compare) { $arguments += '--compare-emitter' }
  & (Join-Path $binary 'asciiomium_live.exe') @arguments
  if ($LASTEXITCODE -ne 0) { throw "Live benchmark failed: $($case.Name)" }
  $legacy = Get-Content -Raw $path | ConvertFrom-Json
  $data = $legacy.telemetry
  if (-not $legacy.load.complete -or $legacy.load.error_code -ne 0) { throw 'Benchmark fixture failed to load' }
  if ($case.Fixture -eq 'motion-scroll' -and $data.output.rendered_frames -lt 10) { throw 'Animated fixture produced too few rendered frames' }
  if ($legacy.schema -ne 'asciiomium-live-v1' -or $data.schema -ne 'asciiomium-runtime-telemetry-v1') { throw 'Wrong telemetry schema transition' }
  if (-not $data.measurement.completed_normally -or $data.measurement.exit_reason -ne 'duration') { throw 'Benchmark did not exit normally at duration' }
  if (-not $data.measurement.benchmark_mode -or $data.measurement.requested_duration_ms -ne 3000 -or $data.measurement.wall_seconds -lt 3.0) { throw 'Benchmark duration contract failed' }
  if ($data.identity.source_revision -ne $revision -or $data.identity.source_tree -ne $tree -or $data.identity.source_dirty) { throw 'Benchmark source identity is not clean and exact' }
  if ($data.identity.asciiomium_version -ne '0.1.0' -or -not $data.identity.cef_headers_match_runtime) { throw 'Build/CEF identity missing or mismatched' }
  if ([string]::IsNullOrWhiteSpace($data.identity.compiler) -or [string]::IsNullOrWhiteSpace($data.identity.chromium_runtime)) { throw 'Compiler/Chromium identity missing' }
  if ($data.configuration.fixture_manifest_sha256 -ne $fixtureHash -or $data.configuration.fixture_bundle_version -ne 'web-fixtures-v1') { throw 'Fixture provenance mismatch' }
  if ($data.configuration.terminal_columns -ne 160 -or $data.configuration.terminal_rows -ne 50 -or $data.configuration.color_mode -ne '1024') { throw 'Benchmark configuration missing' }
  if ($data.output.rendered_frames -lt 1 -or -not $data.output.frame_accounting_consistent) { throw 'Frame accounting invalid' }
  if ($null -ne $data.output.bytes_written_to_terminal -or $null -ne $data.timing.terminal_write) { throw 'Headless report fabricated terminal activity' }
  if ($legacy.timing_ms.terminal_write.available -or $legacy.timing_ms.terminal_write.average -ne 0 -or $legacy.timing_ms.terminal_write.scope -notmatch 'legacy numeric placeholders') { throw 'Legacy headless timing availability/scope invalid' }
  if ($data.output.bytes_serialized -ne $legacy.emission.bytes_serialized) { throw 'Legacy/new serialized byte counts differ' }
  if ($data.source.presentation_generation_end -lt $data.source.last_rendered_generation) { throw 'Source generation counters regressed' }
  if ($case.Timers -eq 'on') {
    if (-not $data.instrumentation.timings_and_process_enabled -or $data.instrumentation.process_samples -ne 2) { throw 'Instrumentation status invalid' }
    if ($data.timing.conversion.sample_count -ne $data.output.rendered_frames -or $data.timing.selected_serialization.sample_count -ne $data.output.rendered_frames) { throw 'Timing sample count differs from successful rendered frames' }
    if ($data.timing.conversion.percentile_sample_count -gt 2048 -or $data.timing.conversion.mean_ms -le 0) { throw 'Missing/bounded timing history invalid' }
    if ($null -eq $data.process.cpu_time_ms -or $data.process.cpu_interval_seconds -le 0 -or $data.process.end.working_set_bytes -le 0 -or $data.process.end.private_bytes -le 0) { throw 'Windows process measurements unavailable' }
    if ($data.process.cpu_interval_source -ne 'steady-clock midpoints around GetProcessTimes') { throw 'CPU denominator is not tied to its own samples' }
  } else {
    if ($data.instrumentation.timings_and_process_enabled -or $null -ne $data.process -or $null -ne $data.timing.conversion -or $null -ne $data.timing.selected_serialization) { throw 'Disabled instrumentation fabricated measurements' }
    if ($legacy.timing_ms.conversion.available -or $legacy.timing_ms.conversion.average -ne 0 -or $legacy.timing_ms.conversion.scope -notmatch 'legacy numeric placeholders') { throw 'Disabled legacy timing availability/scope invalid' }
  }
  if ($case.Compare) {
    if ($data.output.same_frame_full_bytes -le 0 -or $null -eq $data.output.same_frame_differential_to_full_ratio) { throw 'Paired emitter comparison unavailable' }
    if ($case.Fixture -eq 'motion-scroll' -and $data.output.same_frame_differential_to_full_ratio -ge 0.9) { throw 'Motion comparison did not reduce actual serialized bytes' }
  } elseif ($null -ne $data.output.same_frame_full_bytes) { throw 'Unrequested comparison was performed' }
  $summaries += [ordered]@{
    case = $case.Name
    fixture = $case.Fixture
    emitter = $case.Mode
    timings = $case.Timers
    same_frame_comparison = $case.Compare
    wall_seconds = $data.measurement.wall_seconds
    rendered_frames = $data.output.rendered_frames
    emitted_frames = $data.output.emitted_frames
    bytes_per_second = $data.output.bytes_per_second
    conversion = $data.timing.conversion
    planning = $data.timing.diff_planning
    serialization = $data.timing.selected_serialization
    process = $data.process
    differential_to_full_ratio = $data.output.same_frame_differential_to_full_ratio
    report_sha256 = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLowerInvariant()
  }
}

foreach ($arguments in @(
    @('--benchmark','0s','--no-terminal'),
    @('--benchmark','3s','--duration-ms','4000','--no-terminal'),
    @('--benchmark','3s','--max-frames','1','--no-terminal'),
    @('--telemetry','invalid','--no-terminal'))) {
  & (Join-Path $binary 'asciiomium_live.exe') @arguments | Out-Null
  if ($LASTEXITCODE -ne 2) { throw "Invalid CLI options were accepted: $arguments" }
}

[ordered]@{
  schema = 'asciiomium-telemetry-qualification-v1'
  source_revision = $revision
  source_tree = $tree
  configuration = Split-Path $binary -Leaf
  qualification = 'Automated headless CEF/runtime telemetry; no physical terminal writes or acceptance claimed'
  timing_scope = 'Whole-run means, bounded rolling percentiles; process excludes Chromium child processes'
  overhead_note = 'Microbenchmark estimates clock/history work only. Separate live on/off runs are observations with browser scheduling variation, not paired-frame causal speedup claims.'
  overhead = $overhead
  benchmarks = $summaries
} | ConvertTo-Json -Depth 16 | Set-Content -Encoding utf8 (Join-Path $output 'summary.json')
$summaries | ForEach-Object { [pscustomobject]$_ } |
  Format-Table case, rendered_frames, emitted_frames, bytes_per_second, differential_to_full_ratio
# The last native command intentionally returned 2 for invalid CLI options.
# Signal success only after every assertion and evidence write has completed.
exit 0
