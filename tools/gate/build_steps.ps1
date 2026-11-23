# build_steps.ps1: the build steps of build_windows.ps1, dot-sourced by it (and by its self-test, tools/gate/tests/build_steps_test.py).
# A step is a command in the build environment with its output in a log. Start-Step starts it, Finish-Step waits for it and judges
# it: a FAILED line or a non-zero exit fails the build. Reads $vcvars (empty: no build environment, for the test); keeps its
# counts in the including script's scope: $script:failed (steps that failed) and $script:cacheOnly (steps that failed twice in
# the compiler cache alone).
#
# The compiler cache's own client can fail without a compiler message. Seen on 2026-11-21: one job of about 4,700, in a build from
# nothing with the cache's server running throughout, ended with "sccache: error: Only one usage of each socket address ...
# (os error 10048)", a line of the client, and no compiler message. Nothing more is known of the cause.
# When every FAILED block of a step's log holds a line that starts with `sccache: error` and no compiler or linker diagnostic, the step is run once more by itself (the same command: ninja builds
# only what is missing) and one line says so; the second result is the step's result. A second failure of the same kind is
# counted in $script:cacheOnly (the script ends "not judged", exit 2); a step with any real diagnostic is never run again.
$script:failed = 0
$script:cacheOnly = 0

function Start-Step([string]$Command, [string]$Log) {
    $prefix = if ($vcvars) { "call `"$vcvars`" >nul && " } else { "" }
    $p = Start-Process -FilePath cmd.exe -ArgumentList "/d /c `"$prefix($Command) > `"$Log`" 2>&1`"" -WindowStyle Hidden -PassThru
    $null = $p.Handle  # held from the start, or ExitCode reads empty once the process has ended
    $p
}

# What a step's log says of its failed jobs: Total (FAILED blocks: from a `FAILED:` line to the next `[n/m]` or `ninja:` line),
# CacheOnly (the blocks with a `sccache: error` line and no compiler or linker diagnostic), All (there are blocks, and every one is
# of the compiler cache), First (the first sccache line, trimmed).
function Get-CacheFailures([string]$Log) {
    $blocks = New-Object System.Collections.ArrayList
    $current = $null
    if (Test-Path $Log) {
        foreach ($line in (Get-Content -Path $Log -ErrorAction SilentlyContinue)) {
            if ($line -match '^\s*FAILED:') {
                $current = New-Object System.Collections.ArrayList
                $null = $blocks.Add($current)
            } elseif ($line -match '^\s*\[\d+/\d+\]' -or $line -match '^\s*ninja:') {
                $current = $null
            }
            if ($null -ne $current) { $null = $current.Add($line) }
        }
    }
    $cache = 0
    $first = ""
    foreach ($b in $blocks) {
        $sccache = @($b | Where-Object { $_ -match '^\s*sccache: error' })
        $diag = @($b | Where-Object { $_ -notmatch '^\s*sccache: error' -and $_ -match ' error C|error LNK|: error:|fatal error' })
        if ($sccache.Count -gt 0 -and $diag.Count -eq 0) {
            $cache++
            if (-not $first) { $first = $sccache[0].Trim(); if ($first.Length -gt 160) { $first = $first.Substring(0, 160) } }
        }
    }
    [pscustomobject]@{ Total = $blocks.Count; CacheOnly = $cache; All = ($blocks.Count -gt 0 -and $cache -eq $blocks.Count); First = $first }
}

function Finish-Step([string]$Name, $Process, [string]$Log, $Watch, [string]$Command = "") {
    $Process.WaitForExit()
    $code = $Process.ExitCode
    $again = $false
    $fails = @(Select-String -Path $Log -Pattern '^FAILED:|ninja: build stopped' -ErrorAction SilentlyContinue)
    if (($code -ne 0 -or $fails.Count -ne 0) -and $Command) {
        $cache = Get-CacheFailures $Log
        if ($cache.All) {
            "{0}: {1} job(s) failed in the compiler cache, not in the compiler; built again ({2})" -f $Name, $cache.Total, $cache.First
            Move-Item -Force -Path $Log -Destination "$Log.cache-error"      # the first log stays, the second is the step's
            $Watch = [Diagnostics.Stopwatch]::StartNew()
            $second = Start-Step $Command $Log
            $second.WaitForExit()
            $code = $second.ExitCode
            $fails = @(Select-String -Path $Log -Pattern '^FAILED:|ninja: build stopped' -ErrorAction SilentlyContinue)
            $again = $true
        }
    }
    "{0}: exit {1}, {2} FAILED, {3} s" -f $Name, $code, $fails.Count, [int]$Watch.Elapsed.TotalSeconds
    if ($code -ne 0 -or $fails.Count -ne 0) {
        Select-String -Path $Log -Pattern '^FAILED:|: error|: fatal error|error:' | Select-Object -First 12 |
            ForEach-Object { "  " + $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
        if ($again -and (Get-CacheFailures $Log).All) { $script:cacheOnly++ } else { $script:failed++ }
    }
}

function Step([string]$Name, [string]$Command, [string]$Log) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Finish-Step $Name (Start-Step $Command $Log) $Log $sw $Command
}

# What a configured game folder's CMakeCache.txt holds of NEBULA_SANITIZE (the text; empty when the entry is not there) and of
# NEBULA_COVERAGE ($true when it is ON). build_windows.ps1 asks the same question before configuring and after (M-003).
function Get-Game-Config([string]$Folder) {
    $cache = "$Folder/CMakeCache.txt"
    if (-not (Test-Path $cache)) { return [pscustomobject]@{ Sanitize = ""; Counted = $false } }
    $sanitize = Select-String -Path $cache -Pattern '^NEBULA_SANITIZE:STRING=(.*)$' -ErrorAction SilentlyContinue | Select-Object -First 1
    [pscustomobject]@{
        Sanitize = if ($sanitize) { $sanitize.Matches[0].Groups[1].Value } else { "" }
        Counted  = [bool](Select-String -Path $cache -Pattern '^NEBULA_COVERAGE:BOOL=ON$' -Quiet -ErrorAction SilentlyContinue)
    }
}

# Whether the game folder holds other sanitizers or another coverage setting than asked: it is then configured again with the
# ones asked for (the settings are cache entries, so the configure command's -D values replace them), never built as it is.
function Game-Config-Differs([string]$Folder, [string]$Sanitize, [bool]$Coverage) {
    $have = Get-Game-Config $Folder
    return ($have.Sanitize -ne $Sanitize) -or ($have.Counted -ne $Coverage)
}
