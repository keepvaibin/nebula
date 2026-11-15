<#
.SYNOPSIS
Builds Nebula Setup: one native executable that carries the compiled game (Nebula.exe, smg_game.dll), the game
importer and the install root's Nebula.exe.

.DESCRIPTION
Builds nebula-import (crates/nebula-import) with Cargo and Setup and the install root's Nebula.exe (installer\native)
with the MSVC compiler: Win32 programs with the static C runtime, no .NET. Every program carries the Nebula icon (the
importer's too), its file details and, for the two windows programs, an application manifest. Setup embeds the files it
installs as one zip (the PAYLOAD resource: app\ is a version folder as installed, start\Nebula.exe the install root's
program) and the Setup rows of localization\NebulaText.tsv (the NEBULATEXT resource).

A first install packs the game from the player's disc image with nebula-import. An update keeps the packed game, the
saves and the settings and installs this version (installer\native\setup_engine.cpp).

  -Bin    the three renderers' programs for each CPU level, from a host build with NEBULA_VULKAN and NEBULA_D3D11
          and one more with NEBULA_HOST_BASELINE: Nebula.exe (Direct3D 12), NebulaVk.exe (Vulkan), NebulaD11.exe
          (Direct3D 11), and Nebula-x86-64-v2.exe, NebulaVk-x86-64-v2.exe, NebulaD11-x86-64-v2.exe; smg_game.dll,
          smg_game_plain.dll, x86-64-v2\smg_game.dll and x86-64-v2\smg_game_plain.dll (native\build.ps1 with both
          targets); D3D12\ (the Agility SDK's runtime the build put beside it); and glslang\glslang.exe and
          glslang\LICENSE.txt (the build's nebula_glslang target), installed as glslang\ beside dxc\ for LumaLink's
          shader check
  -Names  the game build folder of those binaries (the folder LumaLink's --names takes): its function rows (luma\),
          luma_types\luma_types.json, luma_functions.json, luma_numbers.json and the placement facts
          (smg_game.luma-placement.json, smg_game.luma-self.json). They are installed as luma\ beside the game, with the
          services list the game writes (--luma-services), where LumaLink reads the installed game's self-description.
          The game's SDK headers (native\include\luma*) go in luma\sdk
  -Dxc    Microsoft's DirectX Shader Compiler release, unpacked (github.com/microsoft/DirectXShaderCompiler): a mod's
          shaders are built with it on the player's PC
  -ShippedMods  a folder of the .luma files that come with the game (LumaKit, from LumaLink's `lumalink build` and
          `lumalink pack` of its lumakit folder): installed as luma\mods, where the game installs them into the
          player's mods folder at start

Requires Git, Python, Rust (stable, MSVC target) and Visual Studio 2022 with the C++ tools. Cargo builds into
CARGO_TARGET_DIR when it is set, else target\ in this tree.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$Bin,
    [Parameter(Mandatory = $true)][string]$Names,
    [Parameter(Mandatory = $true)][string]$Dxc,
    [Parameter(Mandatory = $true)][string]$ShippedMods,
    [string]$OutputDirectory = '',
    [string]$SetupName = '',
    [string]$Repository = 'keepvaibin/nebula',
    # Test builds only: Setup opens its first window off screen, saves it as setup-choose.bmp beside itself and exits.
    [switch]$UiSnapshots,
    # The release key's private half (RSA XML). Updates run only a Setup it signed; the default is the release key's
    # usual place. -SigningKey '' builds an unsigned Setup.
    [string]$SigningKey = (Join-Path $env:USERPROFILE '.nebula-release-key\nebula-release-private.xml')
)
$ErrorActionPreference = 'Stop'
$installer = Split-Path -Parent $PSCommandPath
$repo = Split-Path -Parent $installer
$native = Join-Path $installer 'native'
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist' }
if (-not $SetupName) { $SetupName = if ($Version -match '-beta\.') { 'Nebula-Beta-Setup.exe' } else { 'Nebula-Setup.exe' } }
if ($Version -notmatch '^\d+\.\d+\.\d+(-beta\.\d+)?$') { throw "The version must be MAJOR.MINOR.PATCH or MAJOR.MINOR.PATCH-beta.N: $Version" }
$work = Join-Path ([IO.Path]::GetTempPath()) ('nebula-native-' + [Guid]::NewGuid().ToString('N'))
$gen = Join-Path $work 'gen'
$obj = Join-Path $work 'obj'
New-Item -ItemType Directory -Force $work, $gen, $obj, $OutputDirectory | Out-Null
$utf8 = New-Object Text.UTF8Encoding $false

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    # The tools' output goes to the console, never into a function's result.
    & $File @Arguments | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

function Wide([string]$Text) { return 'L"' + $Text.Replace('\', '\\').Replace('"', '\"') + '"' }

# A resource script: the icon, the manifest (windows programs), a payload or text table, and the file details.
function Write-Resources([string]$Name, [string]$Description, [string]$OriginalName, [bool]$Manifest, [string[]]$Extra = @(), [int]$FileType = 1) {
    $rcPath = Join-Path $gen "$Name.rc"
    $icon = (Join-Path $installer 'assets\nebula.ico').Replace('\', '\\')
    $manifestLine = if ($Manifest) { "1 24 `"$((Join-Path $native 'app.manifest').Replace('\', '\\'))`"" } else { '' }
    [IO.File]::WriteAllText($rcPath, @"
1 ICON "$icon"
$manifestLine
$($Extra -join "`r`n")
1 VERSIONINFO
FILEVERSION $($numericVersion.Replace('.', ','))
PRODUCTVERSION $($numericVersion.Replace('.', ','))
FILEFLAGSMASK 0x3fL
FILEFLAGS 0x0L
FILEOS 0x40004L
FILETYPE 0x$($FileType.ToString('x'))L
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

try {
    # Every platform's release items (permissions, usage texts, file and link associations) are in the tree.
    Invoke-Checked python @((Join-Path $repo 'tools\check_platform_items.py'), $repo)
    $commit = (& git -C $repo rev-parse HEAD).Trim()
    if ((& git -C $repo status --porcelain).Length -ne 0) { Write-Warning 'The tree has uncommitted changes; the package records HEAD.' }
    foreach ($name in 'bin\x64\dxc.exe', 'bin\x64\dxcompiler.dll', 'bin\x64\dxil.dll', 'LICENSE-MS.txt', 'LICENCE-MIT.txt', 'LICENSE-LLVM.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $Dxc $name))) { throw "The compiler folder has no $name" }
    }
    foreach ($name in 'Nebula.exe', 'Nebula-x86-64-v2.exe', 'NebulaVk.exe', 'NebulaVk-x86-64-v2.exe', 'NebulaD11.exe', 'NebulaD11-x86-64-v2.exe', 'smg_game.dll', 'smg_game_plain.dll', 'x86-64-v2\smg_game.dll', 'x86-64-v2\smg_game_plain.dll', 'D3D12\D3D12Core.dll', 'D3D12\LICENSE.txt', 'glslang\glslang.exe', 'glslang\LICENSE.txt') {
        if (-not (Test-Path -LiteralPath (Join-Path $Bin $name))) { throw "The bin folder has no $name" }
    }
    # The game ships no test parts (tools/release_check.ps1): LumaLink's Setup installs the test kit.
    foreach ($name in 'nebula_test_extension.dll', 'luma-test.dll') {
        if (Test-Path -LiteralPath (Join-Path $Bin $name)) { Write-Warning "The bin folder's $name is a test part: Setup leaves it out." }
    }

    # The version folder as installed.
    $app = Join-Path $work 'payload\app'
    New-Item -ItemType Directory -Force $app | Out-Null
    foreach ($name in 'Nebula.exe', 'Nebula-x86-64-v2.exe', 'NebulaVk.exe', 'NebulaVk-x86-64-v2.exe', 'NebulaD11.exe', 'NebulaD11-x86-64-v2.exe', 'smg_game.dll', 'smg_game_plain.dll', 'x86-64-v2\smg_game.dll', 'x86-64-v2\smg_game_plain.dll', 'D3D12\D3D12Core.dll', 'D3D12\LICENSE.txt', 'glslang\glslang.exe', 'glslang\LICENSE.txt') {
        $to = Join-Path $app $name
        New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
        Copy-Item -LiteralPath (Join-Path $Bin $name) -Destination $to
    }
    New-Item -ItemType Directory -Force (Join-Path $app 'dxc') | Out-Null
    foreach ($name in 'bin\x64\dxc.exe', 'bin\x64\dxcompiler.dll', 'bin\x64\dxil.dll', 'LICENSE-MS.txt', 'LICENCE-MIT.txt', 'LICENSE-LLVM.txt') {
        Copy-Item -LiteralPath (Join-Path $Dxc $name) -Destination (Join-Path $app "dxc\$(Split-Path -Leaf $name)")
    }
    foreach ($name in 'LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md') { Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $app $name) }

    # The game's self-description, as LumaLink reads it from versions\<version>\luma: the rows, the type description,
    # the placement facts, and the services list this game module gives.
    $luma = Join-Path $app 'luma'
    New-Item -ItemType Directory -Force $luma | Out-Null
    foreach ($name in 'luma', 'luma_types\luma_types.json', 'luma_types\luma_functions.json', 'luma_types\luma_numbers.json', 'smg_game.luma-placement.json', 'smg_game.luma-self.json') {
        if (-not (Test-Path -LiteralPath (Join-Path $Names $name))) { throw "The names folder has no $name" }
    }
    Copy-Item -Recurse -Path (Join-Path $Names 'luma\*') -Destination $luma
    foreach ($name in 'luma_types\luma_types.json', 'luma_types\luma_functions.json', 'luma_types\luma_numbers.json', 'smg_game.luma-placement.json', 'smg_game.luma-self.json') {
        Copy-Item -LiteralPath (Join-Path $Names $name) -Destination $luma
    }
    # The services list: the build's (luma_types\luma_services.json), else the game module writes it.
    $services = Join-Path $Names 'luma_types\luma_services.json'
    if (Test-Path -LiteralPath $services) {
        Copy-Item -LiteralPath $services -Destination $luma
    } else {
        # The game module logs on stderr while it attaches.
        $ErrorActionPreference = 'Continue'
        try {
            Invoke-Checked (Join-Path $Bin 'Nebula.exe') @('--luma-services', (Join-Path $luma 'luma_services.json'), '--module', (Join-Path $Bin 'smg_game.dll'))
        } finally {
            $ErrorActionPreference = 'Stop'
        }
    }
    if (-not @(Get-ChildItem -LiteralPath $luma -Recurse -Filter '*.rows').Count) { throw 'The names folder has no function rows under luma\' }
    # The game's SDK: the headers and linker files a mod's code is built with, of this version.
    $sdk = Join-Path $luma 'sdk'
    New-Item -ItemType Directory -Force $sdk | Out-Null
    $sdkFiles = @(Get-ChildItem -LiteralPath (Join-Path $repo 'native\include') -File -Filter 'luma*')
    if (-not ($sdkFiles | Where-Object Name -eq 'luma.h')) { throw 'native\include has no luma.h' }
    foreach ($file in $sdkFiles) { Copy-Item -LiteralPath $file.FullName -Destination $sdk }
    # The mods that come with the game, beside its self-description (the game reads <program folder>\luma\mods).
    $shipped = @(Get-ChildItem -LiteralPath $ShippedMods -File -Filter '*.luma')
    if (-not $shipped.Count) { throw "The shipped mods folder has no .luma file: $ShippedMods" }
    New-Item -ItemType Directory -Force (Join-Path $luma 'mods') | Out-Null
    foreach ($file in $shipped) { Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $luma 'mods') }

    # build_info.h: what Setup and the install root's Nebula.exe know of this release.
    $numericVersion = (($Version -replace '-.*$', '') + '.0.0.0').Split('.')[0..3] -join '.'
    $publicKey = (Get-Content -LiteralPath (Join-Path $installer 'release-public-key.xml') -Raw).Trim()
    # The store version the importer writes and the game reads (one file for all of them).
    $storeVersion = [uint32](Get-Content -LiteralPath (Join-Path $repo 'runtime\include\galaxy\store_version.inc') -Raw).Trim()
    [IO.File]::WriteAllText((Join-Path $gen 'build_info.h'), @"
#pragma once
namespace nebula::build {
inline constexpr const wchar_t kVersion[] = $(Wide $Version);
inline constexpr const wchar_t kFileVersion[] = $(Wide $numericVersion);
inline constexpr const wchar_t kCommit[] = $(Wide $commit);
inline constexpr const wchar_t kRepository[] = $(Wide $Repository);
inline constexpr const wchar_t kReleasePublicKeyXml[] = $(Wide $publicKey);
inline constexpr unsigned kStoreVersion = $storeVersion;
}
"@, $utf8)

    # Setup's words: the table's header and its "L." rows (the game's rows stay in the game).
    $textTable = Join-Path $gen 'NebulaText.tsv'
    $rows = [IO.File]::ReadAllLines((Join-Path $repo 'localization\NebulaText.tsv'), $utf8)
    [IO.File]::WriteAllText($textTable, ((@($rows[0]) + @($rows | Where-Object { $_.StartsWith('L.') })) -join "`n"), $utf8)

    . (Join-Path $repo 'tools\enter_msvc_environment.ps1')

    # nebula-import with the C runtime linked in (Setup runs it on PCs without the Visual C++ runtime) and its file
    # details: the program (Setup's import and cook) and the library (the game cooks mods' files with it).
    $importRes = Write-Resources 'nebula-import' 'Nebula game importer' 'nebula-import.exe' $false
    $cookRes = Write-Resources 'nebula_import' 'Nebula game importer library' 'nebula_import.dll' $false @() 2
    $cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (-not (Test-Path -LiteralPath $cargo)) { $cargo = 'cargo' }
    Push-Location $repo
    # Cargo reports progress on stderr.
    $ErrorActionPreference = 'Continue'
    try {
        # The source paths they keep (panic texts) name no build machine's folders.
        $cargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path $env:USERPROFILE '.cargo' }
        $rustupHome = if ($env:RUSTUP_HOME) { $env:RUSTUP_HOME } else { Join-Path $env:USERPROFILE '.rustup' }
        $flags = @('-Ctarget-feature=+crt-static') + (@(@($cargoHome, 'cargo'), @($rustupHome, 'rustup'), @($repo, 'nebula')) |
            ForEach-Object { "--remap-path-prefix=$($_[0])=$($_[1])" })
        $env:CARGO_ENCODED_RUSTFLAGS = $flags -join [char]0x1f
        & $cargo rustc --profile dist --locked -p nebula-import --bin nebula-import -- "-Clink-arg=$importRes"
        if ($LASTEXITCODE -ne 0) { throw "cargo failed with exit code $LASTEXITCODE" }
        & $cargo rustc --profile dist --locked -p nebula-import --lib -- "-Clink-arg=$cookRes"
        if ($LASTEXITCODE -ne 0) { throw "cargo failed with exit code $LASTEXITCODE" }
    } finally {
        Remove-Item Env:CARGO_ENCODED_RUSTFLAGS -ErrorAction SilentlyContinue
        $ErrorActionPreference = 'Stop'
        Pop-Location
    }
    $targetDir = if ($env:CARGO_TARGET_DIR) { $env:CARGO_TARGET_DIR } else { Join-Path $repo 'target' }
    Copy-Item -LiteralPath (Join-Path $targetDir 'dist\nebula-import.exe') -Destination (Join-Path $app 'nebula-import.exe')
    Copy-Item -LiteralPath (Join-Path $targetDir 'dist\nebula_import.dll') -Destination (Join-Path $app 'nebula_import.dll')

    # The native programs. miniz (third_party/miniz) reads and writes Setup's zip files.
    $miniz = Join-Path $repo 'third_party\miniz'
    $cxx = @('/nologo', '/c', '/std:c++20', '/O2', '/MT', '/DNDEBUG', '/EHsc', '/W4', '/WX', '/permissive-', '/utf-8', '/Zc:__cplusplus', '/GS',
        '/guard:cf', '/Brepro', '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00', '/DWINVER=0x0A00', "/I$gen", "/I$native", "/I$miniz")
    if ($UiSnapshots) { $cxx += '/DNEBULA_UI_SNAPSHOTS' }
    $linkFlags = @('/nologo', '/SUBSYSTEM:WINDOWS', '/DYNAMICBASE', '/NXCOMPAT', '/HIGHENTROPYVA', '/guard:cf', '/OPT:REF', '/OPT:ICF',
        '/INCREMENTAL:NO', '/MANIFEST:NO', '/Brepro', '/RELEASE')
    $objects = @{}
    foreach ($name in 'common', 'archive', 'ui', 'text', 'setup', 'setup_engine', 'start') {
        $objects[$name] = Join-Path $obj "$name.obj"
        Invoke-Checked cl.exe ($cxx + @("/Fo$($objects[$name])", (Join-Path $native "$name.cpp")))
    }
    $objects['miniz'] = Join-Path $obj 'miniz.obj'
    Invoke-Checked cl.exe @('/nologo', '/c', '/O2', '/MT', '/DNDEBUG', '/W3', '/GS', '/guard:cf', '/Brepro', '/utf-8', "/Fo$($objects['miniz'])", (Join-Path $miniz 'miniz.c'))

    # The install root's Nebula.exe (it carries the text table for its one message).
    $textLine = "NEBULATEXT RCDATA `"$($textTable.Replace('\', '\\'))`""
    $startRes = Write-Resources 'Nebula' 'Nebula' 'Nebula.exe' $true @($textLine)
    $start = Join-Path $work 'payload\start\Nebula.exe'
    New-Item -ItemType Directory -Force (Split-Path -Parent $start) | Out-Null
    Invoke-Checked link.exe ($linkFlags + @("/OUT:$start", $startRes, $objects['start'], $objects['text'], $objects['common']))

    # The payload: one zip, entries named with '/', stored as they are where packing gains nothing.
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $payloadRoot = Join-Path $work 'payload'
    $payload = Join-Path $work 'payload.zip'
    $stream = [IO.File]::Open($payload, [IO.FileMode]::CreateNew)
    $zip = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $payloadRoot -Recurse -File) {
            $entry = $file.FullName.Substring($payloadRoot.Length + 1).Replace('\', '/')
            $level = if ($file.Extension -in '.luma', '.zip') { [IO.Compression.CompressionLevel]::NoCompression } else { [IO.Compression.CompressionLevel]::Optimal }
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entry, $level)
        }
    } finally {
        $zip.Dispose()
        $stream.Dispose()
    }

    $payloadLine = "PAYLOAD RCDATA `"$($payload.Replace('\', '\\'))`""
    $setupRes = Write-Resources 'Nebula-Setup' 'Nebula Setup' $SetupName $true @($payloadLine, $textLine)
    $setup = Join-Path $OutputDirectory $SetupName
    Invoke-Checked link.exe ($linkFlags + @("/OUT:$setup", $setupRes, $objects['setup'], $objects['setup_engine'], $objects['text'], $objects['common'], $objects['archive'], $objects['ui'], $objects['miniz']))

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
