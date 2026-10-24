# Checks the tracked files: no game content, build outputs, oversized files or credentials, and no
# C runtime fma call in the runtime.
[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ForbiddenExtensions = @(
    ".arc", ".ast", ".aw", ".bnr", ".ciso", ".dol", ".gcz", ".img",
    ".iso", ".rso", ".rvz", ".szs", ".thp", ".wad", ".wbfs", ".wia",
    ".bin", ".pak", ".nds", ".sel", ".brres", ".bmg", ".rarc"
)
# The Android build's shader seed: pipeline keys (hashes of GX state) the PC's galaxy tour captured, no game data.
$AllowedBinaries = @(
    "platform/android/vk_seed_keys.bin"
)
$ForbiddenBuildExtensions = @(
    ".dll", ".exe", ".exp", ".ilk", ".lib", ".obj", ".pdb"
)
$SecretPatterns = @(
    ("sk-" + "ant-"),
    ("ANTHROPIC_" + "API_KEY="),
    ("OPENAI_" + "API_KEY="),
    ("<" + "P>"),
    ("-----BEGIN " + "PRIVATE KEY")
)

$Failures = [System.Collections.Generic.List[string]]::new()
$TrackedFiles = @(& git -C $Root ls-files)
if ($LASTEXITCODE -ne 0) {
    throw "git ls-files failed while auditing repository content."
}

$TrackedIgnoredFiles = @(& git -C $Root ls-files -ci --exclude-standard)
if ($LASTEXITCODE -ne 0) {
    throw "git ls-files -ci failed while auditing ignored tracked files."
}
foreach ($RelativePath in $TrackedIgnoredFiles) {
    if (-not [string]::IsNullOrWhiteSpace($RelativePath)) {
        $Failures.Add("Ignored local/build artifact is still tracked: $RelativePath")
    }
}

foreach ($RelativePath in $TrackedFiles) {
    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        continue
    }

    $FullPath = Join-Path -Path $Root -ChildPath $RelativePath
    if (-not (Test-Path -LiteralPath $FullPath -PathType Leaf)) {
        continue
    }

    $File = Get-Item -LiteralPath $FullPath
    # The shader-cache seed is Nebula-generated DXBC (docs/CONTENT_POLICY.md).
    if ($RelativePath -like 'installer/shadercache-seed/*') {
        if ($File.Length -gt 16MB) { $Failures.Add("Shader-cache seed larger than 16 MiB: $($File.FullName)") }
        continue
    }
    if ($File.Extension.ToLowerInvariant() -in $ForbiddenExtensions -and
        $RelativePath -notin $AllowedBinaries) {
        $Failures.Add("Forbidden game-content extension: $($File.FullName)")
    }
    if ($File.Extension.ToLowerInvariant() -in $ForbiddenBuildExtensions) {
        $Failures.Add("Forbidden tracked build/binary output: $($File.FullName)")
    }
    if ($File.Length -gt 5MB) {
        $Failures.Add("Unexpected tracked file larger than 5 MiB: $($File.FullName)")
    }

    if ($File.Extension -in @(".cpp", ".h", ".inl", ".json", ".md", ".ps1", ".rs", ".toml", ".txt", ".cs", ".xml", ".py")) {
        $Text = [string](Get-Content -LiteralPath $File.FullName -Raw -ErrorAction Stop)
        foreach ($Pattern in $SecretPatterns) {
            if ($Text.Contains($Pattern)) {
                $Failures.Add("Possible API credential in $($File.FullName): pattern $Pattern")
            }
        }
        # On an SSE2 build std::fma is a C runtime call, a soft-float routine on CPUs
        # without FMA3, far too slow for the THP decoder. Tests may use it as the
        # reference.
        if ($RelativePath -like 'runtime/*' -and $File.Extension -in @(".cpp", ".h", ".inl") -and
            $RelativePath -notlike '*_tests.cpp' -and $RelativePath -ne 'runtime/include/galaxy/fused_multiply_add.h' -and
            $Text -match '(std::|[^\w.])fmaf?\s*\(') {
            $Failures.Add("std::fma/fmaf in $RelativePath; use galaxy::fused_multiply_add (galaxy/fused_multiply_add.h)")
        }
    }
}

if ($Failures.Count -ne 0) {
    $Failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "[PASS] Tracked repository content contains no forbidden game content, oversized files, API-key patterns, or C runtime fma calls in the runtime."
