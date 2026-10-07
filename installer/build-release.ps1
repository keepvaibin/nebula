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
    # default is now AUTO, which runs the same CPUID + XGETBV probe the module
    # build uses and falls back to SSE2 when the build host cannot execute it.
    #
    # AUTO does NOT mean the produced package runs everywhere: the effective
    # value is recorded as the embedded runtime-isa.txt resource and Setup
    # refuses to install a package whose runtime needs instruction sets this PC
    # lacks, instead of installing a build that faults with an illegal
    # instruction later. Pass -NativeIsa SSE2 for a portable package.
    [ValidateSet('AUTO', 'SSE2', 'AVX2')][string]$NativeIsa = 'AUTO'
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

    # Shader-cache seed: warms a fresh install's first launch with DXBC pairs and
    # PSO keys captured on a reference machine. See
    # installer/shadercache-seed/README.md for provenance, and for why
    # `pipelines.bin` is deliberately never seeded (its loader checks the adapter
    # LUID, so a foreign copy would only be rejected).
    #
    # NOTE: `$src` is a `git archive` export of the revision being built, so these
    # two files MUST BE TRACKED or they are absent for every release while sitting
    # right there in a developer's working tree. That is why the missing case is a
    # hard failure with the fix in the message rather than a silent skip.
    $seedDir = Join-Path $src 'installer\shadercache-seed\RMGE01'
    $pipelineCache = Join-Path $src 'runtime\src\gx\pipeline_cache.cpp'
    foreach ($seedName in 'shaders.bin', 'psos.bin') {
        $seedPath = Join-Path $seedDir $seedName
        if (-not (Test-Path -LiteralPath $seedPath)) {
            throw ("Shader-cache seed is missing $seedName from the source export (looked in $seedPath). " +
                "This script builds from ``git archive $resolved``, which contains only tracked files, so add it with: " +
                "git add -f installer/shadercache-seed/RMGE01/$seedName")
        }
        # A seed whose header version does not match the runtime's constant is
        # rejected record-by-record on load, so shipping it silently buys nothing
        # while looking like coverage. Read the header's u32 version at offset 4
        # and compare against the constant the runtime actually compares with.
        $seedVersion = [BitConverter]::ToUInt32([IO.File]::ReadAllBytes($seedPath), 4)
        $constantName = if ($seedName -eq 'shaders.bin') { 'kShaderCacheVersion' } else { 'kPsoKeyCacheVersion' }
        $constantText = (Select-String -LiteralPath $pipelineCache `
                -Pattern "$constantName\s*=\s*([^;]+);" | Select-Object -First 1).Matches[0].Groups[1].Value
        $runtimeVersion = $null
        if ($constantText -match '^\s*(\d+)u?\s*$') {
            $runtimeVersion = [uint32]$Matches[1]
        }
        elseif ($constantText -match '0x100u?\s*\+\s*kShaderCacheVersion') {
            # Derived form, as the tree currently has it.
            $shaderText = (Select-String -LiteralPath $pipelineCache `
                    -Pattern 'kShaderCacheVersion\s*=\s*(\d+)u?\s*;' | Select-Object -First 1).Matches[0].Groups[1].Value
            $runtimeVersion = [uint32](0x100 + [uint32]$shaderText)
        }
        if ($null -eq $runtimeVersion) {
            throw "Could not determine $constantName from pipeline_cache.cpp; refusing to guess a seed version."
        }
        if ($seedVersion -ne $runtimeVersion) {
            if ($seedName -eq 'shaders.bin') {
                # shaders.bin is the valuable half -- it removes the FXC compile from
                # every covered first-use miss -- so a mismatch is a release blocker.
                throw "Shader-cache seed shaders.bin is version $seedVersion but the runtime expects $runtimeVersion. Regenerate it; see installer/shadercache-seed/README.md."
            }
            # psos.bin only feeds PSO prewarm. Omitting a stale one is better than
            # shipping a file the loader will discard.
            Write-Warning "Shader-cache seed psos.bin is version $seedVersion but the runtime expects $runtimeVersion; omitting it. Regenerate it to restore PSO prewarm coverage."
            continue
        }
        $resources["shadercache-seed.$seedName"] = $seedPath
        Write-Host "Shader-cache seed: $seedName version $seedVersion matches the runtime."
    }

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
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
