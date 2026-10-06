[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$ForbiddenExtensions = @(
    ".arc", ".ast", ".aw", ".bnr", ".ciso", ".dol", ".gcz", ".img",
    ".iso", ".rso", ".rvz", ".szs", ".thp", ".wad", ".wbfs", ".wia",
    ".bin", ".pak", ".nds", ".sel", ".brres", ".bmg", ".rarc"
)
# Clean-room Dolphin DSP ROM replacements (see the folder's README.md).
$AllowedBinaries = @(
    "third_party/dolphin-free-dsp-rom/dsp_rom.bin",
    "third_party/dolphin-free-dsp-rom/dsp_coef.bin"
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

    if ($File.Extension -in @(".cpp", ".h", ".json", ".md", ".ps1", ".rs", ".toml", ".txt", ".cs", ".xml", ".py")) {
        $Text = [string](Get-Content -LiteralPath $File.FullName -Raw -ErrorAction Stop)
        foreach ($Pattern in $SecretPatterns) {
            if ($Text.Contains($Pattern)) {
                $Failures.Add("Possible API credential in $($File.FullName): pattern $Pattern")
            }
        }
    }
}

if ($Failures.Count -ne 0) {
    $Failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Host "[PASS] Tracked repository content contains no forbidden game content, oversized files, or API-key patterns."
