<#
.SYNOPSIS
Builds Nebula Setup: the compiled game (NebulaNative.exe, smg_game.dll), the launcher, Nebula Settings and the game
importer, in one executable.

.DESCRIPTION
Builds nebula-import (crates/nebula-import) with Cargo, compiles the launcher, Nebula Settings and Setup from this tree
with the in-box C# compiler, and embeds them with the given game binaries. A first install packs the game from the
player's disc image with nebula-import. An update keeps the packed game, the saves and the settings, installs this
version and removes the recompiled game of earlier installs, its compiler and its source (InstallEngine.Install).

  -Bin  NebulaNative.exe, smg_game.dll and tables\*.tsv, as tested

Requires Git, Rust (stable, MSVC target) and Visual Studio 2022 with the C++ tools (nebula-import's compression
libraries are C). Cargo builds into CARGO_TARGET_DIR when it is set, else target\ in this tree.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Bin,
    [string]$OutputDirectory = '',
    [string]$SetupName = '',
    [string]$Repository = 'keepvaibin/nebula',
    # The release key's private half (RSA XML). Updates run only a Setup it signed; the default is the release key's
    # usual place. -SigningKey '' builds an unsigned Setup.
    [string]$SigningKey = (Join-Path $env:USERPROFILE '.nebula-release-key\nebula-release-private.xml')
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

    # nebula-import with the C runtime linked in: Setup runs it on PCs without the Visual C++ runtime.
    . (Join-Path $repo 'tools\enter_msvc_environment.ps1')
    $cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (-not (Test-Path -LiteralPath $cargo)) { $cargo = 'cargo' }
    Push-Location $repo
    # Cargo reports progress on stderr.
    $ErrorActionPreference = 'Continue'
    try {
        & $cargo build --release --locked -p nebula-import --bin nebula-import `
            --config "target.x86_64-pc-windows-msvc.rustflags=['-C','target-feature=+crt-static']"
        if ($LASTEXITCODE -ne 0) { throw "cargo failed with exit code $LASTEXITCODE" }
    } finally {
        $ErrorActionPreference = 'Stop'
        Pop-Location
    }
    $targetDir = if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { Join-Path $repo 'target' }
    $importer = Join-Path $targetDir 'release\nebula-import.exe'

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
    # The same program; it always opens the settings.
    $settingsApp = Join-Path $work 'NebulaSettings.exe'
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/define:NEBULA_SETTINGS_APP', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$settingsApp") + $references + $common + $launcherSources)

    $resources = [ordered]@{
        'NebulaNative.exe' = (Join-Path $Bin 'NebulaNative.exe'); 'smg_game.dll' = (Join-Path $Bin 'smg_game.dll')
        'material-names.tsv' = (Join-Path $Bin 'tables\material-names.tsv'); 'rt-materials.tsv' = (Join-Path $Bin 'tables\rt-materials.tsv')
        'rt-suns.tsv' = (Join-Path $Bin 'tables\rt-suns.tsv')
        'Nebula.exe' = $launcher; 'NebulaSettings.exe' = $settingsApp; 'nebula-import.exe' = $importer
        'LICENSE' = (Join-Path $repo 'LICENSE'); 'THIRD-PARTY-NOTICES.md' = (Join-Path $repo 'THIRD-PARTY-NOTICES.md'); 'README.md' = (Join-Path $repo 'README.md')
    }
    $resourceArgs = @($resources.GetEnumerator() | ForEach-Object { "/resource:$($_.Value),$($_.Key)" })
    $setup = Join-Path $OutputDirectory $SetupName
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$setup") + $references + $resourceArgs + $common +
        @(Get-ChildItem (Join-Path $installer 'src\Setup\*.cs') | ForEach-Object FullName))
    $hash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "$SetupName $Version ($commit) $hash"
    if ($SigningKey -and (Test-Path -LiteralPath $SigningKey)) {
        $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
        $rsa.PersistKeyInCsp = $false
        $rsa.FromXmlString((Get-Content -LiteralPath $SigningKey -Raw))
        if ($rsa.ToXmlString($false) -ne $publicKey) { throw 'The signing key does not match installer/release-public-key.xml.' }
        $signature = $rsa.SignData([IO.File]::ReadAllBytes($setup), [Security.Cryptography.CryptoConfig]::MapNameToOID('SHA256'))
        [IO.File]::WriteAllText("$setup.sig", [Convert]::ToBase64String($signature))
        Write-Host "Signed $SetupName with the release key."
    } elseif ($SigningKey) {
        Write-Warning "No release key at $SigningKey`: $SetupName is unsigned and updates will not run it."
    }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
