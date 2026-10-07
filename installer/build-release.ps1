<#
.SYNOPSIS
Builds Nebula-Setup.exe from one exact source revision.

.DESCRIPTION
Exports the revision with `git archive` and records each file's SHA-256
(Setup checks its GitHub download against it), builds nebula-recomp and the
runtime from that export, then compiles the launcher and Setup with the
in-box C# compiler and embeds everything. -SigningKey also writes
Nebula-Setup.exe.sig for the updater.

Requirements: Git, Rust (stable, MSVC target) and Visual Studio 2022 with
the C++ tools. Setup needs none of these.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Version,
    # A commit for releases. A tree id (git write-tree) is accepted for local
    # pre-release testing with Nebula-Setup.exe --source.
    [string]$Revision = 'HEAD',
    [string]$Repository = 'keepvaibin/nebula',
    [string]$OutputDirectory = '',
    [string]$SigningKey = ''
)
$ErrorActionPreference = 'Stop'
$installer = Split-Path -Parent $PSCommandPath
$repo = Split-Path -Parent $installer
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist' }
$work = Join-Path ([IO.Path]::GetTempPath()) ('nebula-release-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work, $OutputDirectory | Out-Null

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

try {
    $resolved = (& git -C $repo rev-parse $Revision).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Unknown revision $Revision" }
    $type = (& git -C $repo cat-file -t $resolved).Trim()
    $gitTree = (& git -C $repo rev-parse "$resolved^{tree}").Trim()
    if ($LASTEXITCODE -ne 0) { throw "Source revision $resolved has no Git tree" }
    Write-Host "Building Nebula $Version from $type $resolved"

    # 1. Exact source export and manifest.
    $zip = Join-Path $OutputDirectory ("nebula-source-" + $resolved.Substring(0, 12) + '.zip')
    Invoke-Checked git @('-C', $repo, '-c', 'core.autocrlf=false', '-c', 'core.eol=lf', 'archive', '--format=zip', "--prefix=nebula-$resolved/", '-o', $zip, $resolved)
    $src = Join-Path $work 'src'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $src)
    $src = Join-Path $src "nebula-$resolved"
    $files = [ordered]@{}
    $tree = New-Object Text.StringBuilder
    foreach ($file in (Get-ChildItem -LiteralPath $src -Recurse -File | Sort-Object { $_.FullName.Substring($src.Length + 1).Replace('\', '/') } -CaseSensitive)) {
        $relative = $file.FullName.Substring($src.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $files[$relative] = $hash
        [void]$tree.Append($relative).Append([char]0).Append($hash).Append("`n")
    }
    $treeHash = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash([Text.Encoding]::UTF8.GetBytes($tree.ToString()))).Replace('-', '').ToLowerInvariant()
    $manifest = [ordered]@{ schema = 'nebula.source.v1'; repository = $Repository; commit = $resolved; version = $Version; treeSha256 = $treeHash; files = $files }
    $manifestPath = Join-Path $work 'source-manifest.json'
    [IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
    Write-Host ("Source manifest: {0} files, tree {1}" -f $files.Count, $treeHash)

    # 2. nebula-recomp from the exported tree, statically linked C runtime.
    $cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (-not (Test-Path $cargo)) { $cargo = 'cargo' }
    $env:RUSTFLAGS = '-C target-feature=+crt-static'
    $env:CARGO_TARGET_DIR = Join-Path $work 'target'
    Push-Location $src
    try { Invoke-Checked $cargo @('build', '--release', '--locked', '-p', 'nebula-recomp') } finally { Pop-Location; Remove-Item Env:RUSTFLAGS }
    $recomp = Join-Path $env:CARGO_TARGET_DIR 'release\nebula-recomp.exe'

    # 3. Prebuilt runtime; the DSP is loaded from RMGE01_dsp.dll, which Setup compiles.
    . (Join-Path $src 'tools\enter_msvc_environment.ps1')
    $rt = Join-Path $work 'runtime'
    Invoke-Checked cmake @('-S', $src, '-B', $rt, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DGALAXY_NATIVE_ISA=SSE2', '-DNEBULA_DSP_MODULE=ON', "-DNEBULA_SOURCE_REVISION=$resolved", "-DNEBULA_SOURCE_REVISION_KIND=$type", "-DNEBULA_SOURCE_GIT_TREE=$gitTree", "-DNEBULA_SOURCE_TREE_SHA256=$treeHash", '-DNEBULA_SOURCE_DIRTY=0')
    Invoke-Checked cmake @('--build', $rt, '--target', 'NebulaRuntime', 'galaxy_ppc_float', 'galaxy_softfloat', 'galaxy_dsp_alu')
    $runtimeFiles = @{}
    foreach ($name in 'NebulaRuntime.exe', 'galaxy_ppc_float.lib', 'galaxy_softfloat.lib', 'galaxy_dsp_alu.lib') {
        $file = Get-ChildItem -LiteralPath $rt -Recurse -File -Filter $name | Select-Object -First 1
        if (-not $file) { throw "The runtime build produced no $name" }
        $runtimeFiles[$name] = $file.FullName
    }

    # 4. Build information and C# executables.
    $publicKey = (Get-Content -LiteralPath (Join-Path $src 'installer\release-public-key.xml') -Raw).Trim()
    $buildInfo = @"
namespace Nebula
{
    internal static class BuildInfo
    {
        public const string Version = "$Version";
        public const string Commit = "$resolved";
        public const string Repository = "$Repository";
        public const string ReleasePublicKeyXml = "$($publicKey.Replace('"', '\"'))";
    }
}
"@
    $buildInfoPath = Join-Path $work 'BuildInfo.cs'
    [IO.File]::WriteAllText($buildInfoPath, $buildInfo, (New-Object Text.UTF8Encoding $false))
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
    $common = @(Get-ChildItem (Join-Path $src 'installer\src\Common\*.cs') | ForEach-Object FullName) + $buildInfoPath
    $references = @('/r:System.Web.Extensions.dll', '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll',
        '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.Core.dll')
    $manifestFile = Join-Path $src 'installer\app.manifest'
    $launcher = Join-Path $work 'Nebula.exe'
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/out:$launcher") + $references + $common +
        @(Get-ChildItem (Join-Path $src 'installer\src\Launcher\*.cs') | ForEach-Object FullName))

    $resources = @{
        'nebula-recomp.exe' = $recomp; 'Nebula.exe' = $launcher; 'source-manifest.json' = $manifestPath
        'toolchain.json' = (Join-Path $src 'installer\toolchain.json'); 'runtime-env.json' = (Join-Path $src 'installer\runtime-env.json')
        'LICENSE' = (Join-Path $src 'LICENSE'); 'THIRD-PARTY-NOTICES.md' = (Join-Path $src 'THIRD-PARTY-NOTICES.md'); 'README.md' = (Join-Path $src 'README.md')
    }
    foreach ($pair in $runtimeFiles.GetEnumerator()) { $resources[$pair.Key] = $pair.Value }
    $resourceArgs = @($resources.GetEnumerator() | ForEach-Object { "/resource:$($_.Value),$($_.Key)" })
    $setup = Join-Path $OutputDirectory 'Nebula-Setup.exe'
    Invoke-Checked $csc (@('/nologo', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/out:$setup") + $references + $resourceArgs + $common +
        @(Get-ChildItem (Join-Path $src 'installer\src\Setup\*.cs') | ForEach-Object FullName))

    # 5. Signature and checksums.
    $setupHash = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($SigningKey) {
        $rsa = New-Object Security.Cryptography.RSACryptoServiceProvider
        $rsa.PersistKeyInCsp = $false
        $rsa.FromXmlString((Get-Content -LiteralPath $SigningKey -Raw))
        if ($rsa.ToXmlString($false) -ne $publicKey) { throw 'The signing key does not match installer/release-public-key.xml.' }
        $bytes = [IO.File]::ReadAllBytes($setup)
        $signature = $rsa.SignData($bytes, [Security.Cryptography.CryptoConfig]::MapNameToOID('SHA256'))
        [IO.File]::WriteAllText("$setup.sig", [Convert]::ToBase64String($signature))
        Write-Host 'Signed Nebula-Setup.exe with the release key.'
    }
    $info = [ordered]@{
        version = $Version; revision = $resolved; revisionType = $type; repository = $Repository
        setupSha256 = $setupHash; nebulaRecompSha256 = (Get-FileHash -LiteralPath $recomp -Algorithm SHA256).Hash.ToLowerInvariant()
        launcherSha256 = (Get-FileHash -LiteralPath $launcher -Algorithm SHA256).Hash.ToLowerInvariant()
        sourceTreeSha256 = $treeHash; sourceZip = (Split-Path -Leaf $zip); builtUtc = [DateTime]::UtcNow.ToString('o')
        rustc = (& (Join-Path (Split-Path $cargo) 'rustc.exe') --version) 2>$null
    }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'build-info.json'), ($info | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
    Write-Host "Nebula-Setup.exe $setupHash"
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
