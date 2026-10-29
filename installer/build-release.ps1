<#
.SYNOPSIS
Builds Nebula-Setup.exe from one exact source revision.

.DESCRIPTION
Exports the revision with `git archive` and records each file's SHA-256
(Setup checks the source it installs against it), builds nebula-recomp and the
runtime from that export, then builds the launcher, Nebula Settings and Setup as native
programs (installer/native, MSVC) and embeds everything in Setup as one zip resource. -SigningKey also writes
Nebula-Setup.exe.sig for the updater. -CodeSigningMetadata Authenticode-signs
every program Nebula ships (nebula-recomp, both runtimes, the launcher, Nebula
Settings and Setup) through Azure Artifact Signing, with Microsoft's timestamp,
before anything embeds it.

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
    # Azure Artifact Signing's metadata.json (Endpoint, CodeSigningAccountName, CertificateProfileName). SignTool
    # (Windows SDK 10.0.22621 or later) and the Artifact Signing dlib are found unless given.
    [string]$CodeSigningMetadata = '',
    [string]$SignTool = '',
    [string]$SigningDlib = '',
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
# The work folder sits outside the user profile: the libraries Setup ships for linking record the paths they were
# built at, so a folder under %TEMP% would carry the build PC's user name into every release.
$work = Join-Path ($env:SystemDrive + '\') ('nebula-release-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $work, $OutputDirectory | Out-Null

# Azure Artifact Signing: SignTool with Microsoft's dlib signs (SHA-256, RFC 3161 timestamp from Microsoft, since the
# certificates live three days) and verifies under the default Authenticode policy. Without metadata, nothing is signed.
if ($CodeSigningMetadata) {
    if (-not (Test-Path -LiteralPath $CodeSigningMetadata)) { throw "No Artifact Signing metadata at $CodeSigningMetadata." }
    if (-not $SignTool) {
        $SignTool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" -ErrorAction SilentlyContinue |
            Where-Object { $_.Directory.Parent.Name -match '^\d+\.\d+\.\d+\.\d+$' -and [version]$_.Directory.Parent.Name -ge [version]'10.0.22621.0' } |
            Sort-Object { [version]$_.Directory.Parent.Name } -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $SigningDlib) {
        $SigningDlib = @($env:ProgramFiles, ${env:ProgramFiles(x86)}, (Join-Path $env:LOCALAPPDATA 'Microsoft')) |
            ForEach-Object { Get-ChildItem -LiteralPath $_ -Recurse -Depth 6 -Filter 'Azure.CodeSigning.Dlib.dll' -ErrorAction SilentlyContinue } |
            Where-Object { $_.FullName -match '\\x64\\' } | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $SignTool -or -not (Test-Path -LiteralPath $SignTool)) { throw 'SignTool from Windows SDK 10.0.22621 or later is required to sign.' }
    if (-not $SigningDlib -or -not (Test-Path -LiteralPath $SigningDlib)) { throw 'The Artifact Signing dlib (winget install Microsoft.Azure.ArtifactSigningClientTools) is required to sign.' }
}

function Invoke-CodeSign([string[]]$Files) {
    if (-not $CodeSigningMetadata) { return }
    Invoke-Checked $SignTool (@('sign', '/fd', 'SHA256', '/tr', 'http://timestamp.acs.microsoft.com', '/td', 'SHA256',
        '/dlib', $SigningDlib, '/dmdf', $CodeSigningMetadata) + $Files)
    Invoke-Checked $SignTool (@('verify', '/pa') + $Files)
    foreach ($file in $Files) { Write-Host "Signed $(Split-Path -Leaf $file)" }
}

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

# A release runtime is built without test hooks (CMake NEBULA_TEST_HOOKS=OFF): none of their names may be left in it.
function Assert-NoTestHooks([string]$File) {
    $bytes = [IO.File]::ReadAllBytes($File)
    $ascii = [Text.Encoding]::ASCII.GetString($bytes)
    $wide = [Text.Encoding]::Unicode.GetString($bytes)
    $names = @('GALAXY_TEST_TELEPORT', 'GALAXY_TEST_GALAXY_TOUR', 'GALAXY_CRASH_TEST',
        'GALAXY_INPUT_AUTOPRESS_SCRIPT', 'GALAXY_INPUT_STICK_SCRIPT', 'GALAXY_INPUT_SHAKE_SCRIPT', 'GALAXY_INPUT_REPLAY_LOG',
        'GALAXY_INPUT_STICK_COMMAND_FILE', 'GALAXY_INPUT_POINTER_X', 'GALAXY_INPUT_AUTOPRESS_A',
        'GALAXY_INPUT_SCRIPT_POINTER', 'GALAXY_PRESENT_SNAPSHOT_DIR', 'GALAXY_INTERP_SNAPSHOT_DIR',
        'GALAXY_INTERP_TIMELINE', 'GALAXY_INTERP_FORCE_T', 'GALAXY_INTERP_COPY_AB', 'GALAXY_DISPLAY_REFRESH_HZ',
        'GALAXY_TEST_POST_FAULT', 'GALAXY_TEST_RENDER_STALL_MS', 'GALAXY_TEST_RUMBLE_TARGET',
        'GALAXY_TEST_POINTER_DEPTH_MISS_FROM_VI', 'GALAXY_TEST_TRACE_RETIRED_DSP_AUDIO_EVENTS',
        'GALAXY_GX_CAPTURE_FRAMES', 'GALAXY_GX_CAPTURE_REQUEST_FILE', 'GALAXY_GX_CAPTURE_PPM',
        'GALAXY_GX_CAPTURE_TEXDIR', 'GALAXY_GX_CAPTURE_DRAW', 'GALAXY_GX_INVALIDATE_FRAMES',
        'GALAXY_AUDIO_WAV_DUMP', 'GALAXY_HIDE_WINDOW', 'GALAXY_DIAG_RENDER_BURN_US', 'GALAXY_DIAG_SIM_BURN_US',
        'GALAXY_GX_ABORT_AFTER_DRAWS', 'GALAXY_ALLOW_RETIRED_DSP_MIXER_FOR_TESTS')
    $found = @($names | Where-Object { $ascii.Contains($_) -or $wide.Contains($_) })
    if ($found.Count -ne 0) { throw ("{0} still contains test hooks: {1}" -f $File, ($found -join ', ')) }
    Write-Host "No test hooks in $(Split-Path -Leaf $File)"
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
    # The build PC's own paths (its user profile, the work folder) never reach the program: Rust records the source
    # path of every panic site, so the crate registry, the source export and the work folder are renamed in it.
    # CARGO_ENCODED_RUSTFLAGS (0x1F between flags) keeps paths with spaces whole.
    $cargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path $env:USERPROFILE '.cargo' }
    $env:CARGO_ENCODED_RUSTFLAGS = (@('-C', 'target-feature=+crt-static', '-C', "link-arg=$recompRes",
        "--remap-path-prefix=$cargoHome=cargo", "--remap-path-prefix=$src=nebula", "--remap-path-prefix=$work=build",
        "--remap-path-prefix=$env:USERPROFILE=home") -join [char]0x1F)
    $env:CARGO_TARGET_DIR = Join-Path $work 'target'
    Push-Location $src
    try { Invoke-Checked $cargo @('build', '--release', '--locked', '-p', 'nebula-recomp') } finally { Pop-Location; Remove-Item Env:CARGO_ENCODED_RUSTFLAGS }
    $recomp = Join-Path $env:CARGO_TARGET_DIR 'release\nebula-recomp.exe'
    # Nothing of the build PC's user profile is left in what Nebula ships.
    $profileName = Split-Path -Leaf $env:USERPROFILE
    $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($recomp))
    if ($text.IndexOf($env:USERPROFILE, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
        $text.IndexOf("Users\$profileName", [StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "$recomp still names the build PC's user profile."
    }
    Invoke-CodeSign @($recomp)

    # 3. Prebuilt runtime; the DSP is loaded from RMGE01_dsp.dll, which Setup compiles.
    # BOTH builds it twice: SSE2 under the plain resource names, AVX2 under
    # "avx2/". Each configure log is captured so the *effective* ISA CMake
    # selected is recorded; AUTO resolves against the build host.
    $runtimeFiles = @{}
    $isaBuilds = if ($NativeIsa -eq 'BOTH') { @('SSE2', 'AVX2') } else { @($NativeIsa) }
    $effectiveIsas = @()
    foreach ($isa in $isaBuilds) {
        $rt = Join-Path $work ('runtime-' + $isa.ToLowerInvariant())
        $configureLog = & cmake @('-S', $src, '-B', $rt, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', "-DGALAXY_NATIVE_ISA=$isa", '-DNEBULA_DSP_MODULE=ON', '-DGALAXY_FIDELITYFX_SDK=', "-DGALAXY_STREAMLINE_SDK=$(Join-Path $src 'third_party\streamline')", '-DNEBULA_TEST_HOOKS=OFF', "-DNEBULA_SOURCE_REVISION=$resolved", "-DNEBULA_SOURCE_REVISION_KIND=$type", "-DNEBULA_SOURCE_GIT_TREE=$gitTree", "-DNEBULA_SOURCE_TREE_SHA256=$treeHash", '-DNEBULA_SOURCE_DIRTY=0') 2>&1
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
        Assert-NoTestHooks $runtimeFiles[$prefix + 'NebulaRuntime.exe']
        Invoke-CodeSign @($runtimeFiles[$prefix + 'NebulaRuntime.exe'])
    }
    $runtimeIsaPath = Join-Path $work 'runtime-isa.txt'
    [IO.File]::WriteAllText($runtimeIsaPath, ($effectiveIsas -join '+'), (New-Object Text.UTF8Encoding $false))

    # 4. Nebula.exe and NebulaSettings.exe: native programs (installer/native, MSVC, static C runtime, no .NET).
    $nativeOut = Join-Path $work 'native'
    $publicKeyFile = Join-Path $src 'installer\release-public-key.xml'
    & (Join-Path $src 'installer\native\build.ps1') -Version $Version -Commit $resolved -Repository $Repository `
        -PublicKeyFile $publicKeyFile -OutputDirectory $nativeOut
    $launcher = Join-Path $nativeOut 'Nebula.exe'
    $settingsApp = Join-Path $nativeOut 'NebulaSettings.exe'
    foreach ($program in $launcher, $settingsApp) {
        if (-not (Test-Path -LiteralPath $program)) { throw "The native build produced no $program" }
        if ([Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($program)).Contains('NEBULA_UI_SNAPSHOT_DIR')) { throw "$program contains the UI snapshot test mode." }
    }
    Invoke-CodeSign @($launcher, $settingsApp)

    # The toolchain's identity (the folder it unpacks to and part of every module key): the first 16 hex digits of the
    # SHA-256 of toolchain.json as Nebula's C# Setup serialized it (JavaScriptSerializer, indented two spaces), so
    # installs made by earlier versions keep their toolchain and retained objects.
    Add-Type -AssemblyName System.Web.Extensions
    $serializer = New-Object System.Web.Script.Serialization.JavaScriptSerializer
    $serializer.MaxJsonLength = [int]::MaxValue
    $serializer.RecursionLimit = 256
    $compact = $serializer.Serialize($serializer.DeserializeObject((Get-Content -LiteralPath (Join-Path $src 'installer\toolchain.json') -Raw)))
    $pretty = New-Object Text.StringBuilder
    $indent = 0; $quoted = $false
    for ($i = 0; $i -lt $compact.Length; $i++) {
        $c = $compact[$i]
        if ($quoted) {
            [void]$pretty.Append($c)
            if ($c -eq '\' -and $i + 1 -lt $compact.Length) { $i++; [void]$pretty.Append($compact[$i]) }
            elseif ($c -eq '"') { $quoted = $false }
            continue
        }
        switch ($c) {
            '"' { $quoted = $true; [void]$pretty.Append($c) }
            { $_ -eq '{' -or $_ -eq '[' } {
                [void]$pretty.Append($c)
                if ($i + 1 -lt $compact.Length -and ($compact[$i + 1] -eq '}' -or $compact[$i + 1] -eq ']')) { $i++; [void]$pretty.Append($compact[$i]) }
                else { $indent++; [void]$pretty.Append("`n").Append(' ', 2 * $indent) }
            }
            { $_ -eq '}' -or $_ -eq ']' } { $indent--; [void]$pretty.Append("`n").Append(' ', 2 * $indent).Append($c) }
            ',' { [void]$pretty.Append(",`n").Append(' ', 2 * $indent) }
            ':' { [void]$pretty.Append(': ') }
            default { [void]$pretty.Append($c) }
        }
    }
    $toolchainId = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash((New-Object Text.UTF8Encoding $false).GetBytes($pretty.ToString()))).Replace('-', '').ToLowerInvariant().Substring(0, 16)
    $toolchainIdPath = Join-Path $work 'toolchain-id.txt'
    [IO.File]::WriteAllText($toolchainIdPath, $toolchainId, (New-Object Text.UTF8Encoding $false))
    Write-Host "Toolchain id $toolchainId"

    $resources = @{
        'nebula-recomp.exe' = $recomp; 'Nebula.exe' = $launcher; 'NebulaSettings.exe' = $settingsApp; 'source-manifest.json' = $manifestPath
        'toolchain.json' = (Join-Path $src 'installer\toolchain.json'); 'runtime-env.json' = (Join-Path $src 'installer\runtime-env.json')
        'runtime-isa.txt' = $runtimeIsaPath; 'toolchain-id.txt' = $toolchainIdPath
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

    # Older updaters inspect every prerelease but recognize only the standard asset name. Keep beta
    # assets separate even for those clients.
    $setupName = if ($Version -match '-beta\.') { 'Nebula-Beta-Setup.exe' } else { 'Nebula-Setup.exe' }
    $setup = Join-Path $OutputDirectory $setupName
    # Every program it carries passes Microsoft Defender first (MpCmdRun, no remediation): a detection stops the release.
    Assert-DefenderClean @($resources.Values | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    # Nothing it carries names the build PC's user profile (the payload is compressed, so each file is checked first).
    foreach ($file in @($resources.Values | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })) {
        $bytes = [IO.File]::ReadAllBytes($file)
        $ascii = [Text.Encoding]::ASCII.GetString($bytes); $wide = [Text.Encoding]::Unicode.GetString($bytes)
        foreach ($needle in @($env:USERPROFILE, "Users\$profileName")) {
            if ($ascii.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or $wide.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "$file names the build PC's user profile ($needle)."
            }
        }
    }
    # Setup carries the files it installs in one compressed zip resource (PAYLOAD), read by name.
    $payloadZip = Join-Path $work 'payload.zip'
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $payloadStream = [IO.File]::Open($payloadZip, [IO.FileMode]::CreateNew)
    try {
        $archive = New-Object IO.Compression.ZipArchive($payloadStream, [IO.Compression.ZipArchiveMode]::Create)
        foreach ($pair in ($resources.GetEnumerator() | Sort-Object Key)) {
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $pair.Value, $pair.Key, [IO.Compression.CompressionLevel]::Optimal)
        }
        $archive.Dispose()
    } finally { $payloadStream.Dispose() }
    & (Join-Path $src 'installer\native\build.ps1') -Version $Version -Commit $resolved -Repository $Repository `
        -PublicKeyFile $publicKeyFile -OutputDirectory $nativeOut -Payload $payloadZip
    Copy-Item -LiteralPath (Join-Path $nativeOut 'Nebula-Setup.exe') -Destination $setup -Force
    if ([Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($setup)).Contains('NEBULA_UI_SNAPSHOT_DIR')) { throw "$setup contains the UI snapshot test mode." }
    Invoke-CodeSign @($setup)
    # Setup carries every program as is: none of them names the build PC's user profile.
    $setupText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($setup))
    $setupWide = [Text.Encoding]::Unicode.GetString([IO.File]::ReadAllBytes($setup))
    foreach ($needle in @($env:USERPROFILE, "Users\$profileName")) {
        if ($setupText.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $setupWide.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "$setup still names the build PC's user profile ($needle)."
        }
    }

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
        authenticode = if ($CodeSigningMetadata) { 'Azure Artifact Signing' } else { 'none' }
        rustc = (& (Join-Path (Split-Path $cargo) 'rustc.exe') --version) 2>$null
    }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'build-info.json'), ($info | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
    Write-Host "Nebula-Setup.exe $setupHash"
} finally {
    $resolvedWork = [IO.Path]::GetFullPath($work)
    $resolvedRoot = [IO.Path]::GetFullPath($env:SystemDrive + '\')
    if (-not [string]::Equals((Split-Path -Parent $resolvedWork).TrimEnd('\') + '\', $resolvedRoot, [StringComparison]::OrdinalIgnoreCase) -or
        (Split-Path -Leaf $resolvedWork) -notmatch '^nebula-release-[0-9a-f]{32}$') {
        throw "Refusing cleanup outside the release's work folder."
    }
    Remove-Item -LiteralPath $resolvedWork -Recurse -Force -ErrorAction SilentlyContinue
}
