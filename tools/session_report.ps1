<#
.SYNOPSIS
Summarizes one recorded Nebula session.

.DESCRIPTION
Reads the logs of a recorded session and prints frame rate, frame-time tails,
pipeline compile and GPU figures as one results row. A metric that is missing
from the logs is reported as unavailable, and a metric the runtime reports as
not measured (gpu-samples=0) is reported as such rather than as zero.

.PARAMETER Session
A session folder. Both packed layouts are accepted: a delivered diagnostics
bundle (runtime.stderr.log / runtime.stdout.log) and a perf_session.ps1 session
(stderr.log / stdout.log).

.PARAMETER Json
Optional path to also write the full result object as JSON.

.EXAMPLE
./session_report.ps1 -Session 'C:\Users\me\Downloads\diagnostics\20261007-000835-...'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Session,
    [string]$Json = '',
    # Emits the required results table instead of the narrative report, one row per
    # operating mode. A session average describes no scene when the session is bimodal.
    [switch]$ResultsTable,
    # Free text for the table's "retained change" column, supplied by the caller.
    [string]$RetainedChange = ''
)
$ErrorActionPreference = 'Stop'

$sessionPath = (Resolve-Path -LiteralPath $Session).Path

function Read-FirstLog {
    param([string[]]$Names)
    foreach ($name in $Names) {
        $path = Join-Path $sessionPath $name
        if (Test-Path -LiteralPath $path) { return [IO.File]::ReadAllText($path) }
    }
    return $null
}

function Get-Number {
    param([string]$Text, [string]$Name)
    $m = [regex]::Match($Text, ('(?m)(?:^|\s)' + [regex]::Escape($Name) + '=(-?[0-9]+(?:\.[0-9]+(?:[eE][-+]?[0-9]+)?)?)'))
    if ($m.Success) { return [double]$m.Groups[1].Value }
    return $null
}

function Get-AllNumbers {
    param([string[]]$Lines, [string]$Name)
    $values = New-Object Collections.Generic.List[double]
    foreach ($line in $Lines) {
        $m = [regex]::Match($line, ('(?:^|\s)' + [regex]::Escape($Name) + '=(-?[0-9]+(?:\.[0-9]+(?:[eE][-+]?[0-9]+)?)?)'))
        if ($m.Success) { $values.Add([double]$m.Groups[1].Value) }
    }
    return $values
}

function Get-Percentile {
    param([double[]]$Values, [double]$Percent)
    if ($Values.Count -eq 0) { return $null }
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 1) { return $sorted[0] }
    # Nearest-rank, stated so a reader can reproduce it: index = ceil(p * n) - 1.
    $index = [Math]::Ceiling($Percent * $sorted.Count) - 1
    if ($index -lt 0) { $index = 0 }
    if ($index -ge $sorted.Count) { $index = $sorted.Count - 1 }
    return $sorted[$index]
}

function Round-OrNull {
    param($Value, [int]$Digits = 3)
    if ($null -eq $Value) { return $null }
    return [Math]::Round([double]$Value, $Digits)
}

# Per-frame ratio of two fields paired within a line. A median of numerators divided by a
# median of denominators is not the median ratio, because the fields are populated on
# different frames. Lines missing either field, or with a zero denominator, are skipped and counted.
# Ratio of a numerator to (denominator - excluded), paired within a line. Early-out calls count
# in flush-calls but never reach the timed body.
function Get-PairedRatio3 {
    param([string[]]$Lines, [string]$Numerator, [string]$Denominator, [string]$Exclude)
    $ratios = New-Object Collections.Generic.List[double]
    $skipped = 0
    foreach ($line in $Lines) {
        $n = Get-Number $line $Numerator
        $d = Get-Number $line $Denominator
        $x = Get-Number $line $Exclude
        if ($null -eq $n -or $null -eq $d) { $skipped++; continue }
        if ($null -eq $x) { $x = 0 }
        $effective = $d - $x
        if ($effective -le 0) { $skipped++; continue }
        $ratios.Add($n / $effective)
    }
    return [pscustomobject]@{ ratios = $ratios; skipped = $skipped }
}

function Get-PairedRatio {
    param([string[]]$Lines, [string]$Numerator, [string]$Denominator)
    $ratios = New-Object Collections.Generic.List[double]
    $skipped = 0
    foreach ($line in $Lines) {
        $n = Get-Number $line $Numerator
        $d = Get-Number $line $Denominator
        if ($null -eq $n -or $null -eq $d -or $d -le 0) { $skipped++; continue }
        $ratios.Add($n / $d)
    }
    return [pscustomobject]@{ ratios = $ratios; skipped = $skipped }
}

$stderrText = Read-FirstLog @('runtime.stderr.log', 'stderr.log')
$stdoutText = Read-FirstLog @('runtime.stdout.log', 'stdout.log')
$exitText = Read-FirstLog @('exit.json')
$sessionText = Read-FirstLog @('session.json')
$processText = Read-FirstLog @('process.csv')
$threadsText = Read-FirstLog @('threads.csv')

if (-not $stderrText) { throw "No runtime stderr log found under $sessionPath" }
$stderrLines = $stderrText -split "`r?`n"

$result = [ordered]@{}
$result.session = $sessionPath

# [runtime-build] is printed first by the runtime. It was absent from some delivered bundles,
# so identity is sometimes reconstructed from hashes.
$buildMatch = [regex]::Match($stderrText, '(?m)^\[runtime-build\]\s*(.+?)\s*$')
$result.runtimeBuild = if ($buildMatch.Success) { $buildMatch.Groups[1].Value } else { 'unavailable' }
$result.runtimeBuildPresent = $buildMatch.Success

$identity = $null
if ($exitText) { try { $identity = $exitText | ConvertFrom-Json } catch { } }
$install = $null
$sessionObj = $null
if ($sessionText) { try { $sessionObj = $sessionText | ConvertFrom-Json; $install = $sessionObj.install } catch { } }
# Two session.json shapes exist: a diagnostics bundle embeds install.json under `install`, and a
# perf_session.ps1 session has `build`, `cacheSeed`, `environment` and `files` at the root.
# Each field is read from wherever it lives.
$rootFiles = if ($sessionObj -and $sessionObj.PSObject.Properties['files']) { $sessionObj.files } else { $null }
$installFiles = if ($install -and $install.files) { $install.files } else { $rootFiles }

$result.commit = if ($install -and $install.commit) { $install.commit } else { 'unavailable' }
$result.moduleKey = if ($install -and $install.moduleKey) { $install.moduleKey } else { 'unavailable' }
$result.runtimeIsa = if ($identity -and $identity.runtimeIsa) { $identity.runtimeIsa }
                     elseif ($install -and $install.runtimeIsa) { $install.runtimeIsa }
                     elseif ($sessionObj -and $sessionObj.runtimeIsa) { $sessionObj.runtimeIsa }
                     else { 'unavailable' }
$result.moduleIsa = if ($install -and $install.moduleIsa) { $install.moduleIsa } else { 'unavailable' }
$result.runtimeExeSha256 = if ($installFiles -and $installFiles.'NebulaRuntime.exe') { $installFiles.'NebulaRuntime.exe' } else { 'unavailable' }
$result.gameModuleSha256 = if ($installFiles -and $installFiles.'RMGE01_game.dll') { $installFiles.'RMGE01_game.dll' } else { 'unavailable' }

# ---- hardware and rendering ---------------------------------------------
$adapter = 'unavailable'
if ($stdoutText) {
    $m = [regex]::Match($stdoutText, '\[gpu\]\s*adapter="([^"]*)"\s*vram-mb=(\d+)')
    if ($m.Success) { $adapter = "$($m.Groups[1].Value) (vram-mb=$($m.Groups[2].Value))" }
}
$result.adapter = $adapter

if ($sessionText) {
    try {
        $s = $sessionText | ConvertFrom-Json
        $result.output = $s.output
        $result.aspect = $s.aspect
        $result.efbScale = $s.efbScale
        $result.monitoring = $s.monitoring
    } catch { }
}

# ---- cache condition ----------------------------------------------------
$cacheLines = @($stderrLines | Where-Object { $_ -match '^\[PipelineCache\]' })
$result.pipelineCache = if ($cacheLines.Count) { ($cacheLines -join ' | ') } else { 'unavailable' }

