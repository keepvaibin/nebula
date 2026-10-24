# Builds the game module (native/CMakeLists.txt) on Windows with clang++ and Ninja, inside the MSVC environment
# (for the C runtime and Windows SDK libraries the link needs).
#   native\build.ps1 -Petari native\petari -Build build\game [-Config Release] [-Target smg_game]
param(
    [Parameter(Mandatory = $true)][string]$Petari,
    [Parameter(Mandatory = $true)][string]$Build,
    [string]$Config = "Release",
    [string]$Target = "smg_game"
)
$ErrorActionPreference = "Stop"
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = (Get-Command ninja -ErrorAction Stop).Source
# CMake reads these into its own files: forward slashes.
$llvm = "C:/Program Files/LLVM/bin"
$src = $PSScriptRoot.Replace("\", "/")
$Petari = $Petari.Replace("\", "/")
$Build = $Build.Replace("\", "/")
$configure = "`"$cmake`" -S `"$src`" -B `"$Build`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$($ninja.Replace("\", "/"))`" " +
    "-DCMAKE_BUILD_TYPE=$Config -DCMAKE_C_COMPILER=`"$llvm/clang.exe`" -DCMAKE_CXX_COMPILER=`"$llvm/clang++.exe`" " +
    "-DCMAKE_RC_COMPILER=`"$llvm/llvm-rc.exe`" -DNEBULA_PETARI_DIR=`"$Petari`""
$compile = "`"$cmake`" --build `"$Build`" --target $Target"
# vcvars64.bat runs vswhere.exe from PATH.
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
cmd.exe /d /c "call `"$vcvars`" >nul && ($configure && $compile) 2>&1"
exit $LASTEXITCODE
