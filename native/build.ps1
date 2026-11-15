# Builds the game module (native/CMakeLists.txt) on Windows with clang++ and Ninja, inside the MSVC environment
# (for the C runtime and Windows SDK libraries the link needs).
#   native\build.ps1 -Petari native\petari -Build build\game [-Config Release] [-Target smg_game] [-Pgo] [-NoBaseline]
#                    [-Llvm <LLVM's bin folder>]
# The module is built for x86-64-v3 into Build and again for x86-64-v2 into Build\x86-64-v2 (the host loads that one
# on a CPU without x86-64-v3; a package carries both). -NoBaseline builds the first only. -Llvm names the LLVM that
# builds it (21 or later: the row generator needs its libclang); by default the installed one.
param(
    [Parameter(Mandatory = $true)][string]$Petari,
    [Parameter(Mandatory = $true)][string]$Build,
    [string]$Config = "Release",
    [string]$Target = "smg_game",
    [switch]$Pgo,
    [string]$ProfileFlags = "",
    [switch]$NoBaseline,
    [string]$Llvm = "C:\Program Files\LLVM\bin"
)
$ErrorActionPreference = "Stop"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
# Ninja from PATH, else the one Visual Studio's CMake comes with.
$ninja = (Get-Command ninja -ErrorAction SilentlyContinue).Source
if (-not $ninja) { $ninja = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" }
if (-not (Test-Path -LiteralPath $ninja)) { throw "Ninja was not found" }
# CMake reads these into its own files: forward slashes.
$llvm = $Llvm.Replace("\", "/").TrimEnd("/")
if (-not (Test-Path -LiteralPath "$llvm/clang++.exe")) { throw "No clang++.exe in $Llvm" }
$src = $PSScriptRoot.Replace("\", "/")
$Petari = $Petari.Replace("\", "/")
$Build = $Build.Replace("\", "/")
$levels = [ordered]@{ "x86-64-v3" = $Build }
if (-not $NoBaseline) { $levels["x86-64-v2"] = "$Build/x86-64-v2" }
# vcvars64.bat runs vswhere.exe from PATH.
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
foreach ($level in $levels.Keys) {
    $folder = $levels[$level]
    $configure = "`"$cmake`" -S `"$src`" -B `"$folder`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$($ninja.Replace("\", "/"))`" " +
        "-DCMAKE_BUILD_TYPE=$Config -DCMAKE_C_COMPILER=`"$llvm/clang.exe`" -DCMAKE_CXX_COMPILER=`"$llvm/clang++.exe`" " +
        "-DCMAKE_RC_COMPILER=`"$llvm/llvm-rc.exe`" -DNEBULA_PETARI_DIR=`"$Petari`" -DNEBULA_CPU_LEVEL=$level -DNEBULA_PGO=$(if ($Pgo) { 'ON' } else { 'OFF' }) -DCMAKE_C_FLAGS=$ProfileFlags -DCMAKE_CXX_FLAGS=$ProfileFlags -DCMAKE_SHARED_LINKER_FLAGS=$ProfileFlags"
    $compile = "`"$cmake`" --build `"$folder`" --target $Target"
    cmd.exe /d /c "call `"$vcvars`" >nul && ($configure && $compile) 2>&1"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
exit 0
