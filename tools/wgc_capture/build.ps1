# Builds wgc_capture.exe (Windows Graphics Capture recorder for test runs) into -Out (default: build\wgc_capture).
param([string]$Out = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'build\wgc_capture'))
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'enter_msvc_environment.ps1')
New-Item -ItemType Directory -Force $Out | Out-Null
$source = Join-Path $PSScriptRoot 'wgc_capture.cpp'
& cl.exe /nologo /std:c++17 /EHsc /O2 /W3 /DNOMINMAX /DUNICODE /D_UNICODE $source /Fo"$Out\" /Fe"$Out\wgc_capture.exe" `
    /link d3d11.lib dxgi.lib user32.lib windowsapp.lib
if ($LASTEXITCODE -ne 0) { throw "cl failed with exit code $LASTEXITCODE" }
