# ctest.ps1 -Dir DIR [-Name TEST]: the unit tests of an MSVC build folder (build_windows.ps1's ROOT\msvc). Prints ctest's summary
# and every failed test; the exit code is ctest's. -Name: only the test of that name (a unit-test program, galaxy_pacing_tests); a
# name that no test has is a failure (ctest's --no-tests=error).
param([Parameter(Mandatory = $true)][string]$Dir, [int]$Jobs = 8, [string]$Name = "")
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$vs = & vswhere.exe -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$ctest = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
$only = ""
if ($Name) { $only = ' -R "^' + $Name + '$" --no-tests=error' }
cmd.exe /d /c "call `"$vcvars`" >nul && cd /d `"$Dir`" && `"$ctest`" --output-on-failure --timeout 600 -j $Jobs$only"
exit $LASTEXITCODE
