param(
    [string]$Root = 'D:\NebulaWork\perf-20261007',
    [string]$Build = 'control',
    [string]$Content = 'D:\NebulaWork\Test Install 2\Nebula\content',
    [ValidateRange(1, 16)][int]$Scale = 1,
    [switch]$Monitor,
    [switch]$Detailed,
    [switch]$Gpu,
    # Opt-in, intrusive per-draw attribution on one frame in every N.
    # The runtime accumulates the renderer breakdown
    # (flush-us, flush-pso-us, pso-get-us/max/calls, vertex-load-us,
    # draw-record-us, and the producer-side snapshot/dependency clocks) only when
    # offline sampling is on, and the sampled frame also *reports* those fields.
    # Before this switch existed the harness could not produce that profile at
    # all: -Detailed enables the per-draw clocks but the report is additionally
    # gated on the frame being a stall, so it printed at most a handful of
    # tail-biased frames. 0 disables. No sample period has established overhead
    # on the acceptance scene; use unsampled runs for the baseline.
    [ValidateRange(0, 100000)][int]$Sample = 0,
    # Copy the session's warmed shader cache back over cache-seed after a clean
    # exit. Off by default because it changes the artifact a later run starts
    # from, so it must be deliberate. See the note at the refresh site.
    [switch]$RefreshSeed
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'copy_preserved_tree.ps1')
# $Root\$Build is either the install folder itself (what build-release produces
# and what perf-20261007\control actually is) or a parent that contains one
# (Setup's versions\<version>\Nebula layout). Resolve the folder that really
# holds NebulaRuntime.exe instead of assuming a fixed depth: the previous
# unconditional Join-Path produced a path that did not exist, so the harness
# failed before it could record anything, which is worse than a bad number
# because it silently yields no number at all.
$buildRoot = Join-Path $Root $Build
if (-not (Test-Path -LiteralPath $buildRoot)) { throw "Build folder not found: $buildRoot" }
$app = $buildRoot
if (-not (Test-Path -LiteralPath (Join-Path $app 'NebulaRuntime.exe'))) {
    $nested = Join-Path $buildRoot 'Nebula'
    if (Test-Path -LiteralPath (Join-Path $nested 'NebulaRuntime.exe')) {
        $app = $nested
    } else {
        $found = @(Get-ChildItem -LiteralPath $buildRoot -Recurse -Depth 3 -Filter 'NebulaRuntime.exe' -File -ErrorAction SilentlyContinue)
        if ($found.Count -ne 1) {
            throw "Could not resolve a single NebulaRuntime.exe under $buildRoot (found $($found.Count))"
        }
        $app = $found[0].Directory.FullName
    }
}
$session = Join-Path $Root ('sessions\' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $session | Out-Null
Copy-PreservedTree -Source (Join-Path $Root 'save-seed') -Destination (Join-Path $session 'saves')
New-Item -ItemType Directory (Join-Path $session 'local\Nebula') -Force | Out-Null
# Recurse: cache-seed holds the per-title subdirectory (RMGE01\shaders.bin,
# psos.bin, pipelines.bin), so a shallow copy would drop the caches the seed
# exists to provide and silently turn a warmed run into a first-use run.
Copy-Item -LiteralPath (Join-Path $Root 'cache-seed') -Destination (Join-Path $session 'local\Nebula\shadercache') -Recurse -Force
# Record the cache condition the run actually started from. The objective
# requires first-use compilation to be separated from warmed gameplay, and the
# only way a later reader can honour that is if the recording says which it was:
# an empty or stale cache-seed makes a run pay FXC and driver PSO builds that a
# warmed run does not, and the two must never be compared. The runtime's own
# "[PipelineCache] loaded N DXBC pairs / M PSO keys" line reports what it
# accepted; this reports what was offered, so a seed that is present but whose
# keys no longer match still shows up as warm here and cold there.
$cacheSeedState = [ordered]@{ present = $false; files = 0; bytes = 0; newestUtc = $null }
$cacheSeedPath = Join-Path $session 'local\Nebula\shadercache'
if (Test-Path -LiteralPath $cacheSeedPath) {
    $cacheFiles = @(Get-ChildItem -LiteralPath $cacheSeedPath -Recurse -File -ErrorAction SilentlyContinue)
    $cacheSeedState.present = $cacheFiles.Count -gt 0
    $cacheSeedState.files = $cacheFiles.Count
    $cacheSeedState.bytes = [int](($cacheFiles | Measure-Object -Property Length -Sum).Sum)
    if ($cacheFiles.Count -gt 0) {
        $cacheSeedState.newestUtc = ($cacheFiles | Sort-Object LastWriteTime -Descending)[0].LastWriteTime.ToUniversalTime().ToString('o')
    }
}
$start = New-Object Diagnostics.ProcessStartInfo
$start.FileName = Join-Path $app 'NebulaRuntime.exe'
$start.Arguments = '"' + $Content + '" "' + (Join-Path $app 'RMGE01_game.dll') + '"'
$start.WorkingDirectory = $app
$start.UseShellExecute = $false
foreach ($key in @($start.EnvironmentVariables.Keys | Where-Object { $_ -like 'GALAXY_*' })) { $start.EnvironmentVariables.Remove($key) }
foreach ($pair in (Get-Content (Join-Path $app 'runtime-env.json') -Raw | ConvertFrom-Json)) { $start.EnvironmentVariables[$pair.name] = $pair.value }
$envs = $start.EnvironmentVariables
$envs['LOCALAPPDATA'] = Join-Path $session 'local'
$envs['GALAXY_NAND_ROOT'] = Join-Path $session 'saves'
$envs['GALAXY_WINDOW_WIDTH'] = '1280'; $envs['GALAXY_WINDOW_HEIGHT'] = '720'
$envs['GALAXY_FULLSCREEN'] = '0'; $envs['GALAXY_EFB_SCALE'] = "$Scale"
$envs['GALAXY_EXPERIMENTAL_NATIVE_4_3'] = '0'
$envs['GALAXY_EXPERIMENTAL_DYNAMIC_ASPECT'] = '0'
$envs['GALAXY_EXPERIMENTAL_ULTRAWIDE_ASPECT'] = ''
# GPU queries are independent of unrelated pointer/display/movie monitoring.
# They measure completed command-list execution, not the whole critical path.
# Their overhead still needs a matched on/off comparison. GPU-only capture
# collects timestamps; the existing stats logger exports them when monitoring
# is also enabled.
$gpuTiming = $Gpu.IsPresent
$envs['GALAXY_GPU_TIMESTAMPS'] = [string][int]$gpuTiming
foreach ($flag in @('GALAXY_TRACE_PRESENT_STATS','GALAXY_MONITOR_POINTER_LATENCY','GALAXY_MONITOR_FRAME_TAILS','GALAXY_MONITOR_DISPLAY_LAYOUT','GALAXY_TRACE_NATIVE_THP_BOUNDARY','GALAXY_TRACE_GX_STALLS')) { $envs[$flag] = '0' }
if ($Monitor -or $Detailed) {
    foreach ($flag in @('GALAXY_TRACE_PRESENT_STATS','GALAXY_MONITOR_POINTER_LATENCY','GALAXY_MONITOR_FRAME_TAILS','GALAXY_MONITOR_DISPLAY_LAYOUT','GALAXY_TRACE_NATIVE_THP_BOUNDARY')) { $envs[$flag] = '1' }
}
if ($Detailed) { $envs['GALAXY_TRACE_GX_STALLS'] = '1'; $envs['GALAXY_TRACE_GX_STALL_US'] = '20000' }
# Per-draw timing is intrusive and opt-in independently of monitoring.
# Periods below2 disable it. Assign the disabled value explicitly so an omitted
# -Sample cannot inherit a detailed sampling period from runtime-env.json.
# Different sampled/unsampled workloads do not measure instrumentation overhead;
# that requires matched scenes/settings with timing disabled and enabled.
$envs['GALAXY_GX_FRAME_TIMING_SAMPLE'] = if ($Sample -ge 2) { "$Sample" } else { '0' }
# Each invocation captures one private session and waits for normal exit.
$info = [ordered]@{ session=$session; build=$app; scale=$Scale; monitor=$Monitor.IsPresent; detailed=$Detailed.IsPresent; gpu=$Gpu.IsPresent; environment=@{} }
$info.cacheSeed = $cacheSeedState
if (-not $cacheSeedState.present) {
    Write-Warning "cache-seed is missing or empty: this run starts cold and will pay first-use FXC and driver PSO builds that a warmed run does not. Do not compare it against a warmed run."
}
# Host CPU identity and clock, captured once before launch so it costs the run
# nothing. Without these a cross-machine comparison of CPU cost per unit of guest
# progress cannot separate "this CPU is slower per clock" from "this build does
# more work per unit" -- which is exactly the ambiguity that made a 6.85x
# guest-progress gap look like blocking when a 2.3x clock difference plus IPC and
# cache effects could account for it on its own. L3 size and memory speed are the
# other two terms in per-instruction cost, and both matter for a 51 MB module.
$info.hostCpu = [ordered]@{
    name=$null; cores=$null; logical=$null; maxClockMhz=$null
    currentClockMhz=$null; l3CacheKb=$null; memorySpeedMhz=$null; capturedUtc=$null
}
# Sampled clock during the run lives in clock.csv; the fields below are the
# pre-launch reading, which is idle-state and therefore only a sanity reference.
$info.clockSampling = 'clock.csv (1 Hz: elapsedSeconds,currentClockMhz,maxClockMhz,loadPercent)'
try {
    $cpu = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1
    $info.hostCpu.name = $cpu.Name
    $info.hostCpu.cores = $cpu.NumberOfCores
    $info.hostCpu.logical = $cpu.NumberOfLogicalProcessors
    $info.hostCpu.maxClockMhz = $cpu.MaxClockSpeed
    $info.hostCpu.currentClockMhz = $cpu.CurrentClockSpeed
    $info.hostCpu.l3CacheKb = $cpu.L3CacheSize
    $info.hostCpu.capturedUtc = [DateTime]::UtcNow.ToString('o')
} catch {
    Write-Warning "could not read host CPU identity: $($_.Exception.Message)"
}
# Registry fallback: WMI/CIM can be denied in constrained or service contexts, and
# the processor description is readable without it. The clock here is the nominal
# rating the firmware advertises, not a live sample, so it is recorded separately
# from currentClockMhz rather than overwriting it.
try {
    $reg = Get-ItemProperty -Path 'HKLM:\HARDWARE\DESCRIPTION\System\CentralProcessor\0' -ErrorAction Stop
    if (-not $info.hostCpu.name) { $info.hostCpu.name = $reg.ProcessorNameString }
    if (-not $info.hostCpu.maxClockMhz -and $reg.'~MHz') { $info.hostCpu.maxClockMhz = [int]$reg.'~MHz' }
    $info.hostCpu.registryName = $reg.ProcessorNameString
    $info.hostCpu.registryMhz = if ($reg.'~MHz') { [int]$reg.'~MHz' } else { $null }
} catch {
    Write-Warning "could not read host CPU from the registry either: $($_.Exception.Message)"
}
if (-not $info.hostCpu.name) {
    Write-Warning "host CPU identity unavailable by CIM and by registry; a cross-machine CPU-cost ratio from this session cannot separate clock from work"
}
try {
    $mem = Get-CimInstance -ClassName Win32_PhysicalMemory -ErrorAction Stop |
        Sort-Object Speed -Descending | Select-Object -First 1
    $info.hostCpu.memorySpeedMhz = $mem.Speed
} catch { }
foreach ($key in $envs.Keys) { if ($key -like 'GALAXY_*' -or $key -eq 'LOCALAPPDATA') { $info.environment[$key] = $envs[$key] } }
$info.files = @{}
foreach ($name in @('NebulaRuntime.exe','RMGE01_game.dll','RMGE01_home_button.dll','RMGE01_dsp.dll','RMGE01_boot_image.bin','runtime-env.json','runtime-isa.txt','install.json')) {
    $path = Join-Path $app $name
    if (Test-Path -LiteralPath $path) { $info.files[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
# The CPU target the prebuilt runtime and the per-machine modules were built
# for. Two installs can share a version label and still run different machine
# code, so record it explicitly rather than leaving it to be inferred from
# hashes. Produced by build-release (-NativeIsa) and extracted by Setup.
$runtimeIsaPath = Join-Path $app 'runtime-isa.txt'
if (Test-Path -LiteralPath $runtimeIsaPath) {
    $info.runtimeIsa = (Get-Content -LiteralPath $runtimeIsaPath -Raw).Trim()
}
$start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
$p = [Diagnostics.Process]::Start($start)
$info.pid = $p.Id
$info | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $session 'session.json')
$info | ConvertTo-Json -Depth 2 -Compress | Write-Output
$stdout = $p.StandardOutput.ReadToEndAsync(); $stderr = $p.StandardError.ReadToEndAsync()

# Per-thread CPU and wait reason, sampled once a second for the life of the run.
#
# The delivered recordings contain no per-thread evidence at all: the launcher's
# process.csv is process-wide, so "the simulation thread is at 100 %" and "the
# simulation thread is blocked on the render queue while the DSP thread burns a
# core" are indistinguishable in them. That is the one discrimination the
# objective asks for ("active simulation work, renderer CPU work, GPU execution,
# or a dependency between them") and nothing in the tree produces it.
#
# System.Diagnostics.ProcessThread gives TotalProcessorTime and WaitReason with
# no P/Invoke and no extra thread; the existing 1 s WaitForExit poll is a free
# sampling tick. Every access is guarded: a monitoring failure must never change
# or abort the measured run, so a failed sample is skipped and the CSV simply has
# fewer rows.
$threadRows = New-Object Collections.Generic.List[string]
$threadRows.Add('elapsedSeconds,threadId,cpu100ns,threadState,waitReason,priorityLevel')
$threadSampleStart = [DateTime]::UtcNow
$threadSampleErrors = 0
function Get-ThreadSampleRows {
    param($Process, [double]$Elapsed)
    $rows = New-Object Collections.Generic.List[string]
    foreach ($t in $Process.Threads) {
        try {
            # TimeSpan.Ticks is already 100-nanosecond units, which is the unit
            # the column name claims; no scaling. Verified on this host:
            # TotalProcessorTime 00:00:00.0781250 reports Ticks = 7812500.
            $cpu = $t.TotalProcessorTime.Ticks
            $threadState = $t.ThreadState
            # WaitReason throws for a running thread. Keep its CPU sample rather
            # than losing precisely the threads doing active work.
            $waitReason = if ($threadState -eq [Diagnostics.ThreadState]::Wait) { $t.WaitReason } else { '' }
            $elapsedText = $Elapsed.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
            $rows.Add(('{0},{1},{2},{3},{4},{5}' -f $elapsedText, $t.Id, $cpu, $threadState, $waitReason, $t.PriorityLevel))
        } catch {
            # A thread can exit between enumeration and the property read.
        }
    }
    return $rows
}

# Host CPU clock, sampled once a second on the same tick as the thread rows.
#
# This closes the largest remaining uncertainty in the whole performance analysis.
# CPU-seconds per completed guest checkpoint is clock-free (A14-19): the Arc spends
# 272.2 ns against the 5090's 34.8 ns, a ratio of 7.82x. Converting that to *cycles*
# per checkpoint requires the clock, and the 2.27x figure used to derive the
# remaining "3.44x of IPC/cache/work" was an ASSUMPTION (5.0 vs 2.2 GHz), never
# measured. A laptop under sustained CPU+iGPU load can sit far below its rated
# clock, and the iGPU shares the package power budget, so the assumption could be
# wrong by a large factor in exactly the direction that matters: if the Arc sustains
# ~1.5 GHz, clock alone explains nearly the whole residual and the deficit is a power
# limit rather than a code defect.
#
# Win32_Processor.CurrentClockSpeed is the firmware's live reading. It is cheap but
# not free, so it is sampled at the same 1 Hz cadence as the threads -- coarse enough
# to add nothing measurable to a multi-minute run, fine enough to see a sustained
# state rather than a boost spike. Wrapped in try/catch because CIM can be denied.
$clockRows = New-Object Collections.Generic.List[string]
$clockRows.Add('elapsedSeconds,currentClockMhz,maxClockMhz,loadPercent')
$clockSampleErrors = 0
$clockSampleStart = [DateTime]::UtcNow

while (-not $p.WaitForExit(1000)) {
    try {
        $elapsed = ([DateTime]::UtcNow - $threadSampleStart).TotalSeconds
        foreach ($row in (Get-ThreadSampleRows -Process $p -Elapsed $elapsed)) { $threadRows.Add($row) }
    } catch {
        $threadSampleErrors++
    }
    try {
        $cpuNow = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop | Select-Object -First 1
        $clockRows.Add(('{0},{1},{2},{3}' -f `
            (([DateTime]::UtcNow - $clockSampleStart).TotalSeconds.ToString('F1', [Globalization.CultureInfo]::InvariantCulture)), `
            $cpuNow.CurrentClockSpeed, $cpuNow.MaxClockSpeed, $cpuNow.LoadPercentage))
    } catch {
        $clockSampleErrors++
    }
}
$stdoutText = $stdout.Result
$stderrText = $stderr.Result
[IO.File]::WriteAllText((Join-Path $session 'stdout.log'), $stdoutText)
[IO.File]::WriteAllText((Join-Path $session 'stderr.log'), $stderrText)
if ($threadRows.Count -gt 1) {
    [IO.File]::WriteAllLines((Join-Path $session 'threads.csv'), $threadRows)
}
if ($clockRows.Count -gt 1) {
    [IO.File]::WriteAllLines((Join-Path $session 'clock.csv'), $clockRows)
}
# The runtime prints its own build identity as the very first stderr line:
# source commit/revision kind, dirty state, runtime-inputs digest, compiler,
# config, flags, the effective `isa=`, dsp-module and recipe. Retaining it here
# is what makes a recording self-describing - the delivered bundles had lost it,
# so their only identity was a hash table, which detects a difference without
# explaining it. Parsed from the captured text, never from a live console.
$runtimeBuild = $null
$buildMatch = [regex]::Match($stderrText, '(?m)^\[runtime-build\]\s*(.+?)\s*$')
if ($buildMatch.Success) { $runtimeBuild = $buildMatch.Groups[1].Value }
$exitInfo = [ordered]@{
    exit = $p.ExitCode; ended = [DateTime]::UtcNow.ToString('o')
    runtimeBuild = $runtimeBuild
    runtimeIsa = $info.runtimeIsa
    gpuTiming = [string][int]$gpuTiming
    threadSamples = [Math]::Max(0, $threadRows.Count - 1)
    threadSampleErrors = $threadSampleErrors
}
$exitInfo | ConvertTo-Json | Set-Content (Join-Path $session 'exit.json')

# Refresh cache-seed from the cache this run warmed.
#
# Why this exists: the run is handed a copy of cache-seed (line 41) and points
# LOCALAPPDATA at it (line 69), but nothing ever copied the warmed cache back.
# So every run started from the same frozen seed and threw away whatever it had
# just compiled -- which means a shader configuration the seed does not cover is
# recompiled by FXC from source in EVERY measured session. In the delivered
# recording that is 17 configurations at 6.7-21.5 ms of FXC plus 2.3-15.2 ms of
# driver PSO build each, synchronously on the render thread: it is exactly the
# "fixed hitch points that never change" symptom, and the harness reproduces it
# by construction. It is invisible in `loaded N DXBC pairs` because the runtime
# faithfully reports the state of the seed it was given.
#
# Off unless -RefreshSeed is passed, because it mutates the baseline a later run
# compares against. Refresh once, deliberately, when the goal is "measure warmed
# gameplay"; leave it off when the goal is "A/B the same conditions twice".
if ($RefreshSeed) {
    $exitInfo.seedRefresh = [ordered]@{ attempted = $true; copied = $false; reason = '' }
    try {
        if ($p.ExitCode -ne 0) {
            $exitInfo.seedRefresh.reason = "run exited $($p.ExitCode); a failed run's cache is not a baseline"
        } elseif (-not (Test-Path -LiteralPath $cacheSeedPath)) {
            $exitInfo.seedRefresh.reason = 'no session cache directory'
        } else {
            $seedRoot = Join-Path $Root 'cache-seed'
            $titlePath = Join-Path $cacheSeedPath 'RMGE01'
            $title = if (Test-Path -LiteralPath $titlePath) { Get-Item -LiteralPath $titlePath -Force -ErrorAction Stop } else { $null }
            $titleFiles = if ($null -ne $title -and $title.PSIsContainer) {
                @(Get-ChildItem -LiteralPath $titlePath -File -Force -ErrorAction Stop |
                    Where-Object { $_.Name -in @('shaders.bin','psos.bin','pipelines.bin') -and $_.Length -gt 0 })
            } else { @() }
            if ($titleFiles.Count -eq 0) {
                $exitInfo.seedRefresh.reason = 'no nonempty RMGE01 cache files in session'
            } else {
                $nodes = @((Get-Item -LiteralPath $cacheSeedPath -Force),$title) + @(Get-ChildItem -LiteralPath $titlePath -Recurse -Force -ErrorAction Stop)
                if ($nodes | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
                    throw 'Cache refresh requires physical directories and files.'
                }
                # Keep the previous seed recoverable. This optional refresh is
                # not an atomic baseline publication; do not run concurrent
                # refreshes or use a failed refresh as an acceptance baseline.
                if (Test-Path -LiteralPath $seedRoot) {
                    Copy-PreservedTree -Source $seedRoot -Destination (Join-Path $session 'cache-seed-before-refresh')
                }
                New-Item -ItemType Directory -Path $seedRoot -Force | Out-Null
                # Copy the explicit title, retaining cache-seed\RMGE01. A legacy
                # nested shadercache sibling is not a usable cache for this run.
                Copy-Item -LiteralPath $title.FullName -Destination $seedRoot -Recurse -Force -ErrorAction Stop
                $exitInfo.seedRefresh.copied = $true
                $exitInfo.seedRefresh.reason = "copied $titlePath -> $seedRoot; prior seed preserved in session"
            }
        }
    } catch {
        $exitInfo.seedRefresh.reason = "refresh failed: $($_.Exception.Message)"
    }
    $exitInfo | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $session 'exit.json')
    if (-not $exitInfo.seedRefresh.copied) {
        Write-Warning "cache-seed was NOT refreshed: $($exitInfo.seedRefresh.reason)"
    }
}
