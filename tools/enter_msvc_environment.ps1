function Find-VsInstallRoot {
    $VsWhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $VsWhere) {
        $Root = & $VsWhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath
        if ($LASTEXITCODE -eq 0 -and $Root) {
            return $Root.Trim()
        }
    }

    $Candidates = @(
        "C:\Program Files\Microsoft Visual Studio\2022\Community",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community",
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    )
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath (Join-Path $Candidate "Common7\Tools\VsDevCmd.bat")) {
            return $Candidate
        }
    }

    throw "Visual Studio developer environment was not found"
}

$VsRoot = Find-VsInstallRoot
$VsDevCmd = Join-Path $VsRoot "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $VsDevCmd)) {
    throw "Visual Studio developer environment was not found at $VsDevCmd"
}

# VsDevCmd.bat shells out to a bare "vswhere.exe" internally; ensure its
# install directory is on PATH regardless of the ambient environment.
$VsWhereDir = "C:\Program Files (x86)\Microsoft Visual Studio\Installer"
if ((Test-Path -LiteralPath (Join-Path $VsWhereDir "vswhere.exe")) -and
    ($env:Path -split ';') -notcontains $VsWhereDir) {
    $env:Path = "$VsWhereDir;$env:Path"
}

$EnvironmentLines = & $env:ComSpec /d /s /c "`"$VsDevCmd`" -no_logo -arch=x64 -host_arch=x64 >nul && set"
if ($LASTEXITCODE -ne 0) {
    throw "Visual Studio developer environment initialization failed"
}

foreach ($Line in $EnvironmentLines) {
    $Separator = $Line.IndexOf("=")
    if ($Separator -gt 0) {
        $Name = $Line.Substring(0, $Separator)
        $Value = $Line.Substring($Separator + 1)
        Set-Item -Path "Env:$Name" -Value $Value
    }
}

$VsCMakeBin = Join-Path $VsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
$VsNinjaBin = Join-Path $VsRoot "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
$PathParts = @()
foreach ($Candidate in @($VsCMakeBin, $VsNinjaBin)) {
    if ((Test-Path -LiteralPath $Candidate) -and
        $PathParts -notcontains $Candidate) {
        $PathParts += $Candidate
    }
}
foreach ($Part in ($env:Path -split ';')) {
    if ($Part -and $PathParts -notcontains $Part) {
        $PathParts += $Part
    }
}
$env:Path = $PathParts -join ';'
