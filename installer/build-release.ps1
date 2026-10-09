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
    [string]$SigningKey = '',
    # Native ISA for the prebuilt runtime and the prebuilt float/DSP libraries
    # that every per-machine module links against.
    #
    # This used to be pinned to SSE2, which meant only the generated game module
    # got VEX/AVX2 code: NebulaRuntime.exe (GX parse, vertex decode, texture
    # decode, host HLE, audio) and galaxy_ppc_float/softfloat/dsp_alu were all
    # emitted for the 2004 x64 baseline even on hosts that can run AVX2. The
    # AUTO runs the same CPUID + XGETBV probe the module build uses and falls
    # back to SSE2 when the build host cannot execute it. Public releases default
    # to SSE2 so the publisher's CPU does not set the user's minimum requirement.
    #
    # AUTO does NOT mean the produced package runs everywhere: the effective
    # value is recorded as the embedded runtime-isa.txt resource and Setup
    # refuses to install a package whose runtime needs instruction sets this PC
    # lacks, instead of installing a build that faults with an illegal
    # instruction later. Pass -NativeIsa SSE2 for a portable package.
    [ValidateSet('AUTO', 'SSE2', 'AVX2')][string]$NativeIsa = 'SSE2'
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
    if ($type -eq 'tree') { $gitTree = $resolved }
    elseif ($type -eq 'commit') { $gitTree = (& git -C $repo show -s --format=%T $resolved).Trim() }
    else { throw "Release revision must identify a commit or tree, not $type" }
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

    # Cargo's native compression dependencies also need the compiler environment.
    . (Join-Path $src 'tools\enter_msvc_environment.ps1')

    # 2. nebula-recomp from the exported tree, statically linked C runtime.
    $cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (-not (Test-Path $cargo)) { $cargo = 'cargo' }
    $env:RUSTFLAGS = '-C target-feature=+crt-static'
    $env:CARGO_TARGET_DIR = Join-Path $work 'target'
    Push-Location $src
    try { Invoke-Checked $cargo @('build', '--release', '--locked', '-p', 'nebula-recomp') } finally { Pop-Location; Remove-Item Env:RUSTFLAGS }
    $recomp = Join-Path $env:CARGO_TARGET_DIR 'release\nebula-recomp.exe'

    # 3. Prebuilt runtime; the DSP is loaded from RMGE01_dsp.dll, which Setup compiles.
    $rt = Join-Path $work 'runtime'
    # Capture the configure log so the *effective* ISA that CMake selected can be
    # recorded. AUTO resolves against the build host, so the requested value is
    # not the shipped requirement.
    $configureLog = & cmake @('-S', $src, '-B', $rt, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DGALAXY_NATIVE_ISA=$NativeIsa", '-DNEBULA_DSP_MODULE=ON', "-DNEBULA_SOURCE_REVISION=$resolved", "-DNEBULA_SOURCE_REVISION_KIND=$type", "-DNEBULA_SOURCE_GIT_TREE=$gitTree", "-DNEBULA_SOURCE_TREE_SHA256=$treeHash", '-DNEBULA_SOURCE_DIRTY=0') 2>&1
    $configureLog | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed with exit code $LASTEXITCODE" }
    $effectiveIsa = $null
    foreach ($line in $configureLog) {
        if ("$line" -match 'Galaxy native CPU target:\s*([A-Za-z0-9]+)') { $effectiveIsa = $Matches[1].ToUpperInvariant() }
    }
    if ($effectiveIsa -ne 'SSE2' -and $effectiveIsa -ne 'AVX2') {
        # Never guess: an unknown effective ISA would let Setup check the wrong
        # requirement and hand the user a build that faults on first execution.
        throw "Could not determine the effective GALAXY_NATIVE_ISA from the CMake configure output."
    }
    Write-Host "Prebuilt runtime CPU target: $effectiveIsa (requested $NativeIsa)"
    $runtimeIsaPath = Join-Path $work 'runtime-isa.txt'
    [IO.File]::WriteAllText($runtimeIsaPath, $effectiveIsa, (New-Object Text.UTF8Encoding $false))
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
        'runtime-isa.txt' = $runtimeIsaPath
        'LICENSE' = (Join-Path $src 'LICENSE'); 'THIRD-PARTY-NOTICES.md' = (Join-Path $src 'THIRD-PARTY-NOTICES.md'); 'README.md' = (Join-Path $src 'README.md')
    }
    foreach ($pair in $runtimeFiles.GetEnumerator()) { $resources[$pair.Key] = $pair.Value }

    # Public releases follow docs/CONTENT_POLICY.md: observed shader caches,
    # driver pipeline libraries and all dump-derived modules remain local.
    # The runtime generates and persists its own cache on each user's PC.
    # Do not require untracked developer cache files in a git-archive build.

    $resourceArgs = @($resources.GetEnumerator() | ForEach-Object { "/resource:$($_.Value),$($_.Key)" })
    # Older preview updaters inspect every prerelease but recognize only the
    # standard asset name. Keep beta assets separate even for those clients.
    $setupName = if ($Version -match '-beta\.') { 'Nebula-Beta-Setup.exe' } else { 'Nebula-Setup.exe' }
    $setup = Join-Path $OutputDirectory $setupName
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
        runtimeNativeIsa = $effectiveIsa; requestedNativeIsa = $NativeIsa
        rustc = (& (Join-Path (Split-Path $cargo) 'rustc.exe') --version) 2>$null
    }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'build-info.json'), ($info | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
    Write-Host "Nebula-Setup.exe $setupHash"
} finally {
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $resolvedTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
    if (-not $resolvedWork.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolvedWork) -notmatch '^nebula-release-[0-9a-f]{32}$') {
        throw "Refusing cleanup outside the release's temporary directory."
    }
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force -ErrorAction SilentlyContinue
}
