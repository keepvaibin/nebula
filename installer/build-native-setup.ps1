<#
.SYNOPSIS
Builds Nebula Setup: the compiled game (NebulaNative.exe, smg_game.dll), the launcher, Nebula Settings and the game
importer, in one executable.

.DESCRIPTION
Builds nebula-import (crates/nebula-import) with Cargo, compiles the launcher, Nebula Settings and Setup from this tree
with the in-box C# compiler, and embeds them with the given game binaries. A first install packs the game from the
player's disc image with nebula-import. An update keeps the packed game, the saves and the settings, installs this
version and removes the recompiled game of earlier installs, its compiler and its source (InstallEngine.Install).

  -Bin    NebulaNative.exe, smg_game.dll, smg_game_plain.dll and D3D12\ (the Agility SDK's runtime the build put
          beside it), as tested; tables\material-names.tsv, rt-materials.tsv and rt-suns.tsv (the ray tracer's tables,
          made by nebula-import rt-materials on the ray tracing branch) when there: Setup installs without them else
  -Names  the game build folder of those binaries (the folder LumaLink's --names takes): its function rows (luma\),
          luma_types\luma_types.json, luma_functions.json, luma_numbers.json and the placement facts
          (smg_game.luma-placement.json, smg_game.luma-self.json). They are installed as luma\ beside the game, with the
          services list the game writes (--luma-services), where LumaLink reads the installed game's self-description
  -Dxc    Microsoft's DirectX Shader Compiler release, unpacked (github.com/microsoft/DirectXShaderCompiler): a mod's
          shaders are built with it on the player's PC

Requires Git, Rust (stable, MSVC target) and Visual Studio 2022 with the C++ tools (nebula-import's compression
libraries are C). Cargo builds into CARGO_TARGET_DIR when it is set, else target\ in this tree.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Bin,
    [Parameter(Mandatory = $true)][string]$Names,
    [Parameter(Mandatory = $true)][string]$Dxc,
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
    # Every platform's release items (permissions, usage texts, file and link associations) are in the tree.
    Invoke-Checked python @((Join-Path $repo 'tools\check_platform_items.py'), $repo)
    $commit = (& git -C $repo rev-parse HEAD).Trim()
    foreach ($name in 'bin\x64\dxc.exe', 'bin\x64\dxcompiler.dll', 'bin\x64\dxil.dll', 'LICENSE-MS.txt', 'LICENCE-MIT.txt', 'LICENSE-LLVM.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $Dxc $name))) { throw "The compiler folder has no $name" }
    }
    if ((& git -C $repo status --porcelain).Length -ne 0) { Write-Warning 'The tree has uncommitted changes; the package records HEAD.' }
    foreach ($name in 'NebulaNative.exe', 'smg_game.dll', 'smg_game_plain.dll', 'D3D12\D3D12Core.dll', 'D3D12\LICENSE.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $Bin $name))) { throw "The bin folder has no $name" }
    }
    $rtTables = @()
    foreach ($name in 'material-names.tsv', 'rt-materials.tsv', 'rt-suns.tsv') {
        if (Test-Path -LiteralPath (Join-Path $Bin "tables\$name")) { $rtTables += $name }
        else { Write-Warning "The bin folder has no tables\$name (a ray tracer's table): Setup installs without it." }
    }

    # The game's self-description, as LumaLink reads it from versions\<version>\luma: the rows, the type description,
    # the placement facts, and the services list this game module gives.
    $luma = Join-Path $work 'luma'
    New-Item -ItemType Directory -Force $luma | Out-Null
    foreach ($name in 'luma', 'luma_types\luma_types.json', 'luma_types\luma_functions.json', 'luma_types\luma_numbers.json', 'smg_game.luma-placement.json', 'smg_game.luma-self.json') {
        if (-not (Test-Path -LiteralPath (Join-Path $Names $name))) { throw "The names folder has no $name" }
    }
    Copy-Item -Recurse -Path (Join-Path $Names 'luma\*') -Destination $luma
    foreach ($name in 'luma_types\luma_types.json', 'luma_types\luma_functions.json', 'luma_types\luma_numbers.json', 'smg_game.luma-placement.json', 'smg_game.luma-self.json') {
        Copy-Item -LiteralPath (Join-Path $Names $name) -Destination $luma
    }
    # The game module logs on stderr while it attaches.
    $ErrorActionPreference = 'Continue'
    try {
        Invoke-Checked (Join-Path $Bin 'NebulaNative.exe') @('--luma-services', (Join-Path $luma 'luma_services.json'), '--module', (Join-Path $Bin 'smg_game.dll'))
    } finally {
        $ErrorActionPreference = 'Stop'
    }
    if (-not @(Get-ChildItem -LiteralPath $luma -Recurse -Filter '*.rows').Count) { throw 'The names folder has no function rows under luma\' }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $lumaZip = Join-Path $work 'luma.zip'
    [IO.Compression.ZipFile]::CreateFromDirectory($luma, $lumaZip, [IO.Compression.CompressionLevel]::Optimal, $false)

    # nebula-import with the C runtime linked in: Setup runs it on PCs without the Visual C++ runtime.
    . (Join-Path $repo 'tools\enter_msvc_environment.ps1')
    $cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (-not (Test-Path -LiteralPath $cargo)) { $cargo = 'cargo' }
    Push-Location $repo
    # Cargo reports progress on stderr.
    $ErrorActionPreference = 'Continue'
    try {
        # The program (Setup's import and cook) and the library (the game cooks mods' files with it).
        & $cargo build --release --locked -p nebula-import --bin nebula-import --lib `
            --config "target.x86_64-pc-windows-msvc.rustflags=['-C','target-feature=+crt-static']"
        if ($LASTEXITCODE -ne 0) { throw "cargo failed with exit code $LASTEXITCODE" }
    } finally {
        $ErrorActionPreference = 'Stop'
        Pop-Location
    }
    $targetDir = if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { Join-Path $repo 'target' }
    $importer = Join-Path $targetDir 'release\nebula-import.exe'
    $cooker = Join-Path $targetDir 'release\nebula_import.dll'

    $publicKey = (Get-Content -LiteralPath (Join-Path $installer 'release-public-key.xml') -Raw).Trim()
    # The store version the importer writes and the game reads (one file for all of them).
    $storeVersion = [uint32](Get-Content -LiteralPath (Join-Path $repo 'runtime\include\galaxy\store_version.inc') -Raw).Trim()
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
        public const uint StoreVersion = $storeVersion;
    }
}
"@, (New-Object Text.UTF8Encoding $false))

    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    $common = @(Get-ChildItem (Join-Path $installer 'src\Common\*.cs') | ForEach-Object FullName) + $buildInfoPath
    $textTable = "/resource:$(Join-Path $repo 'localization\NebulaText.tsv'),NebulaText.tsv"
    $references = @('/r:System.Web.Extensions.dll', '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll',
        '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.Core.dll')
    $manifestFile = Join-Path $installer 'app.manifest'
    $iconFile = Join-Path $installer 'assets\nebula.ico'
    $launcherSources = @(Get-ChildItem (Join-Path $installer 'src\Launcher\*.cs') | ForEach-Object FullName)
    $launcher = Join-Path $work 'Nebula.exe'
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$launcher") + $references + $textTable + $common + $launcherSources)
    # The same program; it always opens the settings.
    $settingsApp = Join-Path $work 'NebulaSettings.exe'
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', '/define:NEBULA_SETTINGS_APP', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$settingsApp") + $references + $textTable + $common + $launcherSources)

    $resources = [ordered]@{
        'NebulaNative.exe' = (Join-Path $Bin 'NebulaNative.exe'); 'smg_game.dll' = (Join-Path $Bin 'smg_game.dll')
        'smg_game_plain.dll' = (Join-Path $Bin 'smg_game_plain.dll'); 'luma.zip' = $lumaZip
        'D3D12\D3D12Core.dll' = (Join-Path $Bin 'D3D12\D3D12Core.dll'); 'D3D12\LICENSE.txt' = (Join-Path $Bin 'D3D12\LICENSE.txt')
        'Nebula.exe' = $launcher; 'NebulaSettings.exe' = $settingsApp; 'nebula-import.exe' = $importer
        'nebula_import.dll' = $cooker
        'dxc\dxc.exe' = (Join-Path $Dxc 'bin\x64\dxc.exe'); 'dxc\dxcompiler.dll' = (Join-Path $Dxc 'bin\x64\dxcompiler.dll'); 'dxc\dxil.dll' = (Join-Path $Dxc 'bin\x64\dxil.dll')
        'dxc\LICENSE-MS.txt' = (Join-Path $Dxc 'LICENSE-MS.txt'); 'dxc\LICENCE-MIT.txt' = (Join-Path $Dxc 'LICENCE-MIT.txt'); 'dxc\LICENSE-LLVM.txt' = (Join-Path $Dxc 'LICENSE-LLVM.txt')
        'LICENSE' = (Join-Path $repo 'LICENSE'); 'THIRD-PARTY-NOTICES.md' = (Join-Path $repo 'THIRD-PARTY-NOTICES.md'); 'README.md' = (Join-Path $repo 'README.md')
    }
    foreach ($name in $rtTables) { $resources[$name] = Join-Path $Bin "tables\$name" }
    $resourceArgs = @($resources.GetEnumerator() | ForEach-Object { "/resource:$($_.Value),$($_.Key)" })
    $setup = Join-Path $OutputDirectory $SetupName
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$setup") + $references + $textTable + $resourceArgs + $common +
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
