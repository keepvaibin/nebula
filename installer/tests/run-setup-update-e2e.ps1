# A built Setup (-Setup, its version -New) on a copy of an installed Nebula (-Source, with its packed game): an update
# interrupted half way, the update, rollback, forward again, rollback refused while Nebula runs, and the installed
# version started to its title. Its own folders (-Work) and data root: the player's install, saves and settings stay.
#   run-setup-update-e2e.ps1 -Setup <Nebula-Setup-X.exe> -New <X> -Source <install root> -Work <folder>
param([Parameter(Mandatory = $true)][string]$Setup, [Parameter(Mandatory = $true)][string]$New,
      [Parameter(Mandatory = $true)][string]$Source, [Parameter(Mandatory = $true)][string]$Work)
$ErrorActionPreference = 'Stop'
$base = $Work
$source = $Source
$root = Join-Path $base 'Nebula'
$data = Join-Path $base 'data'
$old = (Get-Content -Raw (Join-Path $source 'current.json') | ConvertFrom-Json).version
$failures = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "PASS: $what" } else { "FAIL: $what"; $script:failures++ } }
function Current() { Get-Content -Raw (Join-Path $root 'current.json') | ConvertFrom-Json }
function Hash-Tree([string]$dir) {
    (Get-ChildItem -LiteralPath $dir -Recurse -File | Sort-Object FullName | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName).Hash + ' ' + $_.FullName.Substring($dir.Length) }) -join "`n"
}
function Version-Intact([string]$version) {
    $dir = Join-Path $root "versions\$version"
    $manifest = Get-Content -Raw (Join-Path $dir 'install.json') | ConvertFrom-Json
    foreach ($p in $manifest.files.PSObject.Properties) {
        $file = Join-Path $dir $p.Name
        if (-not (Test-Path -LiteralPath $file)) { return $false }
        if ((Get-FileHash -LiteralPath $file).Hash -ne ([string]$p.Value).ToUpper()) { return $false }
    }
    return $true
}
# -Interrupt: ended by force as soon as the new version's files begin to appear in staging (before the switch).
function Run-Setup([string[]]$Arguments, [switch]$Interrupt) {
    $env:NEBULA_DATA_ROOT = $data
    $p = Start-Process -FilePath $Setup -ArgumentList (@('--unattended', '--root', "`"$root`"") + $Arguments) -PassThru -WindowStyle Hidden
    if ($Interrupt) {
        while (-not $p.HasExited) {
            $app = Get-ChildItem -LiteralPath (Join-Path $root 'staging') -Directory -ErrorAction SilentlyContinue |
                ForEach-Object { Join-Path $_.FullName 'app' } | Where-Object { Test-Path -LiteralPath $_ }
            if ($app) { Stop-Process -Id $p.Id -Force; $p.WaitForExit(); Remove-Item Env:NEBULA_DATA_ROOT; return 'killed' }
            Start-Sleep -Milliseconds 5
        }
    }
    $p.WaitForExit()
    Remove-Item Env:NEBULA_DATA_ROOT
    return $p.ExitCode
}

# A fresh copy: the program folders copied, the packed game hard-linked; saves and settings of the test's own.
if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }
New-Item -ItemType Directory -Force $root, $data | Out-Null
& robocopy $source $root /E /XF game.pak /NFL /NDL /NJH /NJS /NP | Out-Null
New-Item -ItemType HardLink -Path (Join-Path $root 'content\game.pak') -Target (Join-Path $source 'content\game.pak') | Out-Null
New-Item -ItemType Directory -Force (Join-Path $data 'nand\title\00010000\524d4745\data'), (Join-Path $data 'settings') | Out-Null
[IO.File]::WriteAllBytes((Join-Path $data 'nand\title\00010000\524d4745\data\GameData.bin'), [byte[]](1..200))
[IO.File]::WriteAllText((Join-Path $data 'settings\config.ini'), "efb_scale = 2`n")
$dataBefore = (Hash-Tree (Join-Path $data 'nand')) + (Hash-Tree (Join-Path $data 'settings'))
"installed: $old; updating to $New"

# 1. The update interrupted half way (Setup ended by force): the installed version is untouched and still current.
$result = Run-Setup @('--apply-update') -Interrupt
Check ($result -eq 'killed') "the update was interrupted half way ($result)"
Check ((Current).version -eq $old) "interrupted: $old is still the current version"
Check (Version-Intact $old) "interrupted: $old's files are whole"
Check (-not (Test-Path -LiteralPath (Join-Path $root "versions\$New"))) "interrupted: no half-made $New folder"

# 2. The update run to its end: the new version current, the old kept for rollback.
$result = Run-Setup @('--apply-update')
Check ($result -eq 0) "the update completes (exit $result)"
$c = Current
Check ($c.version -eq $New -and $c.previous -eq $old) "current $($c.version), previous $($c.previous)"
Check ((Version-Intact $New) -and (Version-Intact $old)) "both versions' files are whole"
Check (-not (Get-ChildItem -LiteralPath (Join-Path $root 'staging') -Directory -ErrorAction SilentlyContinue)) "the interrupted run's staging is cleaned up"

# 3. Rollback, then forward again.
$result = Run-Setup @('--rollback')
$c = Current
Check ($result -eq 0 -and $c.version -eq $old -and $c.previous -eq $New) "rollback: current $($c.version), previous $($c.previous)"
$result = Run-Setup @('--apply-update')
$c = Current
Check ($result -eq 0 -and $c.version -eq $New -and $c.previous -eq $old) "forward again: current $($c.version), previous $($c.previous)"

# 4. With Nebula running from the install, rollback refuses and changes nothing.
$fake = Join-Path $root "versions\$New\NebulaSettings.exe.fake"
$runner = Join-Path $root "versions\$New\NebulaSettings.exe"
$hadSettings = Test-Path -LiteralPath $runner
if ($hadSettings) { Move-Item -LiteralPath $runner $fake }
Copy-Item (Join-Path $env:WINDIR 'System32\cmd.exe') $runner
$held = Start-Process -FilePath $runner -ArgumentList '/c ping -n 8 127.0.0.1 > nul' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 400
$result = Run-Setup @('--rollback')
Check ($result -ne 0 -and (Current).version -eq $New) "with Nebula running, rollback refuses (exit $result) and $New stays"
Stop-Process -Id $held.Id -Force; $held.WaitForExit()
Remove-Item -LiteralPath $runner -Force
if ($hadSettings) { Move-Item -LiteralPath $fake $runner }

# 5. The installed version starts and reaches the title, hidden and silent (its sound to a file), with the test's own
# settings and app data.
$bin = Join-Path $root "versions\$New"
$appdata = Join-Path $base 'localappdata'
New-Item -ItemType Directory -Force $appdata, (Join-Path $base 'nand') | Out-Null
$env:GALAXY_HIDE_WINDOW = '1'; $env:GALAXY_XINPUT_REQUIRE_FOCUS = '1'; $env:LOCALAPPDATA = $appdata
$env:GALAXY_AUDIO_WAV_DUMP_ONLY = '1'; $env:GALAXY_AUDIO_WAV_DUMP = Join-Path $base 'audio.wav'
$env:GALAXY_RENDER_CONFIG_PATH = Join-Path $base 'config.ini'; $env:GALAXY_RUNTIME_SETTINGS_PATH = Join-Path $base 'runtime_settings.ini'
$env:GALAXY_CRASH_DUMP_DIR = $base
$log = Join-Path $base 'boot.log'
$p = Start-Process -FilePath (Join-Path $bin 'NebulaNative.exe') -ArgumentList @('--pak', "`"$(Join-Path $root 'content\game.pak')`"", '--nand', "`"$(Join-Path $base 'nand')`"", '--seconds', '6') -PassThru -WindowStyle Hidden -RedirectStandardOutput $log -RedirectStandardError (Join-Path $base 'boot-err.log')
$p.WaitForExit()
Check ((Select-String -LiteralPath $log -Pattern 'boot: title screen' -Quiet)) "the installed $New starts and reaches the title"

$dataAfter = (Hash-Tree (Join-Path $data 'nand')) + (Hash-Tree (Join-Path $data 'settings'))
"setup logs written: $((Get-ChildItem -LiteralPath (Join-Path $data 'setup-logs') -File).Count)"
Check ($dataBefore -eq $dataAfter) "saves and settings unchanged through it all"
"$failures failure(s)"
if ($failures -ne 0) { exit 1 }