# Cache classification. A row is comparable only against a run with the same cache condition.
# "Loaded 0 keys" means either nothing was offered (cold seed) or offered keys did not match.
# The seed record written by perf_session.ps1 tells them apart; without it the verdict is unknown.
# [PipelineCache] lines are prose, not key=value, so Get-Number cannot read them:
#   "[PipelineCache] loaded 34 DXBC pairs from shader cache"
#   "[PipelineCache] loaded 43 PSO keys from cache"
#   "[PipelineCache] loaded D3D12 pipeline library (532944 bytes)"
$cacheBlob = $cacheLines -join "`n"
$loadedPairs = $null
$loadedKeys = $null
$pairMatch = [regex]::Match($cacheBlob, 'loaded\s+(\d+)\s+DXBC pairs')
if ($pairMatch.Success) { $loadedPairs = [double]$pairMatch.Groups[1].Value }
$keyMatch = [regex]::Match($cacheBlob, 'loaded\s+(\d+)\s+PSO keys')
if ($keyMatch.Success) { $loadedKeys = [double]$keyMatch.Groups[1].Value }
$libraryMatch = [regex]::Match($cacheBlob, 'pipeline library \((\d+) bytes\)')
$libraryBytes = if ($libraryMatch.Success) { [int]$libraryMatch.Groups[1].Value } else { 'unavailable' }
$seedOffered = if ($sessionObj -and $sessionObj.PSObject.Properties['cacheSeed']) { $sessionObj.cacheSeed } else { $null }
$seedPresent = $seedOffered -and $seedOffered.present
$cacheVerdict = 'unknown'
$cacheNote = 'no [PipelineCache] load line: cannot tell warm from cold'
if ($null -ne $loadedKeys) {
    if ($loadedKeys -gt 0) {
        $cacheVerdict = 'warm'
        $cacheNote = "runtime accepted $([int]$loadedPairs) DXBC pairs and $([int]$loadedKeys) PSO keys"
    } elseif ($seedPresent) {
        $cacheVerdict = 'key-mismatch'
        $cacheNote = "cache-seed offered $($seedOffered.files) file(s) / $($seedOffered.bytes) bytes but the runtime accepted 0 PSO keys: this is a key or compatibility failure, not a cold cache"
    } else {
        $cacheVerdict = 'cold'
        $cacheNote = 'no cache offered and none accepted: first-use FXC and driver PSO builds are included in this run'
    }
}
$result.cacheCondition = [ordered]@{
    verdict       = $cacheVerdict
    loadedPairs   = if ($null -ne $loadedPairs) { [int]$loadedPairs } else { 'unavailable' }
    loadedKeys    = if ($null -ne $loadedKeys) { [int]$loadedKeys } else { 'unavailable' }
    libraryBytes  = $libraryBytes
    seedOffered   = if ($seedOffered) { $seedOffered.present } else { 'unrecorded (pre-cacheSeed harness)' }
    seedFiles     = if ($seedOffered) { $seedOffered.files } else { $null }
    note          = $cacheNote
}

# ---- new-frame rate: all [present-stats] windows ------------------------
$presentLines = @($stderrLines | Where-Object { $_ -match '^\[present-stats\]' })
$hz = @(Get-AllNumbers -Lines $presentLines -Name 'new-serial-hz')
$xfbHz = @(Get-AllNumbers -Lines $presentLines -Name 'xfb-production-hz')
$result.presentStatsWindows = $presentLines.Count
$result.newFrameHz = [ordered]@{
    median = Round-OrNull (Get-Percentile $hz 0.5)
    p10    = Round-OrNull (Get-Percentile $hz 0.1)
    p90    = Round-OrNull (Get-Percentile $hz 0.9)
    min    = Round-OrNull (($hz | Measure-Object -Minimum).Minimum)
    max    = Round-OrNull (($hz | Measure-Object -Maximum).Maximum)
    warning = $null   # filled in by the mode detection below
}
$result.xfbProductionHzMedian = Round-OrNull (Get-Percentile $xfbHz 0.5)

# Operating modes, not just a median. A single median can sit between two modes and describe
# neither. Segment the session into runs of stable rate and report the runs, so a bimodal
# session is not mistaken for a steady one.
$windowSeries = New-Object Collections.Generic.List[object]
$tStart = $null
for ($i = 0; $i -lt $presentLines.Count; $i++) {
    $ns = $null
    $m = [regex]::Match($presentLines[$i], '(?:^|\s)steady-ns=([0-9]+)')
    if ($m.Success) { $ns = [double]$m.Groups[1].Value }
    if ($null -eq $tStart -and $null -ne $ns) { $tStart = $ns }
    $h = $null
    $mh = [regex]::Match($presentLines[$i], '(?:^|\s)new-serial-hz=(-?[0-9]+(?:\.[0-9]+(?:[eE][-+]?[0-9]+)?)?)')
    if ($mh.Success) { $h = [double]$mh.Groups[1].Value }
    if ($null -eq $h) { continue }
    $windowSeries.Add([pscustomobject]@{
        t = if ($null -ne $ns -and $null -ne $tStart) { ($ns - $tStart) / 1e9 } else { $null }
        hz = $h
    })
}

# A run continues while each window stays within MODE_TOLERANCE of the run's mean so far;
# otherwise a new run starts. Runs shorter than MODE_MIN_WINDOWS are not merged silently,
# because a short excursion such as a stall is informative.
$modeToleranceHz = 3.0
$modeMinWindows = 3
$modes = New-Object Collections.Generic.List[object]
$runStart = 0
for ($i = 1; $i -le $windowSeries.Count; $i++) {
    $closeRun = $false
    if ($i -eq $windowSeries.Count) { $closeRun = $true }
    else {
        $runMean = 0.0
        for ($k = $runStart; $k -lt $i; $k++) { $runMean += $windowSeries[$k].hz }
        $runMean = $runMean / ($i - $runStart)
        if ([Math]::Abs($windowSeries[$i].hz - $runMean) -gt $modeToleranceHz) { $closeRun = $true }
    }
    if (-not $closeRun) { continue }
    $count = $i - $runStart
    if ($count -ge $modeMinWindows) {
        $sum = 0.0
        for ($k = $runStart; $k -lt $i; $k++) { $sum += $windowSeries[$k].hz }
        $tFrom = $windowSeries[$runStart].t
        $tTo = $windowSeries[$i - 1].t
        $modes.Add([pscustomobject]@{
            fromSeconds = if ($null -ne $tFrom) { [Math]::Round($tFrom, 1) } else { $null }
            toSeconds   = if ($null -ne $tTo) { [Math]::Round($tTo, 1) } else { $null }
            windows     = $count
            meanHz      = [Math]::Round($sum / $count, 2)
        })
    }
    $runStart = $i
}
$sortedModes = @($modes | Sort-Object windows -Descending)
# Compares the longest run with the longest run that differs from it by more than
# MODE_SEPARATION. Taking the two longest runs is wrong: they can both sit at the same rate.
# The question is whether a materially different level exists and is sustained.
$modeSeparation = 0.15
$primary = if ($sortedModes.Count -ge 1) { $sortedModes[0] } else { $null }
$contrast = $null
if ($null -ne $primary -and $primary.meanHz -gt 0) {
    foreach ($candidate in $sortedModes) {
        if ([Math]::Abs($candidate.meanHz - $primary.meanHz) / $primary.meanHz -gt $modeSeparation) {
            $contrast = $candidate
            break
        }
    }
}
$bimodal = $null -ne $contrast
$result.operatingModes = [ordered]@{
    detection        = "contiguous +/-$modeToleranceHz Hz runs, >= $modeMinWindows windows"
    # .ToArray(), not @(): @() passes an existing List[object] through unchanged, and
    # OrderedDictionary rejects a List as a value.
    runs             = $modes.ToArray()
    distinctRuns     = $modes.Count
    bimodal          = $bimodal
    medianDescribesNeitherMode = $bimodal
    primaryModeHz    = if ($null -ne $primary) { $primary.meanHz } else { $null }
    primaryModeWindows = if ($null -ne $primary) { $primary.windows } else { $null }
    contrastModeHz   = if ($null -ne $contrast) { $contrast.meanHz } else { $null }
    contrastModeWindows = if ($null -ne $contrast) { $contrast.windows } else { $null }
    note             = if ($bimodal) {
        "THIS SESSION HAS AT LEAST TWO OPERATING MODES: $($primary.meanHz) Hz over $($primary.windows) windows vs $($contrast.meanHz) Hz over $($contrast.windows) windows. The session median is not a description of either; compare like-for-like segments, never the aggregate."
    } else { $null }
}
if ($bimodal) { $result.newFrameHz.warning = $result.operatingModes.note }

# Uses all windows. A mean of the last few windows misstates the tail.
$tailLines = @($stderrLines | Where-Object { $_ -match '^\[new-frame-tails\]' })
$prodMean = @(Get-AllNumbers -Lines $tailLines -Name 'production-mean-us')
$prodP95 = @(Get-AllNumbers -Lines $tailLines -Name 'production-p95-upper-us')
$prodP99 = @(Get-AllNumbers -Lines $tailLines -Name 'production-p99-upper-us')
$prodMax = @(Get-AllNumbers -Lines $tailLines -Name 'production-max-us')
$intervals = @(Get-AllNumbers -Lines $tailLines -Name 'production-intervals')
# Is the slowdown quantized to whole 60 Hz fields, or continuous? Integer slips are a scheduling
# problem; a smear is more work per frame. `production-max-us` decides it because it is exact
# (`monitor_max_ns`), unlike the `-upper-us` fields, which are upper edges of 1 ms histogram bins.
#
# The timeline is 60.75 MHz and one field is timelineHz/60 ticks. The field period is derived from
# the recorded timeline, so a different timeline rate cannot silently make this check meaningless.
# `timeline-hz=` is published by [input-hid-cadence-audit] on the shutdown line.
$timelineHz = Get-Number $stderrText 'timeline-hz'
if ($null -eq $timelineHz -or $timelineHz -le 0) { $timelineHz = 60750000.0 }
$fieldUs = $timelineHz / 60.0 / ($timelineHz / 1000000.0)   # ticks per field / ticks per us
$fracDist = @($prodMax | Where-Object { $_ -gt 0 } | ForEach-Object {
    $n = $_ / $fieldUs
    [Math]::Abs($n - [Math]::Round($n))
})
$nearWhole = @($fracDist | Where-Object { $_ -lt 0.05 }).Count
$medianFrac = Get-Percentile $fracDist 0.5
$result.tailWindows = $tailLines.Count
$result.newFrameIntervalsTotal = if ($intervals.Count) { [int]((($intervals | Measure-Object -Sum).Sum)) } else { 0 }
$result.productionMeanUs = [ordered]@{
    median = Round-OrNull (Get-Percentile $prodMean 0.5) 1
    p90    = Round-OrNull (Get-Percentile $prodMean 0.9) 1
    min    = Round-OrNull (($prodMean | Measure-Object -Minimum).Minimum) 1
    max    = Round-OrNull (($prodMean | Measure-Object -Maximum).Maximum) 1
}
$result.productionP95UpperUsMedian = Round-OrNull (Get-Percentile $prodP95 0.5) 1
$result.productionP99WorstUs = Round-OrNull (($prodP99 | Measure-Object -Maximum).Maximum) 1
$result.productionMaxWorstUs = Round-OrNull (($prodMax | Measure-Object -Maximum).Maximum) 1

