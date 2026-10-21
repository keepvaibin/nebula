<#
.SYNOPSIS
Builds a Nebula 2 setup: the game compiled (NebulaNative.exe, smg_game.dll), nothing recompiled on the player's PC.

.DESCRIPTION
Compiles the launcher, Nebula Settings and Setup from this tree with the in-box C# compiler and embeds the given
game binaries. On a PC with Nebula installed, Setup offers Update: it keeps the packed game, the saves and the
settings, installs this version, and removes the recompiled game, its compiler and its source (InstallEngine.InstallNative).
A first install packs the game from the player's disc image with nebula-recomp (package-content).

  -Bin     NebulaNative.exe, smg_game.dll and tables\*.tsv, as tested
  -Recomp  nebula-recomp.exe (game identification, extraction and packing for first installs)
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Bin,
    [Parameter(Mandatory = $true)][string]$Recomp,
    [string]$OutputDirectory = '',
    [string]$SetupName = '',
    [string]$Repository = 'keepvaibin/nebula'
)
$ErrorActionPreference = 'Stop'
$installer = Split-Path -Parent $PSCommandPath
$repo = Split-Path -Parent $installer
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist' }
if (-not $SetupName) { $SetupName = "Nebula-Setup-$Version.exe" }
$work = Join-Path ([IO.Path]::GetTempPath()) ('nebula-native-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work, $OutputDirectory | Out-Null

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

try {
    $commit = (& git -C $repo rev-parse HEAD).Trim()
    if ((& git -C $repo status --porcelain).Length -ne 0) { Write-Warning 'The tree has uncommitted changes; the package records HEAD.' }
    foreach ($name in 'NebulaNative.exe', 'smg_game.dll', 'tables\material-names.tsv', 'tables\rt-materials.tsv', 'tables\rt-suns.tsv') {
        if (-not (Test-Path -LiteralPath (Join-Path $Bin $name))) { throw "The bin folder has no $name" }
    }
    if (-not (Test-Path -LiteralPath $Recomp)) { throw "No nebula-recomp.exe at $Recomp" }

    $publicKey = (Get-Content -LiteralPath (Join-Path $installer 'release-public-key.xml') -Raw).Trim()
    $buildInfoPath = Join-Path $work 'BuildInfo.cs'
    [IO.File]::WriteAllText($buildInfoPath, @"
namespace Nebula
{
    internal static class BuildInfo
    {
        public const string Version = "$Version";
        public const string Commit = "$commit";
        public const string Repository = "$Repository";
        public const string ReleasePublicKeyXml = "$($publicKey.Replace('"', '\"'))";
    }
}
"@, (New-Object Text.UTF8Encoding $false))

    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    $common = @(Get-ChildItem (Join-Path $installer 'src\Common\*.cs') | ForEach-Object FullName) + $buildInfoPath
    $references = @('/r:System.Web.Extensions.dll', '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll',
        '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.Core.dll')
    $manifestFile = Join-Path $installer 'app.manifest'
    $iconFile = Join-Path $installer 'assets\nebula.ico'
    $launcherSources = @(Get-ChildItem (Join-Path $installer 'src\Launcher\*.cs') | ForEach-Object FullName)
    $launcher = Join-Path $work 'Nebula.exe'
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$launcher") + $references + $common + $launcherSources)
    $settingsApp = Join-Path $work 'NebulaSettings.exe'
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/define:NEBULA_SETTINGS_APP', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$settingsApp") + $references + $common + $launcherSources)

    # Setup reads a source manifest and the toolchain pins at start; a native package compiles nothing, so its manifest
    # lists no files. The host is built for AVX2 (Setup refuses CPUs without it).
    $sourceManifest = Join-Path $work 'source-manifest.json'
    [ordered]@{ schema = 'nebula.source.v1'; repository = $Repository; commit = $commit; version = $Version; treeSha256 = ''; files = @{} } |
        ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $sourceManifest -Encoding utf8
    $isa = Join-Path $work 'runtime-isa.txt'
    [IO.File]::WriteAllText($isa, 'AVX2', (New-Object Text.UTF8Encoding $false))

    $resources = [ordered]@{
        'NebulaNative.exe' = (Join-Path $Bin 'NebulaNative.exe'); 'smg_game.dll' = (Join-Path $Bin 'smg_game.dll')
        'material-names.tsv' = (Join-Path $Bin 'tables\material-names.tsv'); 'rt-materials.tsv' = (Join-Path $Bin 'tables\rt-materials.tsv')
        'rt-suns.tsv' = (Join-Path $Bin 'tables\rt-suns.tsv')
        'Nebula.exe' = $launcher; 'NebulaSettings.exe' = $settingsApp; 'nebula-recomp.exe' = $Recomp
        'source-manifest.json' = $sourceManifest; 'toolchain.json' = (Join-Path $installer 'toolchain.json'); 'runtime-isa.txt' = $isa
        'LICENSE' = (Join-Path $repo 'LICENSE'); 'THIRD-PARTY-NOTICES.md' = (Join-Path $repo 'THIRD-PARTY-NOTICES.md'); 'README.md' = (Join-Path $repo 'README.md')
    }
    $resourceArgs = @($resources.GetEnumerator() | ForEach-Object { "/resource:$($_.Value),$($_.Key)" })
    $setup = Join-Path $OutputDirectory $SetupName
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$setup") + $references + $resourceArgs + $common +
        @(Get-ChildItem (Join-Path $installer 'src\Setup\*.cs') | ForEach-Object FullName))
    $hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "$SetupName $Version ($commit) $hash"
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
