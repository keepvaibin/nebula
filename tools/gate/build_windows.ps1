# build_windows.ps1 -Tree TREE -Root ROOT -Bin BIN -Pak PAK [-Dxc DXC] [-Jobs N]: every Windows build the gate tests,
# then the bin folder the harness runs. Builds are incremental: ROOT keeps them between runs.
#   ROOT\game   the game modules, the headless host and the self-description (clang, native/)
#   ROOT\msvc   Nebula, NebulaVk, NebulaD11, the unit tests and the test extension (MSVC, the tree's root)
#   ROOT\luma-test.dll  the harness's game half (tools/test_driver/luma_test/luma_test.c)
#   ROOT\cargo  nebula-import
# Prints each step's result; any FAILED line or non-zero exit ends it with exit 1. Nothing here reads the
# environment of the person running it beyond the tools' locations.
#
# -Sanitize address,undefined: the game modules built with those sanitizers (native/CMakeLists.txt NEBULA_SANITIZE),
# their runtime (LLVM's clang_rt.asan_dynamic) in the bin folder. The programs stay as they are: one AddressSanitizer
# runtime serves a process, and Microsoft's compiler would bring its own. Use a ROOT of its own.
param(
    [Parameter(Mandatory = $true)][string]$Tree,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Bin,
    [Parameter(Mandatory = $true)][string]$Pak,
    [string]$Dxc = "D:/NebulaWork/build/luma-bins/lh7/dxc",
    [string]$Ffx = "D:/NebulaWork/deps/ffx-fg/FidelityFX-SDK-1.1.4",
    [string]$Llvm = "C:/Program Files/LLVM/bin",
    [int]$Jobs = 12,
    # A configured build folder whose compiled shader stages (generated/*.bin.stages, keyed by content) start a new
    # build's, so the first build does not compile every program again.
    [string]$SeedFrom = "",
    # Sanitizers for the game modules (clang -fsanitize=...), e.g. address,undefined; empty for none.
    [string]$Sanitize = ""
)
$ErrorActionPreference = "Stop"
$Tree = (Resolve-Path $Tree).Path.Replace("\", "/")
New-Item -ItemType Directory -Force $Root | Out-Null
$Root = (Resolve-Path $Root).Path.Replace("\", "/")
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$vs = & vswhere.exe -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = (Get-Command ninja).Source.Replace("\", "/")
$failed = 0

# A step: its command in the build environment, its output in its log. Start returns the running process; Finish
# waits for it and judges it (a FAILED line or a non-zero exit fails the build); Step does both.
function Start-Step([string]$Command, [string]$Log) {
    $p = Start-Process -FilePath cmd.exe -ArgumentList "/d /c `"call `"$vcvars`" >nul && ($Command) > `"$Log`" 2>&1`"" `
        -WindowStyle Hidden -PassThru
    $null = $p.Handle  # held from the start, or ExitCode reads empty once the process has ended
    $p
}
function Finish-Step([string]$Name, $Process, [string]$Log, $Watch) {
    $Process.WaitForExit()
    $code = $Process.ExitCode
    $fails = @(Select-String -Path $Log -Pattern '^FAILED:|ninja: build stopped' -ErrorAction SilentlyContinue)
    "{0}: exit {1}, {2} FAILED, {3} s" -f $Name, $code, $fails.Count, [int]$Watch.Elapsed.TotalSeconds
    if ($code -ne 0 -or $fails.Count -ne 0) {
        Select-String -Path $Log -Pattern '^FAILED:|: error|: fatal error|error:' | Select-Object -First 12 |
            ForEach-Object { "  " + $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
        $script:failed++
    }
}
function Step([string]$Name, [string]$Command, [string]$Log) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    Finish-Step $Name (Start-Step $Command $Log) $Log $sw
}

# The game modules (both), the headless host, the self-description and the services list.
$game = "$Root/game"
if (-not (Test-Path "$game/build.ninja")) {
    Step "game configure" ("`"$cmake`" -S `"$Tree/native`" -B `"$game`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" " +
        "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=`"$Llvm/clang.exe`" -DCMAKE_CXX_COMPILER=`"$Llvm/clang++.exe`" " +
        "-DCMAKE_RC_COMPILER=`"$Llvm/llvm-rc.exe`" -DNEBULA_PETARI_DIR=`"$Tree/native/petari`" " +
        "-DNEBULA_SANITIZE=`"$Sanitize`"") "$Root/game.configure.log"
}
$configured = (Select-String -Path "$game/CMakeCache.txt" -Pattern '^NEBULA_SANITIZE:STRING=(.*)$' -ErrorAction SilentlyContinue |
    Select-Object -First 1)
$have = if ($configured) { $configured.Matches[0].Groups[1].Value } else { "" }
if ($have -ne $Sanitize) { "game: $game was configured with NEBULA_SANITIZE='$have', asked for '$Sanitize'"; exit 1 }
# The game and the MSVC build run side by side, each with half the jobs.
$half = [Math]::Max(2, [int]($Jobs / 2))
$gameWatch = [Diagnostics.Stopwatch]::StartNew()
$gameRun = Start-Step "`"$cmake`" --build `"$game`" --target all -j $half -- -k 0" "$Root/game.build.log"

# Every Windows program, the unit tests and the test extension, in one MSVC build.
$msvc = "$Root/msvc"
New-Item -ItemType Directory -Force $msvc | Out-Null
# The SDK's libraries are built already; building them again would rewrite the folder other trees link from.
foreach ($stamp in "fidelityfx-fg.built", "fidelityfx-fg-vk.built") {
    if (-not (Test-Path "$msvc/$stamp")) { Set-Content -Path "$msvc/$stamp" -Value "$Ffx/sdk" -Encoding ascii }
}
if ($SeedFrom -and (Test-Path "$SeedFrom/generated")) {
    New-Item -ItemType Directory -Force "$msvc/generated" | Out-Null
    Get-ChildItem "$SeedFrom/generated" -Directory -Filter "*.bin.stages" | ForEach-Object {
        if (-not (Test-Path "$msvc/generated/$($_.Name)")) { Copy-Item -Recurse $_.FullName "$msvc/generated/$($_.Name)" }
    }
}
if (-not (Test-Path "$msvc/build.ninja")) {
    Step "msvc configure" ("`"$cmake`" -G Ninja -S `"$Tree`" -B `"$msvc`" -DCMAKE_BUILD_TYPE=Release -DNEBULA_VULKAN=ON " +
        "-DNEBULA_D3D11=ON -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DFETCHCONTENT_SOURCE_DIR_FIDELITYFX_SDK=$Ffx " +
        "`"-DNEBULA_EXTRA_CMAKE=$Tree/tools/test_driver/test_driver.cmake`"") "$Root/msvc.configure.log"
}
$msvcWatch = [Diagnostics.Stopwatch]::StartNew()
$msvcRun = Start-Step "`"$cmake`" --build `"$msvc`" -j $half -- -k 0" "$Root/msvc.build.log"
Finish-Step "game" $gameRun "$Root/game.build.log" $gameWatch
Finish-Step "msvc" $msvcRun "$Root/msvc.build.log" $msvcWatch

# The harness's game half: a LumaCore mod library, built as build tools build mods (no library linked, unwind tables).
$inc = "$Tree/native/include"
Step "luma-test" ("`"$Llvm/clang.exe`" --target=x86_64-pc-windows-msvc -O2 -shared -nostdlib -ffreestanding " +
    "-fasynchronous-unwind-tables -std=c11 -fno-stack-protector -fvisibility=hidden -fuse-ld=lld -Wl,/NOENTRY " +
    "-Wl,/NOIMPLIB -Wall -Wextra -Werror -I `"$inc`" -o `"$Root/luma-test.dll`" `"$Tree/tools/test_driver/luma_test/luma_test.c`"") `
    "$Root/luma-test.log"

# The importer the harness cooks test mods with.
Step "nebula-import" ("cd /d `"$Tree`" && set CARGO_TARGET_DIR=$Root/cargo&& cargo build --release --locked --offline " +
    "-p nebula-import --bin nebula-import --lib") "$Root/cargo.log"

if ($failed -ne 0) { "build failed"; exit 1 }

# The bin folder: what luma_test.py runs (the programs, the game modules, the game half, the importer, the
# self-description, the shader compiler and the pak's path).
$pv = $null
# The header only (the pak is several gigabytes).
$head = New-Object byte[] 12
$stream = [IO.File]::OpenRead($Pak)
try { $null = $stream.Read($head, 0, 12) } finally { $stream.Dispose() }
if ([Text.Encoding]::ASCII.GetString($head, 0, 7) -eq "NBSTORE") { $pv = [BitConverter]::ToUInt32($head, 8) }
$tv = (Get-Content "$Tree/runtime/include/galaxy/store_version.inc" -Raw).Trim()
if ("$pv" -ne $tv) { "bins: $Pak is store version $pv, the tree's is $tv"; exit 1 }
if (Test-Path $Bin) { Remove-Item -Recurse -Force $Bin }
New-Item -ItemType Directory -Force "$Bin/luma" | Out-Null
Copy-Item "$game/smg_game.dll", "$game/smg_game_plain.dll", "$game/smg_game.luma-placement.json" $Bin
if ($Sanitize) {
    # The sanitizers' runtime the modules import, from the compiler that built them.
    $rt = Get-ChildItem "$Llvm/../lib/clang/*/lib/windows/clang_rt.asan_dynamic-x86_64.dll" | Select-Object -First 1
    if (-not $rt) { "bins: no clang_rt.asan_dynamic-x86_64.dll beside $Llvm"; exit 1 }
    Copy-Item $rt.FullName $Bin
    Set-Content -Path "$Bin/sanitize.txt" -Value $Sanitize -Encoding ascii
}
Copy-Item "$msvc/Nebula.exe", "$msvc/NebulaVk.exe", "$msvc/NebulaD11.exe", "$msvc/nebula_test_extension.dll" $Bin
if (Test-Path "$msvc/D3D12") { Copy-Item -Recurse "$msvc/D3D12" "$Bin/D3D12" }
Copy-Item "$Root/luma-test.dll" "$Bin/luma-test.dll"
Copy-Item "$Root/cargo/release/nebula-import.exe", "$Root/cargo/release/nebula_import.dll" $Bin
Copy-Item "$game/luma_types/luma_types.json" $Bin
Copy-Item -Recurse "$game/luma/*" "$Bin/luma/"
foreach ($f in "luma_types/luma_types.json", "luma_types/luma_functions.json", "luma_types/luma_numbers.json",
        "luma_types/luma_services.json", "smg_game.luma-placement.json", "smg_game.luma-self.json") {
    Copy-Item "$game/$f" "$Bin/luma/"
}
Copy-Item "$game/luma_types/luma_services.json" $Bin
Copy-Item -Recurse $Dxc "$Bin/dxc"
Set-Content -Path "$Bin/pak.path" -Value $Pak -Encoding ascii
$commit = & git -C $Tree rev-parse --short HEAD
Set-Content -Path "$Bin/commit.txt" -Value $commit -Encoding ascii
"bins ok $commit"