# ---- stalls and waits ---------------------------------------------------
$psoLines = @($stderrLines | Where-Object { $_ -match '^\[gx-pso-miss\]' })
$psoTotal = @(Get-AllNumbers -Lines $psoLines -Name 'total-us')
$psoBlob = @(Get-AllNumbers -Lines $psoLines -Name 'blob-us')
$psoDriver = @(Get-AllNumbers -Lines $psoLines -Name 'pso-us')
$blobHits = @($psoLines | Where-Object { $_ -match 'blob-cache-hit=1' })
$result.psoMisses = [ordered]@{
    count        = $psoLines.Count
    blobCacheHit = $blobHits.Count
    totalUsSum   = Round-OrNull (($psoTotal | Measure-Object -Sum).Sum) 0
    meanTotalUs  = Round-OrNull (($psoTotal | Measure-Object -Average).Average) 0
    maxTotalUs   = Round-OrNull (($psoTotal | Measure-Object -Maximum).Maximum) 0
    meanBlobUs   = Round-OrNull (($psoBlob | Measure-Object -Average).Average) 0
    meanDriverUs = Round-OrNull (($psoDriver | Measure-Object -Average).Average) 0
}

$queueWait = @(Get-AllNumbers -Lines $stderrLines -Name 'wait-us')
$queueLines = @($stderrLines | Where-Object { $_ -match '^\[gx-queue-wait\]' })
$result.gxQueueWait = [ordered]@{
    samples = $queueLines.Count
    maxUs   = if ($queueWait.Count) { Round-OrNull (($queueWait | Measure-Object -Maximum).Maximum) 0 } else { 'none-observed' }
}

