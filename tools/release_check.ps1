# release_check.ps1 -Path <folder>: the gate every release folder passes before it is published (v1.0.1 was flagged by
# Defender: unsigned, rare, a dropper's patterns and test hooks shipped). Every executable (.exe, .dll) must carry
# version information; no file may hold a test switch's name (the test hooks are compiled out of releases), the
# owner's personal data or a build machine's user path (link with /PDBALTPATH:%_PDB%); and Microsoft Defender must
# find nothing in any file (scanned without remediation). Exits 1 on any failure, listing each.
param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = "Stop"
$failures = New-Object System.Collections.Generic.List[string]
$files = Get-ChildItem -LiteralPath $Path -Recurse -File
if ($files.Count -eq 0) { Write-Output "release_check: no files under $Path"; exit 1 }

# Text no shipped file may contain (ASCII and UTF-16): the test hooks' switches, a build machine's user path, and the
# personal terms the release key's folder lists one per line (private-terms.txt; kept out of the repository, so no
# name is ever written here).
$forbidden = @("GALAXY_TEST_", "GALAXY_INPUT_AUTOPRESS", "GALAXY_PRESENT_SNAPSHOT", "GALAXY_INTERP_SNAPSHOT",
               "GALAXY_TEST_TELEPORT", "C:\Users\")
$privateTerms = Join-Path $env:USERPROFILE ".nebula-release-key\private-terms.txt"
if (Test-Path -LiteralPath $privateTerms) {
    $forbidden += Get-Content -LiteralPath $privateTerms | Where-Object { $_.Trim() -ne "" } | ForEach-Object { $_.Trim() }
} else {
    $failures.Add("$privateTerms is missing: the personal terms were not checked")
}
$patterns = foreach ($text in $forbidden) {
    [pscustomobject]@{ Text = $text; Ascii = [Text.Encoding]::ASCII.GetBytes($text); Wide = [Text.Encoding]::Unicode.GetBytes($text) }
}
function Find-Bytes([byte[]]$haystack, [byte[]]$needle) {
    $first = $needle[0]
    $last = $haystack.Length - $needle.Length
    for ($i = [Array]::IndexOf($haystack, $first); $i -ge 0 -and $i -le $last; $i = [Array]::IndexOf($haystack, $first, $i + 1)) {
        $match = $true
        for ($k = 1; $k -lt $needle.Length; $k++) { if ($haystack[$i + $k] -ne $needle[$k]) { $match = $false; break } }
        if ($match) { return $true }
    }
    return $false
}

foreach ($file in $files) {
    $relative = $file.FullName.Substring((Resolve-Path -LiteralPath $Path).Path.Length).TrimStart('\')
    if ($file.Extension -in @(".exe", ".dll")) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName)
        if ([string]::IsNullOrWhiteSpace($info.ProductName) -or [string]::IsNullOrWhiteSpace($info.FileVersion) -or
            [string]::IsNullOrWhiteSpace($info.FileDescription)) {
            $failures.Add("$relative`: no version information (product, file version and description)")
        }
    }
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    foreach ($p in $patterns) {
        if ((Find-Bytes $bytes $p.Ascii) -or (Find-Bytes $bytes $p.Wide)) { $failures.Add("$relative`: contains `"$($p.Text)`"") }
    }
}

$scanner = Join-Path $env:ProgramFiles "Windows Defender\MpCmdRun.exe"
if (-not (Test-Path -LiteralPath $scanner)) {
    $failures.Add("Microsoft Defender's command line scanner is missing: nothing was scanned")
} else {
    foreach ($file in $files) {
        $output = & $scanner -Scan -ScanType 3 -File $file.FullName -DisableRemediation 2>&1
        if ($LASTEXITCODE -ne 0) {
            $failures.Add("$($file.Name): Defender reports a threat or failed to scan (exit $LASTEXITCODE): $(($output | Select-Object -Last 3) -join ' ')")
        }
    }
}

if ($failures.Count -ne 0) {
    Write-Output "release_check: $($failures.Count) failure(s) in $Path"
    $failures | ForEach-Object { Write-Output "  $_" }
    exit 1
}
Write-Output "release_check: $($files.Count) files passed (version information, no test hooks or personal data, Defender clean)"
exit 0
