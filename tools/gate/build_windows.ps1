# build_windows.ps1 -Tree TREE -Root ROOT -Bin BIN [-Pak PAK] [-Dxc DXC] [-Jobs N]: every Windows build the gate tests,
# then the bin folder the harness runs. Builds are incremental: ROOT keeps them between runs.
#   ROOT\game   the game modules, the headless host and the self-description (clang, native/)
#   ROOT\msvc   Nebula, NebulaVk, NebulaD11, the unit tests and the test extension (MSVC, the tree's root)
#   ROOT\luma-test.dll  the harness's game half (tools/test_driver/luma_test/luma_test.c)
#   ROOT\cargo  nebula-import
# -Pak is optional and never refused: a game.pak of the tree's store version is named in BIN/pak.path; with none, or one of
# another store version, the bin folder has no pak.path and the build says so. The gate asks for the right data by key
# (tools/gate/gamedata.py, after this build: the importer is one of the programs built here) and writes pak.path itself.
# Prints each step's result; any FAILED line or non-zero exit ends it with exit 1, except a step whose every failed job is of the
# compiler cache alone (build_steps.ps1): that is run again once, and failing again the same way ends it with exit 2 (not judged). Nothing here reads the
# environment of the person running it beyond the tools' locations and NEBULA_NO_MEASURE_WAIT (the gate sets it).
#
# -Sanitize address,undefined: the game modules built with those sanitizers (native/CMakeLists.txt NEBULA_SANITIZE),
# their runtime (LLVM's clang_rt.asan_dynamic) in the bin folder. The programs stay as they are: one AddressSanitizer
# runtime serves a process, and Microsoft's compiler would bring its own. Use a ROOT of its own.
# -Coverage: the game modules and the programs count the lines and functions they run (native/CMakeLists.txt and
# CMakeLists.txt NEBULA_COVERAGE; the programs then built by clang-cl), each process writing them where LLVM_PROFILE_FILE
# says (tools/gate/coverage.py). Use a ROOT of its own.
param(
    [Parameter(Mandatory = $true)][string]$Tree,
    [Parameter(Mandatory = $true)][string]$Root,
    [Parameter(Mandatory = $true)][string]$Bin,
    [string]$Pak = "",
    [string]$Dxc = "D:/NebulaWork/build/luma-bins/lh7/dxc",
    [string]$Ffx = "D:/NebulaWork/deps/ffx-fg/FidelityFX-SDK-1.1.4",
    [string]$Llvm = "C:/Program Files/LLVM/bin",
    [int]$Jobs = 12,
    # A configured build folder whose compiled shader stages (generated/*.bin.stages, keyed by content) start a new
    # build's, so the first build does not compile every program again.
    [string]$SeedFrom = "",
    # Sanitizers for the game modules (clang -fsanitize=...), e.g. address,undefined; empty for none.
    [string]$Sanitize = "",
    [switch]$Coverage,
    # The compiler cache in front of clang and cl (N-1049): one cache for every tree and build folder, keyed by the
    # paths as they are. Empty, or no such program: none.
    [string]$Sccache = "D:/NebulaWork/tools/sccache/sccache.exe",
    [string]$SccacheDir = "D:/NebulaWork/sccache",
    # A build started by hand waits while a speed measure holds this lock (tools/gate/measure_wait.py), at most this many minutes.
    [string]$MeasureLock = "D:/NebulaWork/plan/measure.lock",
    [double]$MeasureWaitMinutes = 40
)
# Started by hand (the gate and a landing set NEBULA_NO_MEASURE_WAIT for what they start): one line and a wait while a speed
# measure runs, so the build does not spoil its figures; a measure that still runs after the minutes given ends the script with 3, nothing built.
if (-not $env:NEBULA_NO_MEASURE_WAIT) {
    $waitPython = Get-Command python -ErrorAction SilentlyContinue
    if ($waitPython) {
        & $waitPython.Source -X utf8 -I (Join-Path $PSScriptRoot "measure_wait.py") --lock $MeasureLock --minutes $MeasureWaitMinutes --stop
        if ($LASTEXITCODE -ne 0) { exit 3 }
    }
}
$ErrorActionPreference = "Stop"
$Tree = (Resolve-Path $Tree).Path.Replace("\", "/")
New-Item -ItemType Directory -Force $Root | Out-Null
$Root = (Resolve-Path $Root).Path.Replace("\", "/")
$env:PATH = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;$env:PATH"
$vs = & vswhere.exe -latest -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja = (Get-Command ninja).Source.Replace("\", "/")
# The compiler cache: its server starts with the first build and reads these then (it stops after 10 idle minutes).
$launch = ""
$cacheBefore = $null
if ($Sccache -and (Test-Path $Sccache)) {
    $Sccache = (Resolve-Path $Sccache).Path.Replace("\", "/")
    $env:SCCACHE_DIR = $SccacheDir
    $env:SCCACHE_CACHE_SIZE = "60G"
    # No path is taken out of what it hashes (SCCACHE_BASEDIRS): an object holds the paths it was compiled with (a
    # test's work folder and extension, __FILE__, the debug information's sources), so another tree's object is never
    # this tree's (N-1049: the unit tests of one tree ran another's test extension). A server of its own port, as one
    # started with base folders keeps them until it stops.
    Remove-Item Env:SCCACHE_BASEDIRS -ErrorAction SilentlyContinue
    $env:SCCACHE_SERVER_PORT = "4227"
    $launch = "-DCMAKE_C_COMPILER_LAUNCHER=`"$Sccache`" -DCMAKE_CXX_COMPILER_LAUNCHER=`"$Sccache`" "
    $cacheBefore = (& $Sccache --show-stats --stats-format json 2>$null | ConvertFrom-Json)
}
# A build folder configured with another launcher (or none) is configured again with this one; the game folder also when it holds other
# sanitizers or another coverage setting than asked (Game-Config-Differs, build_steps.ps1).
function Launcher-Differs([string]$Folder) {
    if (-not (Test-Path "$Folder/CMakeCache.txt")) { return $false }
    $line = Select-String -Path "$Folder/CMakeCache.txt" -Pattern '^CMAKE_CXX_COMPILER_LAUNCHER:[A-Z]+=(.*)$' | Select-Object -First 1
    $have = if ($line) { $line.Matches[0].Groups[1].Value } else { "" }
    $want = if ($launch) { $Sccache } else { "" }
    return $have -ne $want
}
# A folder built through the cache while it took the trees' paths out may hold objects another tree compiled: they go,
# once, and the build makes them again (sccache-own-paths says it is done; a new folder is so from the start).
function Drop-Shared-Objects([string]$Folder) {
    if (-not $launch -or (Test-Path "$Folder/sccache-own-paths")) { return }
    if (Test-Path "$Folder/CMakeCache.txt") {
        Get-ChildItem -Path $Folder -Recurse -File -Include *.obj, *.o -ErrorAction SilentlyContinue | Remove-Item -Force
    }
    New-Item -ItemType Directory -Force $Folder | Out-Null
    Set-Content -Path "$Folder/sccache-own-paths" -Value "objects compiled with this folder's own paths (N-1049)" -Encoding ascii
}

# An object whose compile read another work tree's files (a folder another tree built into) is never made again when this tree's files change:
# ninja looks at the other tree's. Those objects, and only those, go (tools/gate/foreign_objects.py, R-004); a sound folder is untouched.
function Drop-Foreign-Objects([string]$Folder) {
    if (-not (Test-Path "$Folder/.ninja_deps")) { return }
    $py = Get-Command python -ErrorAction SilentlyContinue
    if (-not $py) { return }
    # It answers in one line and deletes nothing when it cannot tell; whatever it does, the build goes on as it would have.
    try { & $py.Source -X utf8 -I (Join-Path $PSScriptRoot "foreign_objects.py") drop $Folder --tree $Tree } catch { "foreign_objects: not run ($_); the build goes on" }
}

. "$PSScriptRoot/build_steps.ps1"   # Start-Step, Finish-Step, Step: a step that failed in the compiler cache alone is run again

# The game modules (both), the headless host, the self-description and the services list.
$game = "$Root/game"
Drop-Shared-Objects $game
if (-not (Test-Path "$game/build.ninja") -or (Launcher-Differs $game) -or (Game-Config-Differs $game $Sanitize ([bool]$Coverage))) {
    Step "game configure" ("`"$cmake`" -S `"$Tree/native`" -B `"$game`" -G Ninja -DCMAKE_MAKE_PROGRAM=`"$ninja`" $launch" +
        "-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=`"$Llvm/clang.exe`" -DCMAKE_CXX_COMPILER=`"$Llvm/clang++.exe`" " +
        "-DCMAKE_RC_COMPILER=`"$Llvm/llvm-rc.exe`" -DNEBULA_PETARI_DIR=`"$Tree/native/petari`" " +
        "-DNEBULA_SANITIZE=`"$Sanitize`" -DNEBULA_COVERAGE=$(if ($Coverage) { 'ON' } else { 'OFF' })") "$Root/game.configure.log"
}
# After configuring, a folder that still holds other settings than asked is refused: a build never runs with other ones, silently.
$config = Get-Game-Config $game
if ($config.Sanitize -ne $Sanitize) { "game: $game was configured with NEBULA_SANITIZE='$($config.Sanitize)', asked for '$Sanitize'"; exit 1 }
if ($config.Counted -ne [bool]$Coverage) { "game: $game was configured with NEBULA_COVERAGE=$($config.Counted), asked for $([bool]$Coverage)"; exit 1 }
# The game and the MSVC build run side by side, each with half the jobs.
$half = [Math]::Max(2, [int]($Jobs / 2))
$gameWatch = [Diagnostics.Stopwatch]::StartNew()
$gameRun = Start-Step "`"$cmake`" --build `"$game`" --target all -j $half -- -k 0" "$Root/game.build.log"

# Every Windows program, the unit tests and the test extension, in one MSVC build.
$msvc = "$Root/msvc"
New-Item -ItemType Directory -Force $msvc | Out-Null
Drop-Shared-Objects $msvc
Drop-Foreign-Objects $msvc
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
# A coverage build's programs: clang-cl, its linker and resource compiler (CMakeLists.txt NEBULA_COVERAGE).
$compilers = "-DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DNEBULA_COVERAGE=OFF "
if ($Coverage) {
    $compilers = "-DCMAKE_C_COMPILER=`"$Llvm/clang-cl.exe`" -DCMAKE_CXX_COMPILER=`"$Llvm/clang-cl.exe`" " +
        "-DCMAKE_LINKER=`"$Llvm/lld-link.exe`" -DCMAKE_RC_COMPILER=`"$Llvm/llvm-rc.exe`" -DNEBULA_COVERAGE=ON "
}
# A folder configured the other way (another compiler) is configured again from nothing.
$counting = { (Test-Path "$msvc/CMakeCache.txt") -and [bool](Select-String -Path "$msvc/CMakeCache.txt" -Pattern '^NEBULA_COVERAGE:BOOL=ON$' -Quiet) }
if ((Test-Path "$msvc/CMakeCache.txt") -and ((& $counting) -ne [bool]$Coverage)) {
    Remove-Item -Force "$msvc/CMakeCache.txt"
    Remove-Item -Recurse -Force "$msvc/CMakeFiles" -ErrorAction SilentlyContinue
    Remove-Item -Force "$msvc/build.ninja" -ErrorAction SilentlyContinue
}
if (-not (Test-Path "$msvc/build.ninja") -or -not (Test-Path "$msvc/CMakeCache.txt") -or (Launcher-Differs $msvc)) {
    Step "msvc configure" ("`"$cmake`" -G Ninja -S `"$Tree`" -B `"$msvc`" -DCMAKE_BUILD_TYPE=Release -DNEBULA_VULKAN=ON $launch" +
        "-DNEBULA_D3D11=ON $compilers-DFETCHCONTENT_SOURCE_DIR_FIDELITYFX_SDK=$Ffx " +
        "`"-DNEBULA_EXTRA_CMAKE=$Tree/tools/test_driver/test_driver.cmake`"") "$Root/msvc.configure.log"
}
if ((& $counting) -ne [bool]$Coverage) { "msvc: $msvc is not configured with NEBULA_COVERAGE=$([bool]$Coverage)"; exit 1 }
$msvcWatch = [Diagnostics.Stopwatch]::StartNew()
$msvcRun = Start-Step "`"$cmake`" --build `"$msvc`" -j $half -- -k 0" "$Root/msvc.build.log"
Finish-Step "game" $gameRun "$Root/game.build.log" $gameWatch "`"$cmake`" --build `"$game`" --target all -j $half -- -k 0"
Finish-Step "msvc" $msvcRun "$Root/msvc.build.log" $msvcWatch "`"$cmake`" --build `"$msvc`" -j $half -- -k 0"

# The harness's game half: a LumaCore mod library, built as build tools build mods (no library linked, unwind tables).
$inc = "$Tree/native/include"
Step "luma-test" ("`"$Llvm/clang.exe`" --target=x86_64-pc-windows-msvc -O2 -shared -nostdlib -ffreestanding " +
    "-fasynchronous-unwind-tables -std=c11 -fno-stack-protector -fvisibility=hidden -fuse-ld=lld -Wl,/NOENTRY " +
    "-Wl,/NOIMPLIB -Wall -Wextra -Werror -I `"$inc`" -o `"$Root/luma-test.dll`" `"$Tree/tools/test_driver/luma_test/luma_test.c`"") `
    "$Root/luma-test.log"

# The importer the harness cooks test mods with.
Step "nebula-import" ("cd /d `"$Tree`" && set CARGO_TARGET_DIR=$Root/cargo&& cargo build --release --locked --offline " +
    "-p nebula-import --bin nebula-import --lib") "$Root/cargo.log"

# What the cache gave, this build's share of the server's counts (another build at the same time is counted too).
if ($launch) {
    $after = (& $Sccache --show-stats --stats-format json 2>$null | ConvertFrom-Json)
    if ($after -and $cacheBefore) {
        $sum = { param($c) $n = 0; if ($c) { $c.PSObject.Properties | ForEach-Object { $n += [int64]$_.Value } }; $n }
        $hits = (& $sum $after.stats.cache_hits.counts) - (& $sum $cacheBefore.stats.cache_hits.counts)
        $misses = (& $sum $after.stats.cache_misses.counts) - (& $sum $cacheBefore.stats.cache_misses.counts)
        $other = $after.stats.requests_not_cacheable - $cacheBefore.stats.requests_not_cacheable
        "sccache: $hits hits, $misses misses, $other not cacheable; cache $([int]($after.cache_size / 1MB)) MB in $SccacheDir"
    }
}
if ($script:failed -ne 0) { "build failed"; exit 1 }
if ($script:cacheOnly -ne 0) { "build not judged: the compiler cache failed twice in the same way, not the compiler"; exit 2 }

# The bin folder: what luma_test.py runs (the programs, the game modules, the game half, the importer, the
# self-description, the shader compiler and the pak's path).
$pv = $null
if ($Pak) {
    # The header only (the pak is several gigabytes).
    $head = New-Object byte[] 12
    $stream = [IO.File]::OpenRead($Pak)
    try { $null = $stream.Read($head, 0, 12) } finally { $stream.Dispose() }
    if ([Text.Encoding]::ASCII.GetString($head, 0, 7) -eq "NBSTORE") { $pv = [BitConverter]::ToUInt32($head, 8) }
    $tv = (Get-Content "$Tree/runtime/include/galaxy/store_version.inc" -Raw).Trim()
    if ("$pv" -ne $tv) {
        "bins: $Pak is store version $pv, the tree's is ${tv}: no pak.path is written; the gate asks for the right data by key"
        $Pak = ""
    }
}
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
if ($Pak) { Set-Content -Path "$Bin/pak.path" -Value $Pak -Encoding ascii }
$commit = & git -C $Tree rev-parse --short HEAD
Set-Content -Path "$Bin/commit.txt" -Value $commit -Encoding ascii
"bins ok $commit"