# [gx-frame-timing] prints for two reasons: a frame crossed the stall threshold (selected=1), or it
# is the 1-in-N frame chosen by GALAXY_GX_FRAME_TIMING_SAMPLE (sample=1). A default run prints only
# selected frames, so statistics over them describe the worst frames. The two populations are kept apart.
# The render path's own timing. `frame-ms` is the renderer's per-frame work. If it sits at one field
# (16.67 ms) while new-serial-hz is far below 60, the renderer is not the limiter, and replay-pct gives
# the share of time spent re-showing an old frame.
$presentLines   = @($stderrLines | Where-Object { $_ -match '^\[present-stats\]' })
# Splits on the VALUE, not the digits: a pattern like `6[0-9]` misclassifies 59.26 as slow.
# The threshold is this session's own median `new-serial-hz`, not a fixed 55 Hz. A fixed threshold
# calibrated on another machine put almost every gameplay window in 'slow' on the Arc, while the
# median puts half the session on each side. Same rule as the per-thread regime split below.
$presentHz = @($presentLines | ForEach-Object { Get-Number $_ 'new-serial-hz' } | Where-Object { $null -ne $_ } | Sort-Object)
$presentThresholdHz = if ($presentHz.Count -ge 2) {
    $presentHz[[int][Math]::Floor($presentHz.Count / 2)]
} else { 55.0 }
$presentFast    = @($presentLines | Where-Object { $n = Get-Number $_ 'new-serial-hz'; $null -ne $n -and $n -ge $presentThresholdHz })
$presentSlow    = @($presentLines | Where-Object { $n = Get-Number $_ 'new-serial-hz'; $null -ne $n -and $n -lt $presentThresholdHz })
$frameTimingLines = @($stderrLines | Where-Object { $_ -match '^\[gx-frame-timing\]' })
$renderTotalAll   = @(Get-AllNumbers -Lines $frameTimingLines -Name 'total-us')
$renderParseAll   = @(Get-AllNumbers -Lines $frameTimingLines -Name 'parse-us')
$renderPsoAll     = @(Get-AllNumbers -Lines $frameTimingLines -Name 'flush-pso-us')
$sampledLines     = @($frameTimingLines | Where-Object { $_ -match 'sample=1' })
$selectedLines    = @($frameTimingLines | Where-Object { $_ -match 'selected=1' })
$unselectedLines  = @($frameTimingLines | Where-Object { $_ -match 'selected=0' })
$markerPresent    = ($sampledLines.Count + $selectedLines.Count + $unselectedLines.Count) -gt 0
$renderTotalSampled = @(Get-AllNumbers -Lines $sampledLines -Name 'total-us')
$renderTotalUnsel   = @(Get-AllNumbers -Lines $unselectedLines -Name 'total-us')
# Attribution. `flush-pso-us` dominates the worst frame, but a per-call figure needs `flush-calls`,
# and the resolution share needs `pso-resolutions`. Reports unavailable when these are missing.
$flushPsoPerCall = Get-PairedRatio -Lines $frameTimingLines -Numerator 'flush-pso-us' -Denominator 'flush-calls'
$flushPsoPerCallSampled = Get-PairedRatio -Lines $sampledLines -Numerator 'flush-pso-us' -Denominator 'flush-calls'
$resolutionsPerFlush = Get-PairedRatio -Lines $frameTimingLines -Numerator 'pso-resolutions' -Denominator 'flush-calls'
# draws per flush call: how many guest draws one acquisition actually covers.
# This is the number that says whether batching is already doing its job.
$drawsPerFlush = Get-PairedRatio -Lines $frameTimingLines -Numerator 'draws' -Denominator 'flush-calls'
# Per-call cost, valid on an uninstrumented frame. `flush-mean-us` comes from ScopedFlushTiming, which
# times one call in kFlushTimingSampleStride (64). The `*_us` fields inside flush_draw_state need
# per-draw instrumentation and are inflated on frames that carry it. Includes early-out calls.
$flushMeanAll     = @(Get-AllNumbers -Lines $frameTimingLines -Name 'flush-mean-us')
$flushMeanSampled = @(Get-AllNumbers -Lines $sampledLines  -Name 'flush-mean-us')
# Body-path mean. Early-out calls are in the denominator but add nothing to the numerator.
$bodyPathPerCall = Get-PairedRatio3 -Lines $frameTimingLines `
    -Numerator 'flush-us' -Denominator 'flush-calls' -Exclude 'pso-route-early'
# producer-wait-us is the simulation thread's wait on queue_space_cv_ against GALAXY_RENDER_QUEUE_DEPTH,
# its only hard coupling to the renderer. Trusted only when the population marker exists; earlier builds
# reported it as absent or as epoch-sized values.
$producerWaitSampled = @(Get-AllNumbers -Lines $sampledLines -Name 'producer-wait-us')
$producerWaitAll     = @(Get-AllNumbers -Lines $frameTimingLines -Name 'producer-wait-us')
# Values above one second measure the clock epoch, not a wait. They are detected and not reported.
$producerWaitGarbage = @($producerWaitAll | Where-Object { $_ -gt 1000000 }).Count -gt 0
$result.presentSplit = [ordered]@{
    windows      = $presentLines.Count
    fastWindows  = $presentFast.Count
    slowWindows  = $presentSlow.Count
    note         = 'frame-ms is the renderer per-frame work; replay% is the share of presents showing a frame the guest had not re-produced. High replay% with frame-ms at one field means the guest, not the renderer, set the rate.'
    frameMsFast  = Round-OrNull (Get-Percentile @(Get-AllNumbers -Lines $presentFast -Name 'frame-ms(avg/max)') 0.5) 2
    frameMsSlow  = Round-OrNull (Get-Percentile @(Get-AllNumbers -Lines $presentSlow -Name 'frame-ms(avg/max)') 0.5) 2
    replayPctFast = Round-OrNull (Get-Percentile @(Get-AllNumbers -Lines $presentFast -Name 'same-serial-or-replay-pct') 0.5) 2
    replayPctSlow = Round-OrNull (Get-Percentile @(Get-AllNumbers -Lines $presentSlow -Name 'same-serial-or-replay-pct') 0.5) 2
    xfbTracksNewHz = Round-OrNull (Get-Percentile @(
        for ($i = 0; $i -lt $presentLines.Count; $i++) {
            $a = Get-Number $presentLines[$i] 'new-serial-hz'
            $b = Get-Number $presentLines[$i] 'xfb-production-hz'
            if ($null -ne $a -and $null -ne $b) { [Math]::Abs($a - $b) }
        }) 0.95) 3
    latencyWaitMsMax = Round-OrNull (($(Get-AllNumbers -Lines $presentLines -Name 'latency-wait-ms(avg/max)') | Measure-Object -Maximum).Maximum) 4
    paceWaitMsMax    = Round-OrNull (($(Get-AllNumbers -Lines $presentLines -Name 'pace-wait-ms(avg/max)') | Measure-Object -Maximum).Maximum) 4
    presentMsMax     = Round-OrNull (($(Get-AllNumbers -Lines $presentLines -Name 'present-ms(avg/max)') | Measure-Object -Maximum).Maximum) 3
}

$result.renderFrameTiming = [ordered]@{
    lines                = $frameTimingLines.Count
    populationMarker     = if ($markerPresent) { 'selected=/sample= present' } else { 'ABSENT (pre-marker build)' }
    selectedLines        = if ($markerPresent) { $selectedLines.Count } else { 'unavailable' }
    sampledLines         = if ($markerPresent) { $sampledLines.Count } else { 'unavailable' }
    unbiasedLines        = if ($markerPresent) { $unselectedLines.Count } else { 'unavailable' }
    totalUsMedianAll     = Round-OrNull (Get-Percentile $renderTotalAll 0.5) 0
    totalUsMedianSampled = Round-OrNull (Get-Percentile $renderTotalSampled 0.5) 0
    parseUsMedianAll     = Round-OrNull (Get-Percentile $renderParseAll 0.5) 0
    flushPsoUsMedianAll  = Round-OrNull (Get-Percentile $renderPsoAll 0.5) 0
    # Per-call attribution, paired within each frame. Reports `unavailable` when the build predates the
    # flush-calls counter.
    flushPsoUsPerCallMedian = if ($flushPsoPerCall.ratios.Count) { Round-OrNull (Get-Percentile $flushPsoPerCall.ratios 0.5) 1 } else { 'unavailable' }
    flushPsoUsPerCallP95    = if ($flushPsoPerCall.ratios.Count) { Round-OrNull (Get-Percentile $flushPsoPerCall.ratios 0.95) 1 } else { 'unavailable' }
    flushPsoUsPerCallMax    = if ($flushPsoPerCall.ratios.Count) { Round-OrNull (($flushPsoPerCall.ratios | Measure-Object -Maximum).Maximum) 1 } else { 'unavailable' }
    flushPsoUsPerCallSampledMedian = if ($flushPsoPerCallSampled.ratios.Count) { Round-OrNull (Get-Percentile $flushPsoPerCallSampled.ratios 0.5) 1 } else { 'unavailable' }
    flushPsoUsPerCallSkipped = $flushPsoPerCall.skipped
    # Sampled per-call flush cost, valid on a routine frame. Compare this across builds;
    # `flushPsoUsPerCallMedian` above is body-path only.
    flushMeanUsMedianAll     = if ($flushMeanAll.Count)     { Round-OrNull (Get-Percentile $flushMeanAll 0.5) 1 }     else { 'unavailable' }
    flushMeanUsMedianSampled = if ($flushMeanSampled.Count) { Round-OrNull (Get-Percentile $flushMeanSampled 0.5) 1 } else { 'unavailable' }
    flushMeanUsP95Sampled    = if ($flushMeanSampled.Count) { Round-OrNull (Get-Percentile $flushMeanSampled 0.95) 1 } else { 'unavailable' }
    flushBodyPathPerCallMedian = if ($bodyPathPerCall.ratios.Count) { Round-OrNull (Get-Percentile $bodyPathPerCall.ratios 0.5) 1 } else { 'unavailable' }
    resolutionsPerFlushMedian = if ($resolutionsPerFlush.ratios.Count) { Round-OrNull (Get-Percentile $resolutionsPerFlush.ratios 0.5) 2 } else { 'unavailable' }
    drawsPerFlushMedian       = if ($drawsPerFlush.ratios.Count) { Round-OrNull (Get-Percentile $drawsPerFlush.ratios 0.5) 1 } else { 'unavailable' }
    producerWaitUsMedianSampled = if ($markerPresent) { Round-OrNull (Get-Percentile $producerWaitSampled 0.5) 0 } else { 'unavailable' }
    producerWaitUsMaxSampled    = if ($markerPresent -and $producerWaitSampled.Count) { Round-OrNull (($producerWaitSampled | Measure-Object -Maximum).Maximum) 0 } else { 'unavailable' }
    producerWaitUsLooksGarbage  = $producerWaitGarbage
    warning              = if ($producerWaitGarbage) {
        'producer-wait-us contains values above 1e6 us: this build predates the wait_start predicate fix and the field is garbage on sampled frames'
    } elseif ($flushMeanAll.Count -eq 0) {
        'flush-mean-us is absent from this build, so there is no per-call flush cost at all (not even the sampled one, which is the only variant valid on an uninstrumented frame)'
    } elseif ($flushPsoPerCall.ratios.Count -eq 0) {
        'flush-calls is absent from this build, so flush-pso-us cannot be divided into a per-call cost; the denominator is flush-calls (NOT draw-batches, which counts submitted host draws)'
    } elseif (-not $markerPresent) {
        'pre-marker build: cannot tell a stall-selected frame from an unbiased sample, so render-thread timings describe the worst frames only'
    } elseif ($sampledLines.Count -eq 0) {
        'no sample=1 lines: GALAXY_GX_FRAME_TIMING_SAMPLE is off or below 2, so producer-wait-us is only present on traced frames'
    } elseif ($unselectedLines.Count -eq 0) {
        'every line is a threshold-selected stall (sample=0, selected=1): these timings describe the worst frames, NOT the session'
    } else { $null }
}

# Guest-observable lateness, in real time: the runtime timeline runs at 60.75 MHz.
$viLine = @($stderrLines | Where-Object { $_ -match '^\[vi-deadline-audit\]' })
$decLine = @($stderrLines | Where-Object { $_ -match '^\[decrementer-deadline-audit\]' })
$toMs = { param($ticks) if ($null -eq $ticks) { $null } else { Round-OrNull ($ticks / 60750000.0 * 1000.0) 1 } }
$result.viDeadlineAudit = if ($viLine.Count) { [ordered]@{
    maxLatenessMs      = & $toMs (Get-Number $viLine[-1] 'max-lateness-ticks')
    missedOrCoalesced  = Get-Number $viLine[-1] 'missed-or-coalesced'
    scheduled          = Get-Number $viLine[-1] 'scheduled'
} } else { 'unavailable' }
$result.decrementerDeadlineAudit = if ($decLine.Count) { [ordered]@{
    maxLatenessMs = & $toMs (Get-Number $decLine[-1] 'max-lateness-ticks')
    dispatches    = Get-Number $decLine[-1] 'dispatches'
} } else { 'unavailable' }

# Native THP decode, recorded explicitly because a session may not exercise movie playback.
# (Array concatenation is built up rather than written as `pipeline + @(..)`: PowerShell parses a
# bare `+` after a pipeline as a command name.)
$thpLine = @($stderrLines | Where-Object { $_ -match '^\[thp-boundary-summary\]' })
if ($stdoutText) {
    $thpLine += @(($stdoutText -split "`r?`n") | Where-Object { $_ -match '^\[thp-boundary-summary\]' })
}
$result.nativeThpDecode = if ($thpLine.Count) { [ordered]@{
    attempts       = Get-Number $thpLine[-1] 'attempts'
    nativeDecodes  = Get-Number $thpLine[-1] 'native-decodes'
    classified     = Get-Number $thpLine[-1] 'classified'
} } else { 'unavailable' }

# ---- GPU: measured or explicitly not --------------------------------
$gpuSamples = @(Get-AllNumbers -Lines $presentLines -Name 'gpu-samples')
$gpuMs = @(Get-AllNumbers -Lines $presentLines -Name 'gpu-total-ms')
$gpuTiming = 'unavailable'
if ($presentLines.Count) {
    $m = [regex]::Match($presentLines[-1], 'gpu-timing=(\S+)')
    if ($m.Success) { $gpuTiming = $m.Groups[1].Value }
}
$result.gpu = [ordered]@{
    timingState   = $gpuTiming
    samplesMax    = if ($gpuSamples.Count) { [int]((($gpuSamples | Measure-Object -Maximum).Maximum)) } else { 0 }
    totalMsMax    = Round-OrNull (($gpuMs | Measure-Object -Maximum).Maximum) 3
    note          = 'gpu-samples=0 means the GPU was NOT measured, not that it was idle'
}

# ---- routine per-thread CPU, when the harness sampler was active --------
if ($threadsText) {
    $rows = @($threadsText | ConvertFrom-Csv)
    if ($rows.Count -gt 0) {
        $perThread = @{}
        foreach ($row in $rows) {
            $id = $row.threadId
            if (-not $perThread.ContainsKey($id)) {
                $perThread[$id] = [pscustomobject]@{ threadId = $id; first = [double]$row.cpu100ns; last = [double]$row.cpu100ns; lastElapsed = [double]$row.elapsedSeconds }
            } else {
                $perThread[$id].last = [double]$row.cpu100ns
                $perThread[$id].lastElapsed = [double]$row.elapsedSeconds
            }
        }
        $summary = $perThread.Values | ForEach-Object {
            $cpuSeconds = ($_.last - $_.first) / 1e7
            [pscustomobject]@{
                threadId = [int]$_.threadId
                cpuSeconds = [Math]::Round($cpuSeconds, 3)
                coresAverage = if ($_.lastElapsed -gt 0) { [Math]::Round($cpuSeconds / $_.lastElapsed, 3) } else { 0 }
            }
        } | Sort-Object cpuSeconds -Descending
        # Session totals show the largest CPU consumer overall, not whose CPU rises when the frame rate falls.
        # A thread can be the largest consumer and flat across regimes. Each thread is split by the frame-rate
        # population of the window it was sampled in.
        #
        # Window k of [present-stats] covers roughly [2k, 2k+2) seconds, the same clock threads.csv uses for
        # elapsedSeconds.
        $hzBySecond = @{}
        for ($i = 0; $i -lt $presentLines.Count; $i++) {
            $h = Get-Number $presentLines[$i] 'new-serial-hz'
            if ($null -ne $h) { $hzBySecond[$i] = $h }
        }
        # The split threshold is the session's own median frame rate, $presentThresholdHz, not a fixed 55 Hz.
        # A fixed threshold put only the startup windows (nearly empty scene, one field per frame) in 'fast'
        # on the Arc, so the regime delta would contrast startup against gameplay. The median separates the
        # session's faster and slower halves. On the reference machine it lands near 57 Hz.
        #
        # Shared with the present-stats split above so the two cannot disagree.
        $regimeThresholdHz = $presentThresholdHz
        $threadCpuByPop = @{}
        $threadPrev = @{}
        foreach ($row in $rows) {
            $id = $row.threadId
            $sec = [int][Math]::Floor([double]$row.elapsedSeconds)
            $cpu = [double]$row.cpu100ns
            # The row at elapsed `e` covers the interval [e-1, e], since the sampler writes it at the end of each
            # 1 s poll. Classify by the window containing the interval's midpoint: elapsed 1 and 2 both belong to
            # window 0. Using floor(e/2) misassigns the second sample of each window.
            $win = if ($sec -ge 1) { [int][Math]::Floor(($sec - 1) / 2) } else { 0 }
            $pop = if ($hzBySecond.ContainsKey($win)) {
                if ($hzBySecond[$win] -ge $regimeThresholdHz) { 'fast' } else { 'slow' }
            } else { $null }
            if ($pop -ne $null -and $threadPrev.ContainsKey($id)) {
                $d = $cpu - $threadPrev[$id]
                if ($d -ge 0) {
                    if (-not $threadCpuByPop.ContainsKey($id)) {
                        $threadCpuByPop[$id] = @{ fast = 0.0; slow = 0.0; fastSec = 0.0; slowSec = 0.0 }
                    }
                    $threadCpuByPop[$id][$pop] += $d
                    # Accumulates the elapsed time each delta covers. The first sample of each population is dropped
                    # because its delta spans the regime boundary. Dividing by a sample count would charge each population
                    # for time it did not measure.
                    if ($pop -eq 'fast') { $threadCpuByPop[$id].fastSec++ } else { $threadCpuByPop[$id].slowSec++ }
                }
            }
            $threadPrev[$id] = $cpu
        }
        $byRegime = @($threadCpuByPop.Keys | ForEach-Object {
            $e = $threadCpuByPop[$_]
            [pscustomobject]@{
                threadId = [int]$_
                fastCpuSeconds = [Math]::Round($e.fast / 1e7, 3)
                slowCpuSeconds = [Math]::Round($e.slow / 1e7, 3)
                # CPU per second of wall time within each population. Populations differ in duration, so this is the
                # comparable figure.
                fastCores = if ($e.fastSec -gt 0) { [Math]::Round(($e.fast / 1e7) / $e.fastSec, 3) } else { 0 }
                slowCores = if ($e.slowSec -gt 0) { [Math]::Round(($e.slow / 1e7) / $e.slowSec, 3) } else { 0 }
            }
        } | Sort-Object -Property @{ Expression = { $_.slowCores - $_.fastCores }; Descending = $true })
        $result.perThreadCpu = [ordered]@{
            sampled = $true
            windowSeconds = Round-OrNull ([double]($rows[-1].elapsedSeconds)) 1
            # Reported so the split is visible: this is the session's own median frame rate, not a constant.
            # See the note at the threshold computation.
            regimeThresholdHz = [Math]::Round($regimeThresholdHz, 2)
            topThreads = @($summary | Select-Object -First 6)
            # Sorted by how much each thread's CPU per wall second RISES in the slow
            # regime, which is the attribution the regime change needs.
            perThreadByRegime = @($byRegime | Select-Object -First 6)
        }
    }
} else {
    $result.perThreadCpu = [ordered]@{ sampled = $false; note = 'no threads.csv; start the run with tools/perf_session.ps1' }
}

if ($processText) {
    $proc = @($processText | ConvertFrom-Csv)
    if ($proc.Count -gt 1) {
        $first = $proc[0]; $last = $proc[-1]
        $cpu = [double]$last.cpuSeconds - [double]$first.cpuSeconds
        $elapsed = [double]$last.elapsedSeconds - [double]$first.elapsedSeconds
        $result.processCores = if ($elapsed -gt 0) { Round-OrNull ($cpu / $elapsed) } else { $null }
        $result.workingSetPeakMb = Round-OrNull (([double]($proc | ForEach-Object { [double]$_.workingSetBytes } | Measure-Object -Maximum).Maximum) / 1MB) 0
    }
}

# Guest progress rate, from checkpoint_count, which the generated guest code increments at every
# checkpoint. Its rate is how fast the guest advanced, independent of host timing models. A machine
# that advances slowly while using no more CPU is blocked, not CPU-bound.
# Absolute rate only: do not divide by a `vi=` field, which is slot-relative in [input-anomaly] and
# absent from some builds.
$maxCheckpoint = 0L
foreach ($line in $stderrLines) {
    foreach ($m in [regex]::Matches($line, '(?<![-\w])checkpoint=(\d+)')) {
        $v = [long]$m.Groups[1].Value
        if ($v -gt $maxCheckpoint) { $maxCheckpoint = $v }
    }
}
$result.guestProgress = if ($maxCheckpoint -gt 0 -and $elapsed -gt 0) {
    $cpuSeconds = if ($null -ne $result.processCores) { $result.processCores * $elapsed } else { $null }
    [ordered]@{
        checkpointsTotal = $maxCheckpoint
        perSecond        = [Math]::Round($maxCheckpoint / $elapsed, 0)
        overSeconds      = [Math]::Round($elapsed, 1)
        cpuSeconds       = if ($null -ne $cpuSeconds) { [Math]::Round($cpuSeconds, 1) } else { 'unavailable' }
        # CPU seconds per completed guest checkpoint. A clock-free measure that separates expensive from blocked:
        # a waiting thread uses no CPU, so blocking lowers this number. Cross-machine comparison needs no
        # assumption about clock speed.
        nsCpuPerCheckpoint = if ($null -ne $cpuSeconds) {
                                 [Math]::Round($cpuSeconds * 1e9 / $maxCheckpoint, 1)
                             } else { 'unavailable' }
        note             = 'guest checkpoints executed per wall second, and CPU-seconds spent per checkpoint; blocking lowers the second, expensive execution raises it'
    }
} else { 'unavailable (no checkpoint= field or no process.csv)' }

# Each EFB peek drains the pending GX FIFO and round-trips through the frame queue, blocking the
# simulation thread on the render thread twice. The two halves are reported separately because they are
# different dependencies. Absent on builds before this instrumentation.
$peekLine = @($stderrLines | Where-Object { $_ -match '^\[gx-efb-peek-summary\]' })
$result.efbPeek = if ($peekLine.Count) {
    $p = $peekLine[-1]
    [ordered]@{
        count        = Get-Number $p 'count'
        failures     = Get-Number $p 'failures'
        blockedTotalUs = Get-Number $p 'blocked-total-us'
        blockedMaxUs = Get-Number $p 'blocked-max-us'
        blockedMeanUs = Get-Number $p 'blocked-mean-us'
        fifoSyncTotalUs = Get-Number $p 'fifo-sync-total-us'
        roundTripTotalUs = Get-Number $p 'round-trip-total-us'
    }
} else { 'unavailable (pre-instrumentation build)' }

# The guest reaches a pending VI boundary and the host advances until IRQ24 is delivered. `advances` far
# above `boundaries` means many re-entries. `await-dispatch-noop` counts re-entries that reported progress
# while delivering nothing, which happens by construction while an external dispatch is active. With the
# [vi-deadline-audit] line (scheduled == delivered), this separates "the guest never got its interrupt"
# from "the guest got it and still could not finish a frame".
$viBoundaryLine = @($stderrLines | Where-Object { $_ -match '^\[pending-vi-boundary-audit\]' })
$viDeadlineLine = @($stderrLines | Where-Object { $_ -match '^\[vi-deadline-audit\]' })
if ($viBoundaryLine.Count) {
    $vb = $viBoundaryLine[-1]
    $advances  = Get-Number $vb 'advances'
    $boundaries = Get-Number $vb 'boundaries'
    $returned  = Get-Number $vb 'await-dispatch-returned'
    $delivered = Get-Number $vb 'await-dispatch-delivered'
    $noop      = Get-Number $vb 'await-dispatch-noop'
    $result.viBoundary = [ordered]@{
        advances        = $advances
        boundaries      = $boundaries
        advancesPerBoundary = if ($boundaries -and $boundaries -gt 0) { [Math]::Round($advances / $boundaries, 1) } else { 'unavailable' }
        awaitExternalActive = Get-Number $vb 'await-external-active'
        awaitEeMasked   = Get-Number $vb 'await-ee-masked'
        awaitEligible   = Get-Number $vb 'await-dispatch-eligible'
        awaitReturned   = $returned
        awaitDelivered  = $delivered
        awaitNoop       = $noop
        noopShare       = if ($null -ne $returned -and $returned -gt 0 -and $null -ne $noop) {
                              [Math]::Round($noop / $returned, 3)
                          } else { 'unavailable' }
        note            = 'awaitNoop advances reported progress without delivering IRQ24; when external-dispatch-active the callee cannot deliver, by construction'
    }
    $result.viBoundaryDelivered = if ($viDeadlineLine.Count) {
        $vd = $viDeadlineLine[-1]
        [ordered]@{
            scheduled = Get-Number $vd 'scheduled'
            delivered = Get-Number $vd 'delivered'
            deliveredInterrupts = Get-Number $vd 'delivered-interrupts'
            missedOrCoalesced = Get-Number $vd 'missed-or-coalesced'
            maxBacklog = Get-Number $vd 'max-backlog'
            maxLatenessTicks = Get-Number $vd 'max-lateness-ticks'
            punctual = ((Get-Number $vd 'scheduled') -eq (Get-Number $vd 'delivered'))
        }
    } else { 'unavailable' }
} else {
    $result.viBoundary = 'unavailable (pre-instrumentation build)'
    $result.viBoundaryDelivered = 'unavailable'
}

# Lateness fields in two categories that must not be mixed. ADDITIVE fields are totals of blocked time and
# may be summed to bound what a stall fix could recover. NON-additive fields are maxima or per-event
# latencies that overlap, so summing them gives a meaningless total. `selected-to-store-total-us`
# accumulates a latency per pointer store, so it is not blocked time.
$TICKS_PER_SECOND = 60750000.0
function Sum-StallField([string]$Tag, [string]$Field, [double]$Divisor) {
    $ls = @($stderrLines | Where-Object { $_ -match ('^\[' + $Tag + '\]') })
    if (-not $ls.Count) { return $null }
    $vals = @()
    foreach ($l in $ls) {
        $m = [regex]::Match($l, '(?:^|\s)' + [regex]::Escape($Field) + '=([0-9.eE+-]+)')
        if ($m.Success) { $vals += [double]$m.Groups[1].Value }
    }
    if (-not $vals.Count) { return $null }
    $sum = ($vals | Measure-Object -Sum).Sum
    [pscustomobject]@{ tag = $Tag; field = $Field; n = $vals.Count; seconds = ($sum / $Divisor) }
}
$additive = @()
$additive += Sum-StallField 'gx-pso-miss'              'total-us'          1e6
$additive += Sum-StallField 'ai-dma-deadline-audit'    'late-total-ticks'  $TICKS_PER_SECOND
$additive += Sum-StallField 'host-suspension-audit'    'recovered-ticks'   $TICKS_PER_SECOND
$additive += Sum-StallField 'frame-pe-wait-audit'      'total-us'          1e6
$additive += Sum-StallField 'gx-queue-wait'            'wait-us'           1e6
$additive += Sum-StallField 'mouse-pointer-summary'    'field-wait-total-us' 1e6
$additive += Sum-StallField 'decrementer-deadline-audit' 'late-total-ticks' $TICKS_PER_SECOND
$additive = @($additive | Where-Object { $_ -ne $null })
$stallTotal = 0.0
foreach ($a in $additive) { $stallTotal += $a.seconds }
$nonAdditive = @()
foreach ($p in @(@('vi-deadline-audit','max-lateness-ticks',$TICKS_PER_SECOND),
                 @('decrementer-deadline-audit','max-lateness-ticks',$TICKS_PER_SECOND),
                 @('mouse-pointer-summary','selected-to-store-total-us',1e6))) {
    $r = Sum-StallField $p[0] $p[1] $p[2]
    if ($r) { $nonAdditive += $r }
}
$wall = if ($elapsed -gt 0) { $elapsed } else { 0 }
$result.stallBudget = [ordered]@{
    wallSeconds        = if ($wall -gt 0) { [Math]::Round($wall, 1) } else { 'unavailable' }
    additive           = @($additive | ForEach-Object {
                             [ordered]@{ source = $_.tag; field = $_.field; events = $_.n; seconds = [Math]::Round($_.seconds, 3) } })
    additiveTotalSeconds = [Math]::Round($stallTotal, 3)
    additiveShareOfWall  = if ($wall -gt 0) { [Math]::Round($stallTotal / $wall, 4) } else { 'unavailable' }
    nonAdditiveNotSummed = @($nonAdditive | ForEach-Object {
                             [ordered]@{ source = $_.tag; field = $_.field; events = $_.n; seconds = [Math]::Round($_.seconds, 3) } })
    note               = 'additiveTotalSeconds bounds what ALL reported stall fixes could recover; nonAdditiveNotSummed are maxima or overlapping per-event latencies and must never be added in'
}

# Converts the clock-free nsCpuPerCheckpoint into CYCLES per checkpoint, separating "slower per clock" from
# "more work per unit". The clock is a median over the run, so a boost spike cannot pass as the sustained
# state. The clock is measured, not assumed: a laptop under sustained load can run well below its rated clock.
$clockCsv = Join-Path $sessionPath 'clock.csv'
$result.hostClock = 'unavailable (no clock.csv; start the run with tools/perf_session.ps1)'
if (Test-Path -LiteralPath $clockCsv) {
    try {
        $cr = @(Import-Csv -LiteralPath $clockCsv | Where-Object { $_.currentClockMhz -match '^\d+$' })
        $vals = @($cr | ForEach-Object { [double]$_.currentClockMhz } | Where-Object { $_ -gt 0 })
        $loads = @($cr | ForEach-Object { $_.loadPercent } | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [double]$_ })
        if ($vals.Count -ge 2) {
            $sorted = @($vals | Sort-Object)
            $result.hostClock = [ordered]@{
                samples       = $vals.Count
                medianMhz     = [Math]::Round($sorted[[int][Math]::Floor($sorted.Count / 2)], 0)
                minMhz        = [Math]::Round(($vals | Measure-Object -Minimum).Minimum, 0)
                maxMhz        = [Math]::Round(($vals | Measure-Object -Maximum).Maximum, 0)
                medianLoadPct = if ($loads.Count) { [Math]::Round(($loads | Measure-Object -Average).Average, 0) } else { 'unavailable' }
            }
            # Coerces rather than type-tests: the runtime type depends on how Round-OrNull produced the value, and a
            # strict `-is [double]` guard would skip this computation.
            try {
                $nsPerCk = [double]$result.guestProgress.nsCpuPerCheckpoint
                if ($nsPerCk -gt 0 -and $result.hostClock.medianMhz -gt 0) {
                    $result.hostClock.cyclesPerCheckpoint =
                        [Math]::Round($nsPerCk * $result.hostClock.medianMhz / 1000.0, 0)
                }
            } catch { }
        }
    } catch {
        $result.hostClock = ('unreadable clock.csv: ' + $_.Exception.Message)
    }
}

# ---- the compact row ----------------------------------------------------
$row = [ordered]@{
    session        = Split-Path $sessionPath -Leaf
    commit         = $result.commit
    runtimeIsa     = $result.runtimeIsa
    moduleIsa      = $result.moduleIsa
    adapter        = $result.adapter
    output         = $result.output
    efbScale       = $result.efbScale
    newFrameHzMed  = $result.newFrameHz.median
    modesDetected  = $result.operatingModes.distinctRuns
    bimodal        = $result.operatingModes.bimodal
    prodMeanUsMed  = $result.productionMeanUs.median
    prodMaxWorstUs = $result.productionMaxWorstUs
    psoMisses      = $result.psoMisses.count
    cacheState     = $result.cacheCondition.verdict
    renderTotalMed = $result.renderFrameTiming.totalUsMedianAll
    renderUnbiased = $result.renderFrameTiming.unbiasedLines
    gpuState       = $result.gpu.timingState
    cores          = $result.processCores
    guestCkPerSec  = if ($result.guestProgress -is [System.Collections.IDictionary]) { $result.guestProgress.perSecond } else { 'unavailable' }
    nsCpuPerCk     = if ($result.guestProgress -is [System.Collections.IDictionary]) { $result.guestProgress.nsCpuPerCheckpoint } else { 'unavailable' }
    wsPeakMb       = $result.workingSetPeakMb
}
$result.compactRow = $row

# Emitted on request, one row per operating mode rather than per session, because one averaged row describes
# no scene in a multi-modal recording. Columns follow the objective's order. A cell that cannot be derived
# is written as 'unavailable'. 'scene' is the operating-mode time span, the only scene identity the logs carry.
if ($ResultsTable) {
    $hcName = if ($sessionObj -and $sessionObj.PSObject.Properties['hostCpu'] -and $sessionObj.hostCpu) {
        $h = $sessionObj.hostCpu
        $bits = @()
        if ($h.name) { $bits += $h.name }
        if ($h.maxClockMhz) { $bits += ('max ' + $h.maxClockMhz + 'MHz') }
        if ($h.currentClockMhz) { $bits += ('cur ' + $h.currentClockMhz + 'MHz') }
        if ($h.l3CacheKb) { $bits += ('L3 ' + $h.l3CacheKb + 'KB') }
        if ($h.memorySpeedMhz) { $bits += ('mem ' + $h.memorySpeedMhz + 'MHz') }
        ($bits -join ' ')
    } else { 'unrecorded' }
    $buildCell = ('{0} isa={1} modIsa={2} runtimeBuild={3} moduleKey={4}' -f `
        $row.commit, $row.runtimeIsa, $row.moduleIsa, `
        $(if ($result.runtimeBuildPresent) { 'present' } else { 'absent' }), $result.moduleKey)
    $hwCell = ('{0} | {1}' -f $row.adapter, $hcName)
    $renderCell = ('{0} aspect={1} efbScale={2}' -f $row.output, $result.aspect, $row.efbScale)
    $cacheCell = ('{0} pairs={1} keys={2} seedOffered={3}' -f `
        $result.cacheCondition.verdict, $result.cacheCondition.loadedPairs, `
        $result.cacheCondition.loadedKeys, $result.cacheCondition.seedOffered)
    $tailCell = ('session: prodMeanMed={0}us p90={1}us prodP95Med={2}us p99Worst={3}us maxWorst={4}us' -f `
        $result.productionMeanUs.median, $result.productionMeanUs.p90, `
        $result.productionP95UpperUsMedian, $result.productionP99WorstUs, $result.productionMaxWorstUs)
    $cpuGpuCell = ('session: cores={0} wsPeakMb={1} guestCkPerSec={2} nsCpuPerCk={3} gpu={4} gpuMsMax={5} psoMisses={6}' -f `
        $row.cores, $row.wsPeakMb, $row.guestCkPerSec, $row.nsCpuPerCk, $row.gpuState, $result.gpu.totalMsMax, $row.psoMisses)
    # Correctness: only what the recording can witness. The presence of the new-frame and presentation counters
    # is checked, so a repeated presentation cannot be counted as a new frame.
    $viPunctual = if ($result.viBoundaryDelivered -is [System.Collections.IDictionary]) {
        $result.viBoundaryDelivered.punctual } else { 'unavailable' }
    $distinctCounters = if ($result.newFrameHz.median -ne $null -and $result.xfbProductionHzMedian -ne $null) {
        'new-vs-present counters distinct' } else { 'check: counters not both present' }
    $viLatenessCell = if ($result.viDeadlineAudit -is [System.Collections.IDictionary]) {
        [string]$result.viDeadlineAudit.maxLatenessMs } else { 'unavailable' }
    $correctnessCell = ('cache={0}; viPunctual={1}; {2}; psoMisses={3}; viLatenessMs={4}' -f `
        $result.cacheCondition.verdict, $viPunctual, $distinctCounters, $row.psoMisses, $viLatenessCell)
    $retainedCell = if ($RetainedChange) { $RetainedChange } else { 'unspecified (pass -RetainedChange)' }

    $cols = @('build','hardware','scene','rendering','cacheCondition','newFrameRate','frameTails','cpuGpuCosts','correctness','retainedChange')
    Write-Output ($cols -join "`t")
    $modeRows = @($result.operatingModes.runs | Sort-Object windows -Descending)
    if ($modeRows.Count -eq 0) {
        Write-Output (@($buildCell,$hwCell,'single session (no modes detected)',$renderCell,$cacheCell,
            ($row.newFrameHzMed.ToString() + 'Hz median'),$tailCell,$cpuGpuCell,$correctnessCell,$retainedCell) -join "`t")
    } else {
        foreach ($m in $modeRows) {
            $sceneCell = ('t={0}..{1}s windows={2}' -f $m.fromSeconds, $m.toSeconds, $m.windows)
            $rateCell = ('{0}Hz over {1} windows' -f $m.meanHz, $m.windows)
            Write-Output (@($buildCell,$hwCell,$sceneCell,$renderCell,$cacheCell,$rateCell,$tailCell,$cpuGpuCell,$correctnessCell,$retainedCell) -join "`t")
        }
    }
    if ($Json) { }
    return
}

