param(
    [string]$Root = 'D:\NebulaWork\perf-20261007',
    [string]$Build = 'control',
    [string]$Content = 'D:\NebulaWork\Test Install 2\Nebula\content',
    [ValidateRange(1, 16)][int]$Scale = 1,
    [switch]$Monitor,
    [switch]$Detailed,
    [switch]$Gpu
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'copy_preserved_tree.ps1')
$app = Join-Path $Root $Build
$session = Join-Path $Root ('sessions\' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $session | Out-Null
Copy-PreservedTree -Source (Join-Path $Root 'save-seed') -Destination (Join-Path $session 'saves')
New-Item -ItemType Directory (Join-Path $session 'local\Nebula') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $Root 'cache-seed') -Destination (Join-Path $session 'local\Nebula\shadercache') -Recurse
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
$envs['GALAXY_GPU_TIMESTAMPS'] = [string][int]$Gpu.IsPresent
foreach ($flag in @('GALAXY_TRACE_PRESENT_STATS','GALAXY_MONITOR_POINTER_LATENCY','GALAXY_MONITOR_FRAME_TAILS','GALAXY_MONITOR_DISPLAY_LAYOUT','GALAXY_TRACE_NATIVE_THP_BOUNDARY','GALAXY_TRACE_GX_STALLS')) { $envs[$flag] = '0' }
if ($Monitor -or $Detailed -or $Gpu) {
    foreach ($flag in @('GALAXY_TRACE_PRESENT_STATS','GALAXY_MONITOR_POINTER_LATENCY','GALAXY_MONITOR_FRAME_TAILS','GALAXY_MONITOR_DISPLAY_LAYOUT','GALAXY_TRACE_NATIVE_THP_BOUNDARY')) { $envs[$flag] = '1' }
}
if ($Detailed) { $envs['GALAXY_TRACE_GX_STALLS'] = '1'; $envs['GALAXY_TRACE_GX_STALL_US'] = '20000' }
# Each invocation captures one private session and waits for normal exit.
$info = [ordered]@{ session=$session; build=$app; scale=$Scale; monitor=$Monitor.IsPresent; detailed=$Detailed.IsPresent; gpu=$Gpu.IsPresent; environment=@{} }
foreach ($key in $envs.Keys) { if ($key -like 'GALAXY_*' -or $key -eq 'LOCALAPPDATA') { $info.environment[$key] = $envs[$key] } }
$info.files = @{}
foreach ($name in @('NebulaRuntime.exe','RMGE01_game.dll','RMGE01_home_button.dll','RMGE01_dsp.dll','RMGE01_boot_image.bin','runtime-env.json')) {
    $info.files[$name] = (Get-FileHash -LiteralPath (Join-Path $app $name) -Algorithm SHA256).Hash.ToLowerInvariant()
}
$start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
$p = [Diagnostics.Process]::Start($start)
$info.pid = $p.Id
$info | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $session 'session.json')
$info | ConvertTo-Json -Depth 2 -Compress | Write-Output
$stdout = $p.StandardOutput.ReadToEndAsync(); $stderr = $p.StandardError.ReadToEndAsync()
while (-not $p.WaitForExit(1000)) { }
[IO.File]::WriteAllText((Join-Path $session 'stdout.log'), $stdout.Result)
[IO.File]::WriteAllText((Join-Path $session 'stderr.log'), $stderr.Result)
@{ exit=$p.ExitCode; ended=[DateTime]::UtcNow.ToString('o') } | ConvertTo-Json | Set-Content (Join-Path $session 'exit.json')
