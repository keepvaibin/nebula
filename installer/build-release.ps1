<#
.SYNOPSIS
Builds Nebula-Setup.exe from one exact source revision.

.DESCRIPTION
Exports the revision with `git archive` and records each file's SHA-256
(Setup checks the source it installs against it), builds nebula-recomp and the
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
    # Instruction set of the prebuilt runtime and of the float/DSP libraries that each installed
    # game module links against. BOTH (the default) embeds an SSE2 and an AVX2 build, and Setup
    # installs the AVX2 one on CPUs that support it. AUTO picks AVX2 when the build PC supports it
    # and SSE2 otherwise. The choice is recorded in the embedded runtime-isa.txt, and Setup refuses
    # a package whose runtime needs instructions the PC lacks.
    [ValidateSet('AUTO', 'SSE2', 'AVX2', 'BOTH')][string]$NativeIsa = 'BOTH'
)
$ErrorActionPreference = 'Stop'
$installer = Split-Path -Parent $PSCommandPath
$repo = Split-Path -Parent $installer
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repo 'dist' }
$work = Join-Path ([IO.Path]::GetTempPath()) ('nebula-release-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work, $OutputDirectory | Out-Null

# Microsoft Defender's verdict on each file (a custom scan without remediation, the current signatures and cloud
# protection): any detection stops the build with its name.
function Assert-DefenderClean([string[]]$Files) {
    $platform = Get-ChildItem 'C:\ProgramData\Microsoft\Windows Defender\Platform' -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    $scanner = if ($platform) { Join-Path $platform.FullName 'MpCmdRun.exe' } else { Join-Path $env:ProgramFiles 'Windows Defender\MpCmdRun.exe' }
    if (-not (Test-Path -LiteralPath $scanner)) { throw 'Microsoft Defender (MpCmdRun.exe) is required to build a release.' }
    foreach ($file in $Files) {
        $output = & $scanner -Scan -ScanType 3 -File $file -DisableRemediation 2>&1
        if ($LASTEXITCODE -ne 0 -or -not ($output -match 'found no threats')) {
            throw ("Microsoft Defender flagged {0}: {1}" -f $file, (($output | Out-String).Trim()))
        }
        Write-Host "Defender: no threats in $(Split-Path -Leaf $file)"
    }
}

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

    # 1. Exact source export and manifest. Setup carries the zip.
    $zip = Join-Path $work 'nebula-source.zip'
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

    # No C runtime fma in the runtime or module headers: it is a soft-float routine on CPUs
    # without FMA3 (galaxy/fused_multiply_add.h). Tests may use it as the reference.
    $fmaCalls = @(Get-ChildItem -LiteralPath (Join-Path $src 'runtime') -Recurse -File -Include *.cpp, *.h, *.inl |
        Where-Object { $_.Name -notlike '*_tests.cpp' -and $_.Name -ne 'fused_multiply_add.h' } |
        Select-String -Pattern '(std::|[^\w.])fmaf?\s*\(')
    if ($fmaCalls.Count -ne 0) {
        throw ("std::fma/fmaf in the runtime; use galaxy::fused_multiply_add: " + (($fmaCalls | ForEach-Object { "$($_.Path):$($_.LineNumber)" }) -join ', '))
    }

    # Cargo's native compression dependencies also need the compiler environment.
    . (Join-Path $src 'tools\enter_msvc_environment.ps1')

    # 2. nebula-recomp from the exported tree, statically linked C runtime.
    $cargo = Join-Path $env:USERPROFILE '.cargo\bin\cargo.exe'
    if (-not (Test-Path $cargo)) { $cargo = 'cargo' }
    # File details (VERSIONINFO) for nebula-recomp.exe, linked in as a compiled resource: a program without them is
    # one more trait antivirus models weigh against unknown files.
    $numericVersion = (($Version -replace '-.*$', '') + '.0.0.0').Split('.')[0..3] -join '.'
    $recompRc = Join-Path $work 'nebula-recomp.rc'
    $recompRes = Join-Path $work 'nebula-recomp.res'
    [IO.File]::WriteAllText($recompRc, @"
1 ICON "$((Join-Path $src 'installer\assets\nebula.ico').Replace('\', '\\'))"
1 VERSIONINFO
FILEVERSION $($numericVersion.Replace('.', ','))
PRODUCTVERSION $($numericVersion.Replace('.', ','))
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "040904b0"
    BEGIN
      VALUE "CompanyName", "keepvaibin"
      VALUE "FileDescription", "Nebula game converter (used by Nebula Setup)"
      VALUE "FileVersion", "$Version"
      VALUE "LegalCopyright", "Copyright 2026 keepvaibin"
      VALUE "OriginalFilename", "nebula-recomp.exe"
      VALUE "ProductName", "Nebula"
      VALUE "ProductVersion", "$Version"
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x409, 1200
  END
END
"@, (New-Object Text.UTF8Encoding $false))
    Invoke-Checked rc.exe @('/nologo', "/fo$recompRes", $recompRc)
    $env:RUSTFLAGS = "-C target-feature=+crt-static -C link-arg=$recompRes"
    $env:CARGO_TARGET_DIR = Join-Path $work 'target'
    Push-Location $src
    try { Invoke-Checked $cargo @('build', '--release', '--locked', '-p', 'nebula-recomp') } finally { Pop-Location; Remove-Item Env:RUSTFLAGS }
    $recomp = Join-Path $env:CARGO_TARGET_DIR 'release\nebula-recomp.exe'

    # 3. Prebuilt runtime; the DSP is loaded from RMGE01_dsp.dll, which Setup compiles.
    # BOTH builds it twice: SSE2 under the plain resource names, AVX2 under
    # "avx2/". Each configure log is captured so the *effective* ISA CMake
    # selected is recorded; AUTO resolves against the build host.
    $runtimeFiles = @{}
    $isaBuilds = if ($NativeIsa -eq 'BOTH') { @('SSE2', 'AVX2') } else { @($NativeIsa) }
    $effectiveIsas = @()
    foreach ($isa in $isaBuilds) {
        $rt = Join-Path $work ('runtime-' + $isa.ToLowerInvariant())
        $configureLog = & cmake @('-S', $src, '-B', $rt, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DGALAXY_NATIVE_ISA=$isa", '-DNEBULA_DSP_MODULE=ON', '-DGALAXY_FIDELITYFX_SDK=', '-DGALAXY_WITH_STREAMLINE_BRIDGE=OFF', "-DNEBULA_SOURCE_REVISION=$resolved", "-DNEBULA_SOURCE_REVISION_KIND=$type", "-DNEBULA_SOURCE_GIT_TREE=$gitTree", "-DNEBULA_SOURCE_TREE_SHA256=$treeHash", '-DNEBULA_SOURCE_DIRTY=0') 2>&1
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
        if ($NativeIsa -eq 'BOTH' -and $effectiveIsa -ne $isa) { throw "The $isa runtime configured as $effectiveIsa." }
        Write-Host "Prebuilt runtime CPU target: $effectiveIsa (requested $isa)"
        $effectiveIsas += $effectiveIsa
        Invoke-Checked cmake @('--build', $rt, '--target', 'NebulaRuntime', 'galaxy_ppc_float', 'galaxy_softfloat', 'galaxy_dsp_alu')
        $prefix = if ($NativeIsa -eq 'BOTH' -and $isa -eq 'AVX2') { 'avx2/' } else { '' }
        foreach ($name in 'NebulaRuntime.exe', 'galaxy_ppc_float.lib', 'galaxy_softfloat.lib', 'galaxy_dsp_alu.lib') {
            $file = Get-ChildItem -LiteralPath $rt -Recurse -File -Filter $name | Select-Object -First 1
            if (-not $file) { throw "The runtime build produced no $name" }
            $runtimeFiles[$prefix + $name] = $file.FullName
        }
    }
    $runtimeIsaPath = Join-Path $work 'runtime-isa.txt'
    [IO.File]::WriteAllText($runtimeIsaPath, ($effectiveIsas -join '+'), (New-Object Text.UTF8Encoding $false))

    # 4. Build information and C# executables.
    $publicKey = (Get-Content -LiteralPath (Join-Path $src 'installer\release-public-key.xml') -Raw).Trim()
    $buildInfo = @"
namespace Nebula
{
    internal static class BuildInfo
    {
        public const string Version = "$Version";
        public const string FileVersion = "$numericVersion";
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
    $iconFile = Join-Path $src 'installer\assets\nebula.ico'
    $launcherSources = @(Get-ChildItem (Join-Path $src 'installer\src\Launcher\*.cs') | ForEach-Object FullName)
    $launcher = Join-Path $work 'Nebula.exe'
    Invoke-Checked $csc (@('/nologo', '/deterministic', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$launcher") + $references + $common +
        $launcherSources)
    # Same program; always opens the settings window instead of the game.
    $settingsApp = Join-Path $work 'NebulaSettings.exe'
    Invoke-Checked $csc (@('/nologo', '/deterministic', '/target:winexe', '/platform:x64', '/optimize+', '/define:NEBULA_SETTINGS_APP', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$settingsApp") + $references + $common +
        $launcherSources)

    $resources = @{
        'nebula-recomp.exe' = $recomp; 'Nebula.exe' = $launcher; 'NebulaSettings.exe' = $settingsApp; 'source-manifest.json' = $manifestPath
        'toolchain.json' = (Join-Path $src 'installer\toolchain.json'); 'runtime-env.json' = (Join-Path $src 'installer\runtime-env.json')
        'runtime-isa.txt' = $runtimeIsaPath
        'LICENSE' = (Join-Path $src 'LICENSE'); 'THIRD-PARTY-NOTICES.md' = (Join-Path $src 'THIRD-PARTY-NOTICES.md'); 'README.md' = (Join-Path $src 'README.md')
        # The exact source (SourceTree.EmbeddedArchiveName): Setup never needs GitHub for it.
        'nebula-source.zip' = $zip
    }
    $apiText = Get-Content -LiteralPath (Join-Path $src 'runtime/include/galaxy/native_api.h') -Raw
    $dspText = Get-Content -LiteralPath (Join-Path $src 'runtime/include/galaxy/dsp_context.h') -Raw
    $nativeAbi = [regex]::Match($apiText, 'kNativeAbiVersion\s*=\s*(\d+)').Groups[1].Value
    $rsoAbi = [regex]::Match($apiText, 'kNativeRsoModuleAbiVersion\s*=\s*(\d+)').Groups[1].Value
    $dspVersion = [regex]::Match($dspText, 'kDspGeneratedProbeContractVersion\s*=\s*(\d+)').Groups[1].Value
    if (-not $nativeAbi -or -not $rsoAbi -or -not $dspVersion -or
        $dspText -notmatch 'kDspGeneratedProbeCapabilitySelectionPublication\s*=\s*1u << 0u' -or
        $dspText -notmatch 'kDspGeneratedProbeCapabilitySelectedChannelBranch\s*=\s*1u << 1u' -or
        $dspText -notmatch 'kDspGeneratedProbeRequiredCapabilities\s*=\s*kDspGeneratedProbeCapabilitySelectionPublication\s*\|\s*kDspGeneratedProbeCapabilitySelectedChannelBranch\s*;') {
        throw 'Module ABI contract changed; update the installer compatibility contract explicitly.'
    }
    # A different generation means different game code: Setup recompiles it instead of reusing the installed modules.
    $generateText = Get-Content -LiteralPath (Join-Path $src 'crates/nebula-recomp-cli/src/generate.rs') -Raw
    $generation = [regex]::Match($generateText, 'GENERATION_VERSION:\s*u32\s*=\s*(\d+)').Groups[1].Value
    if (-not $generation) { throw 'GENERATION_VERSION not found in generate.rs.' }
    $compatibilityPath = Join-Path $work 'module-compatibility.json'
    [ordered]@{nativeAbi=[uint32]$nativeAbi;rsoAbi=[uint32]$rsoAbi;dspProbeVersion=[uint32]$dspVersion;dspCapabilities=3;generation=[uint32]$generation} |
        ConvertTo-Json | Set-Content -LiteralPath $compatibilityPath -Encoding utf8
    $resources['module-compatibility.json'] = $compatibilityPath
    foreach ($pair in $runtimeFiles.GetEnumerator()) { $resources[$pair.Key] = $pair.Value }

    # Public releases follow docs/CONTENT_POLICY.md: observed shader caches,
    # driver pipeline libraries and all dump-derived modules remain local.
    # The runtime generates and persists its own cache on each user's PC.
    # Do not require untracked developer cache files in a git-archive build.

    # Shader-cache seed (Nebula-generated DXBC + pipeline keys; docs/CONTENT_POLICY.md).
    # InstallEngine.SeedShaderCache extracts 'shadercache-seed.<name>' on a fresh install.
    # A seed whose header version differs from the runtime's constants is refused.
    $cacheSource = Get-Content -LiteralPath (Join-Path $src 'runtime/src/gx/pipeline_cache.cpp') -Raw
    $shaderCacheVersion = [uint32][regex]::Match($cacheSource, 'kShaderCacheVersion\s*=\s*(\d+)u').Groups[1].Value
    foreach ($seedName in 'shaders.bin', 'psos.bin') {
        $seedPath = Join-Path $src ('installer/shadercache-seed/RMGE01/' + $seedName)
        if (-not (Test-Path -LiteralPath $seedPath)) { continue }
        $seedBytes = [IO.File]::ReadAllBytes($seedPath)
        $seedVersion = [BitConverter]::ToUInt32($seedBytes, 4)
        $expected = if ($seedName -eq 'shaders.bin') { $shaderCacheVersion } else { 0x100 + $shaderCacheVersion }
        if ($seedVersion -ne $expected) { throw "Shader-cache seed $seedName has version $seedVersion; the runtime expects $expected. Refresh the seed." }
        $resources['shadercache-seed.' + $seedName] = $seedPath
    }

    $resourceArgs = @($resources.GetEnumerator() | ForEach-Object { "/resource:$($_.Value),$($_.Key)" })
    # Older updaters inspect every prerelease but recognize only the standard asset name. Keep beta
    # assets separate even for those clients.
    $setupName = if ($Version -match '-beta\.') { 'Nebula-Beta-Setup.exe' } else { 'Nebula-Setup.exe' }
    $setup = Join-Path $OutputDirectory $setupName
    # Every program it carries passes Microsoft Defender first (MpCmdRun, no remediation): a detection stops the release.
    Assert-DefenderClean @($resources.Values | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    Invoke-Checked $csc (@('/nologo', '/deterministic', '/target:winexe', '/platform:x64', '/optimize+', "/win32manifest:$manifestFile", "/win32icon:$iconFile", "/out:$setup") + $references + $resourceArgs + $common +
        @(Get-ChildItem (Join-Path $src 'installer\src\Setup\*.cs') | ForEach-Object FullName))

    Assert-DefenderClean @($setup)

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
        sourceTreeSha256 = $treeHash; builtUtc = [DateTime]::UtcNow.ToString('o')
        runtimeNativeIsa = ($effectiveIsas -join '+'); requestedNativeIsa = $NativeIsa
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