Write-Output ''
Write-Output "session  : $sessionPath"
Write-Output ("identity : commit={0} runtimeIsa={1} moduleIsa={2} runtimeBuild={3}" -f $row.commit, $row.runtimeIsa, $row.moduleIsa, $(if ($result.runtimeBuildPresent) { 'present' } else { 'ABSENT from logs' }))
Write-Output ("hardware : {0}" -f $row.adapter)
if ($sessionObj -and $sessionObj.PSObject.Properties['hostCpu'] -and $sessionObj.hostCpu -and $sessionObj.hostCpu.name) {
    $hc = $sessionObj.hostCpu
    Write-Output ("host CPU : {0}  cores={1}/{2}  clock max={3} MHz current={4} MHz  L3={5} KB  mem={6} MHz" -f `
        $hc.name, $hc.cores, $hc.logical, $hc.maxClockMhz, $hc.currentClockMhz, $hc.l3CacheKb, $hc.memorySpeedMhz)
} else {
    Write-Output "host CPU : unrecorded (pre-hostCpu harness) -- a cross-machine CPU-cost ratio cannot separate clock from work without it"
}
Write-Output ("render   : output={0} aspect={1} efbScale={2}" -f $row.output, $result.aspect, $row.efbScale)
Write-Output ("cache    : {0}" -f $result.pipelineCache)
Write-Output ("cache state  : {0}  (pairs={1} keys={2} seed-offered={3})" -f `
    $result.cacheCondition.verdict, $result.cacheCondition.loadedPairs, $result.cacheCondition.loadedKeys, $result.cacheCondition.seedOffered)
Write-Output ("               {0}" -f $result.cacheCondition.note)
Write-Output ''
Write-Output ("new frames   : median={0} Hz  p10={1}  p90={2}  min={3}  max={4}   over {5} windows ({6} intervals)" -f `
    $result.newFrameHz.median, $result.newFrameHz.p10, $result.newFrameHz.p90, $result.newFrameHz.min, $result.newFrameHz.max, `
    $result.presentStatsWindows, $result.newFrameIntervalsTotal)
$om = $result.operatingModes
Write-Output ("operating    : {0} sustained run(s) detected ({1})" -f $om.distinctRuns, $om.detection)
foreach ($r in @($om.runs | Sort-Object windows -Descending | Select-Object -First 6)) {
    Write-Output ("               t={0,6}..{1,-6}s  {2,6} Hz  over {3,3} windows" -f $r.fromSeconds, $r.toSeconds, $r.meanHz, $r.windows)
}
if ($om.note) { Write-Output ("               WARNING: {0}" -f $om.note) }
# The `-upper-us` fields are bucket edges, not percentiles, so they are labelled that way.
if ($prodMax.Count -gt 0) {
    Write-Output ("frame tails  : production-max-us exact; p95/p99-upper-us are 1 ms BUCKET EDGES (upper bounds), never point values")
    Write-Output ("field quantisation: {0} of {1} windows have max-us within 5% of a whole 60 Hz field; median fractional distance {2}" -f `
        $nearWhole, $prodMax.Count, $(if ($null -eq $medianFrac) { 'n/a' } else { [Math]::Round($medianFrac, 3) }))
    Write-Output ("               <- near-uniform fractional parts mean CONTINUOUS slowdown; clustering would mean frames slip whole vblanks")
}
Write-Output ("frame tails  : production-mean median={0} us  p90={1}  worst window mean={2} us" -f `
    $result.productionMeanUs.median, $result.productionMeanUs.p90, $result.productionMeanUs.max)
Write-Output ("               p95-upper median={0} us   p99 worst={1} us   max worst={2} us" -f `
    $result.productionP95UpperUsMedian, $result.productionP99WorstUs, $result.productionMaxWorstUs)
Write-Output ("pso misses   : count={0} blobCacheHits={1} totalSum={2} us mean={3} us max={4} us" -f `
    $result.psoMisses.count, $result.psoMisses.blobCacheHit, $result.psoMisses.totalUsSum, $result.psoMisses.meanTotalUs, $result.psoMisses.maxTotalUs)
if ($result.guestProgress -is [System.Collections.IDictionary]) {
    Write-Output ("guest progress: {0:N0} checkpoints/s over {1}s  (total {2:N0})" -f `
        $result.guestProgress.perSecond, $result.guestProgress.overSeconds, $result.guestProgress.checkpointsTotal)
    Write-Output ("               {0} ns CPU spent per completed checkpoint  <- CLOCK-FREE: blocking drives this DOWN (waiters consume no CPU), expensive execution drives it UP" -f `
        $result.guestProgress.nsCpuPerCheckpoint)
    if ($result.hostClock -is [System.Collections.IDictionary]) {
        Write-Output ("               sampled host clock: median={0} MHz (min {1}, max {2}) over {3} samples, load={4}%" -f `
            $result.hostClock.medianMhz, $result.hostClock.minMhz, $result.hostClock.maxMhz, $result.hostClock.samples, $result.hostClock.medianLoadPct)
        # OrderedDictionary keys are not exposed through PSObject.Properties, so the presence test goes through the
        # dictionary interface.
        if ($result.hostClock.Contains('cyclesPerCheckpoint')) {
            Write-Output ("               => {0} CYCLES per completed checkpoint at the SAMPLED clock (was estimated from an assumed 2.2 GHz for the Arc)" -f `
                $result.hostClock.cyclesPerCheckpoint)
        }
    } else {
        Write-Output ("               host clock: {0}" -f $result.hostClock)
    }
} else {
    Write-Output ("guest progress: {0}" -f $result.guestProgress)
}
Write-Output ("queue waits  : samples={0} max={1} us" -f $result.gxQueueWait.samples, $result.gxQueueWait.maxUs)
$ps = $result.presentSplit
if ($ps.windows -gt 0) {
    Write-Output ("present split: {0} windows ({1} at >=55 Hz, {2} below 55 Hz)" -f $ps.windows, $ps.fastWindows, $ps.slowWindows)
    Write-Output ("               frame-ms median: fast={0}  slow={1}   <- at one field (16.67) in both means the renderer kept up" -f $ps.frameMsFast, $ps.frameMsSlow)
    Write-Output ("               replay% median : fast={0}  slow={1}   <- presents showing a frame the guest had not re-produced" -f $ps.replayPctFast, $ps.replayPctSlow)
    Write-Output ("               |new-serial-hz - xfb-production-hz| p95 = {0}   <- ~0 means the guest's production rate IS the new-frame rate" -f $ps.xfbTracksNewHz)
    Write-Output ("               waits max: latency={0} ms  pace={1} ms  present={2} ms" -f $ps.latencyWaitMsMax, $ps.paceWaitMsMax, $ps.presentMsMax)
}

$rt = $result.renderFrameTiming
Write-Output ("render frames: lines={0}  ({1})" -f $rt.lines, $rt.populationMarker)
Write-Output ("               selected(stall)={0}  sampled={1}  unbiased={2}" -f $rt.selectedLines, $rt.sampledLines, $rt.unbiasedLines)
Write-Output ("               total-us median all={0}  sampled={1}  parse-us median={2}  flush-pso-us median={3}" -f `
    $rt.totalUsMedianAll, $(if ($null -eq $rt.totalUsMedianSampled) { 'none' } else { $rt.totalUsMedianSampled }), $rt.parseUsMedianAll, $rt.flushPsoUsMedianAll)
Write-Output ("               producer-wait-us (sim thread parked on the render queue): sampled median={0} max={1}" -f `
    $rt.producerWaitUsMedianSampled, $rt.producerWaitUsMaxSampled)
# Attribution for the next optimisation target. Always printed: when the denominator is missing from the build,
# `unavailable` and the warning below are the answer.
Write-Output ("               flush per call (sampled, includes early-out, valid uninstrumented): median={0} us  sampled-only median={1} us  p95={2} us" -f `
    $rt.flushMeanUsMedianAll, $rt.flushMeanUsMedianSampled, $rt.flushMeanUsP95Sampled)
Write-Output ("               flush body path per call = flush-us / (flush-calls - pso-route-early): median={0} us" -f `
    $rt.flushBodyPathPerCallMedian)
Write-Output ("               flush-pso per call: median={0} us  p95={1} us  max={2} us  (paired frames={3}, skipped={4})" -f `
    $rt.flushPsoUsPerCallMedian, $rt.flushPsoUsPerCallP95, $rt.flushPsoUsPerCallMax, `
    $(if ($rt.flushPsoUsPerCallMedian -eq 'unavailable') { 0 } else { $frameTimingLines.Count - $rt.flushPsoUsPerCallSkipped }), `
    $rt.flushPsoUsPerCallSkipped)
Write-Output ("               resolutions per flush call: median={0}   draws per flush call: median={1}" -f `
    $rt.resolutionsPerFlushMedian, $rt.drawsPerFlushMedian)
if ($rt.warning) { Write-Output ("               WARNING: {0}" -f $rt.warning) }
if ($result.viDeadlineAudit -ne 'unavailable') {
    Write-Output ("vi lateness  : max={0} ms  missed-or-coalesced={1} of {2}" -f $result.viDeadlineAudit.maxLatenessMs, $result.viDeadlineAudit.missedOrCoalesced, $result.viDeadlineAudit.scheduled)
}
Write-Output ("GPU          : gpu-timing={0} samples={1} totalMsMax={2}  <- {3}" -f $result.gpu.timingState, $result.gpu.samplesMax, $result.gpu.totalMsMax, $result.gpu.note)
$sb = $result.stallBudget
Write-Output ("stall budget : {0} s of {1} s wall = {2}%  (upper bound on what ALL reported stall fixes could recover)" -f `
    $sb.additiveTotalSeconds, $sb.wallSeconds, [Math]::Round(100 * $sb.additiveShareOfWall, 2))
foreach ($a in $sb.additive) {
    Write-Output ("               additive  {0,-26} {1,-20} n={2,-6} {3,9:N3} s" -f $a.source, $a.field, $a.events, $a.seconds)
}
foreach ($a in $sb.nonAdditiveNotSummed) {
    Write-Output ("               NOT added {0,-26} {1,-20} n={2,-6} {3,9:N3} s  <- max or overlapping; never sum" -f $a.source, $a.field, $a.events, $a.seconds)
}
if ($result.viBoundary -is [System.Collections.IDictionary]) {
    $vb = $result.viBoundary
    Write-Output ("VI boundary  : {0:N0} advances over {1:N0} boundaries = {2} per boundary" -f `
        $vb.advances, $vb.boundaries, $vb.advancesPerBoundary)
    Write-Output ("               await reasons: external-active={0:N0} ee-masked={1:N0} eligible={2:N0}" -f `
        $vb.awaitExternalActive, $vb.awaitEeMasked, $vb.awaitEligible)
    Write-Output ("               advances that delivered IRQ24={0:N0}  reported-progress-without-delivering={1:N0} ({2} of advances returned)" -f `
        $vb.awaitDelivered, $vb.awaitNoop, $vb.noopShare)
    if ($result.viBoundaryDelivered -is [System.Collections.IDictionary]) {
        $vd = $result.viBoundaryDelivered
        Write-Output ("               deadline audit: scheduled={0:N0} delivered={1:N0} punctual={2} missed-or-coalesced={3:N0} max-backlog={4:N0}" -f `
            $vd.scheduled, $vd.delivered, $vd.punctual, $vd.missedOrCoalesced, $vd.maxBacklog)
    }
} else {
    Write-Output ("VI boundary  : {0}" -f $result.viBoundary)
}

if ($result.efbPeek -is [System.Collections.IDictionary]) {    Write-Output ("EFB peeks    : count={0} failures={1} blocked total={2} us mean={3} us max={4} us" -f `
        $result.efbPeek.count, $result.efbPeek.failures, $result.efbPeek.blockedTotalUs, $result.efbPeek.blockedMeanUs, $result.efbPeek.blockedMaxUs)
    Write-Output ("               split: fifo-sync total={0} us   frame-round-trip total={1} us" -f `
        $result.efbPeek.fifoSyncTotalUs, $result.efbPeek.roundTripTotalUs)
} else {
    Write-Output ("EFB peeks    : {0}" -f $result.efbPeek)
}
if ($result.nativeThpDecode -ne 'unavailable') {
    Write-Output ("native THP   : attempts={0} native-decodes={1} classified={2}" -f $result.nativeThpDecode.attempts, $result.nativeThpDecode.nativeDecodes, $result.nativeThpDecode.classified)
}
if ($result.perThreadCpu.sampled) {
    Write-Output ("per-thread   : window={0} s ; top by CPU seconds" -f $result.perThreadCpu.windowSeconds)
    $reg = $result.perThreadCpu.perThreadByRegime
    if ($reg -and @($reg).Count -gt 0) {
        Write-Output ("               cores by regime (fast -> slow) at this session's own median of {0} Hz; the thread whose CPU RISES is the one to fix:" -f $result.perThreadCpu.regimeThresholdHz)
        foreach ($t in $reg) {
            Write-Output ("                 thread {0,-6} fast={1,6}  slow={2,6} cores   delta={3,7}" -f `
                $t.threadId, $t.fastCores, $t.slowCores, [Math]::Round($t.slowCores - $t.fastCores, 3))
        }
    }
    foreach ($t in $result.perThreadCpu.topThreads) {
        Write-Output ("               tid={0,-7} cpu={1,8} s  cores={2}" -f $t.threadId, $t.cpuSeconds, $t.coresAverage)
    }
} else {
    Write-Output ("per-thread   : {0}" -f $result.perThreadCpu.note)
}
if ($null -ne $result.processCores) {
    Write-Output ("process      : {0} cores average   workingSet peak={1} MB" -f $result.processCores, $result.workingSetPeakMb)
}
Write-Output ''
Write-Output 'compact row (tab separated):'
Write-Output (($row.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "`t")

if ($Json) {
    $result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Json
    Write-Output ''
    Write-Output "JSON written to $Json"
}
