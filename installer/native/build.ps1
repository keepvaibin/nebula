<#
.SYNOPSIS
Builds Nebula.exe, NebulaSettings.exe and (with -Payload) Nebula-Setup.exe with the MSVC compiler.

.DESCRIPTION
Native Win32 programs with the static C runtime: no .NET, no runtime to install. Each carries the Nebula icon, an
application manifest (common controls 6, per-monitor DPI, long paths, UTF-8, asInvoker) and its file details.
-Payload embeds a zip of the files Setup installs as its PAYLOAD resource. Run inside an MSVC x64 environment
(tools/enter_msvc_environment.ps1).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Commit,
    [string]$Repository = 'keepvaibin/nebula',
    [Parameter(Mandatory = $true)][string]$PublicKeyFile,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$Payload = '',
    # Test builds only: NEBULA_UI_SNAPSHOT_DIR saves every window as a picture without showing it.
    [switch]$UiSnapshots
)
$ErrorActionPreference = 'Stop'
$native = $PSScriptRoot
$installer = Split-Path -Parent $native
$gen = Join-Path $OutputDirectory 'gen'
$obj = Join-Path $OutputDirectory 'obj'
New-Item -ItemType Directory -Force $OutputDirectory, $gen, $obj | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    # The tools' output goes to the console, never into a function's result.
    & $File @Arguments | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

function Wide([string]$Text) { return 'L"' + $Text.Replace('\', '\\').Replace('"', '\"') + '"' }

$numericVersion = (($Version -replace '-.*$', '') + '.0.0.0').Split('.')[0..3] -join '.'
$publicKey = (Get-Content -LiteralPath $PublicKeyFile -Raw).Trim()
[IO.File]::WriteAllText((Join-Path $gen 'build_info.h'), @"
#pragma once
namespace nebula::build {
inline constexpr const wchar_t kVersion[] = $(Wide $Version);
inline constexpr const wchar_t kFileVersion[] = $(Wide $numericVersion);
inline constexpr const wchar_t kCommit[] = $(Wide $Commit);
inline constexpr const wchar_t kRepository[] = $(Wide $Repository);
inline constexpr const wchar_t kReleasePublicKeyXml[] = $(Wide $publicKey);
}
"@, $utf8)

function Write-Resources([string]$Name, [string]$Description, [string]$OriginalName, [string]$PayloadPath) {
    $rcPath = Join-Path $gen "$Name.rc"
    $icon = (Join-Path $installer 'assets\nebula.ico').Replace('\', '\\')
    $manifest = (Join-Path $native 'app.manifest').Replace('\', '\\')
    $payloadLine = if ($PayloadPath) { "PAYLOAD RCDATA `"$($PayloadPath.Replace('\', '\\'))`"" } else { '' }
    [IO.File]::WriteAllText($rcPath, @"
1 ICON "$icon"
1 24 "$manifest"
$payloadLine
1 VERSIONINFO
FILEVERSION $($numericVersion.Replace('.', ','))
PRODUCTVERSION $($numericVersion.Replace('.', ','))
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x1L
FILESUBTYPE 0x0L
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "keepvaibin"
      VALUE "FileDescription", "$Description"
      VALUE "FileVersion", "$Version"
      VALUE "InternalName", "$Name"
      VALUE "LegalCopyright", "Copyright 2026 keepvaibin"
      VALUE "OriginalFilename", "$OriginalName"
      VALUE "ProductName", "Nebula"
      VALUE "ProductVersion", "$Version"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"@, $utf8)
    $res = Join-Path $obj "$Name.res"
    Invoke-Checked rc.exe @('/nologo', "/fo$res", $rcPath)
    return $res
}

$cxx = @('/nologo', '/c', '/std:c++20', '/O2', '/MT', '/DNDEBUG', '/EHsc', '/W4', '/permissive-', '/utf-8', '/Zc:__cplusplus', '/GS', '/guard:cf',
    '/Brepro', '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00', "/I$gen", "/I$native")
if ($UiSnapshots) { $cxx += '/DNEBULA_UI_SNAPSHOTS' }
$linkFlags = @('/nologo', '/SUBSYSTEM:WINDOWS', '/DYNAMICBASE', '/NXCOMPAT', '/HIGHENTROPYVA', '/guard:cf', '/OPT:REF', '/OPT:ICF',
    '/INCREMENTAL:NO', '/MANIFEST:NO', '/Brepro', '/RELEASE')

function Compile([string]$Source, [string]$Object, [string[]]$Extra = @()) {
    if ($Source.EndsWith('.c')) {
        Invoke-Checked cl.exe (@('/nologo', '/c', '/O2', '/MT', '/DNDEBUG', '/W3', '/GS', '/guard:cf', '/Brepro', '/utf-8', "/Fo$Object", $Source) + $Extra)
    } else {
        Invoke-Checked cl.exe ($cxx + $Extra + @("/Fo$Object", $Source))
    }
    return $Object
}

$common = @(
    (Compile (Join-Path $native 'common.cpp') (Join-Path $obj 'common.obj')),
    (Compile (Join-Path $native 'archive.cpp') (Join-Path $obj 'archive.obj')),
    (Compile (Join-Path $native 'ui.cpp') (Join-Path $obj 'ui.obj')),
    (Compile (Join-Path $native 'third_party\miniz\miniz.c') (Join-Path $obj 'miniz.obj'))
)
$model = @(
    (Compile (Join-Path $native 'launcher_model.cpp') (Join-Path $obj 'launcher_model.obj')),
    (Compile (Join-Path $native 'launcher_session.cpp') (Join-Path $obj 'launcher_session.obj'))
)

$launcherObj = Compile (Join-Path $native 'launcher.cpp') (Join-Path $obj 'launcher.obj')
$settingsObj = Compile (Join-Path $native 'launcher.cpp') (Join-Path $obj 'launcher_settings_app.obj') @('/DNEBULA_SETTINGS_APP')
$launcherRes = Write-Resources 'Nebula' 'Nebula' 'Nebula.exe' ''
$settingsRes = Write-Resources 'NebulaSettings' 'Nebula settings' 'NebulaSettings.exe' ''
Invoke-Checked link.exe ($linkFlags + @("/OUT:$(Join-Path $OutputDirectory 'Nebula.exe')", $launcherObj, $launcherRes) + $common + $model)
Invoke-Checked link.exe ($linkFlags + @("/OUT:$(Join-Path $OutputDirectory 'NebulaSettings.exe')", $settingsObj, $settingsRes) + $common + $model)

if ($Payload) {
    $setupObjs = @(
        (Compile (Join-Path $native 'setup.cpp') (Join-Path $obj 'setup.obj')),
        (Compile (Join-Path $native 'setup_engine.cpp') (Join-Path $obj 'setup_engine.obj'))
    )
    $setupRes = Write-Resources 'Nebula-Setup' 'Nebula Setup' 'Nebula-Setup.exe' (Resolve-Path -LiteralPath $Payload).Path
    Invoke-Checked link.exe ($linkFlags + @("/OUT:$(Join-Path $OutputDirectory 'Nebula-Setup.exe')", $setupRes) + $setupObjs + $common)
}
