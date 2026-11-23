# SPDX-License-Identifier: GPL-3.0-only
# The Android app, from source to a signed APK:
#   1. per ABI: the game modules (native/, libsmg_game.so and libsmg_game_plain.so), the SDL host with the Vulkan
#      renderer (platform/sdl, libmain.so and libSDL3.so), the launcher's mod importer (platform/sdl,
#      libnebula_mods.so) and setup's importer (crates/nebula-import, libnebula_import.so), with the NDK;
#   2. the license texts the app shows, and the mods that come with the game (-ShippedMods);
#   3. Gradle: the launcher, SDL's Java half and the libraries, signed with the key -Signing names (debug key without).
#
#   build_apk.ps1 -Deps <folder of SDL3-3.4.18, glslang-src, Vulkan-Headers, volk, VMA> -Work <build folder>
#                 [-SeedTool <vk_shader_seed.exe>] [-Signing <signing.properties>] [-Abis arm64-v8a,x86_64]
#                 [-FfxShaders <FidelityFX's Vulkan shader headers, made already>]
#                 [-Debuggable: the APK can be debugged (run-as) on a test device]
#                 [-Harness <the folder of a test runner's Harness.java>: test builds; without it the app runs none.
#                  With it (and -Debuggable, so the app is dev.nebula.galaxy.test beside the game) the APK carries the
#                  test extension, and <Work>\test-bin-<ABI> is the Android runner's bin folder: libsmg_game.so,
#                  libsmg_game_plain.so, libnebula_test_extension.so, the game half luma-test.so, luma_types.json]
#                 [-Plain: no ThinLTO for the game module and host; links in a fraction of the time, for development]
#                 [-ReleaseKey <the release key's private XML>: Nebula.apk.sig, the release-key signature the app's
#                  updates check (Updates.kt); default the Windows setups' key at its usual place, '' for none]
#                 [-ShippedMods <the folder of the .luma files that come with the game: LumaKit>]
#                 [-NoMods: an app with no mod support (CMake NEBULA_MODS=OFF; the environment variable NEBULA_MODS=OFF does
#                  the same): only the plain game module, no mod importer library, no LumaKit (-ShippedMods is refused), no
#                  mod entry in the manifest and no mod page or text in the launcher. A test build (-Harness) still builds
#                  both game modules: the test runner's game half is a mod]
#   The environment variable NEBULA_VERSION_SUFFIX (-beta.<n>) gives the version its pre-release label (cmake/nebula_options.cmake).
#                 [-Unsigned: Nebula-unsigned.apk, a release build with no signature (and no .sig), which
#                  installer/sign-release.ps1 signs with the release key; the release workflow builds this]
#                 [-UnitTests: also the launcher's JVM tests (app/src/test), in the same Gradle run]
#   A release publishes the two as Nebula-Android.apk and Nebula-Android.apk.sig (a beta: Nebula-Beta-Android.apk).
param(
    [Parameter(Mandatory = $true)][string]$Deps,
    [Parameter(Mandatory = $true)][string]$Work,
    [Parameter(Mandatory = $true)][string]$HostTools,
    [string]$SeedTool = "",
    [string]$Signing = "",
    [string]$Abis = "arm64-v8a,x86_64",
    [string]$FfxShaders = "",
    [switch]$Debuggable,
    # Settings > ADVANCED (any galaxy, the Grand Stars) in every run: test builds only, never a release.
    [switch]$TestTools,
    [string]$Harness = "",
    [switch]$Plain,
    [string]$ReleaseKey = (Join-Path $env:USERPROFILE '.nebula-release-key\nebula-release-private.xml'),
    # The .luma files that come with the game (LumaKit, from LumaLink's `lumalink build` and `lumalink pack`): the app
    # carries them in its assets' mods folder and the game installs them at start. Empty: an app without them (a test
    # build); its mods that need LumaKit then need it installed by hand.
    [string]$ShippedMods = "",
    [switch]$NoMods,
    [switch]$Unsigned,
    [switch]$UnitTests
)
$ErrorActionPreference = "Stop"
if ($env:NEBULA_MODS -eq "OFF") { $NoMods = $true }
if ($NoMods) {
    if ($ShippedMods -ne "") { throw "-NoMods and -ShippedMods exclude each other" }
    # Every CMake project reads this (cmake/nebula_options.cmake).
    $env:NEBULA_MODS = "OFF"
}
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
# A test build is its own app (dev.nebula.galaxy.test, app/build.gradle.kts): only a debuggable build gets that id, so
# a harness build that is not debuggable would replace the player's game.
if ($Harness -ne "" -and -not $Debuggable) { throw "-Harness needs -Debuggable: a test build is its own app beside the game" }
$android = Join-Path $root "platform\android"
$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA "Android\Sdk" }
$ndk = Join-Path $sdk "ndk\28.2.13676358"
$toolchain = (Join-Path $ndk "build\cmake\android.toolchain.cmake") -replace '\\', '/'
$llvm = Join-Path $ndk "toolchains\llvm\prebuilt\windows-x86_64\bin"
if (-not (Test-Path $toolchain)) { throw "NDK 28.2.13676358 not found under $sdk" }
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$cmakeDir = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake"
$env:PATH = "$cmakeDir\CMake\bin;$cmakeDir\Ninja;$env:PATH"
$depsSlash = $Deps -replace '\\', '/'
$hostToolsSlash = $HostTools -replace '\\', '/'
# FidelityFX's source when it is here already (downloaded and checked otherwise), its shaders likewise.
$ffxArgs = @()
if (Test-Path "$Deps\ffx-src\FidelityFX-SDK-1.1.4\sdk") { $ffxArgs += "-DFETCHCONTENT_SOURCE_DIR_FIDELITYFX_SDK=$depsSlash/ffx-src/FidelityFX-SDK-1.1.4" }
if ($FfxShaders) { $ffxArgs += "-DNEBULA_FFX_VK_SHADERS=$($FfxShaders -replace '\\', '/')" }
# The players' build: ThinLTO for the game module (native/CMakeLists.txt NEBULA_LTO) and the host (NEBULA_HOST_IPO).
$lto = if ($Plain) { "OFF" } else { "ON" }
New-Item -ItemType Directory -Force $Work | Out-Null
# Plan 2.9: every platform's release items (permissions, usage texts, file and link associations) are in the tree.
& python (Join-Path $root "tools\check_platform_items.py") $root
if ($LASTEXITCODE -ne 0) { throw "tools/check_platform_items.py: a platform item is missing" }

# A step's output to a UTF-8 log; its errors shown and the build stopped when it fails. Tools write progress to
# stderr, so stderr lines are output here, not errors.
function Invoke-Logged([string]$Log, [scriptblock]$Command) {
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $Command 2>&1 | ForEach-Object { "$_" } | Out-File -Encoding utf8 $Log
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previous
    if ($code -ne 0) {
        Get-Content $Log | Select-String -Pattern "error|FAILED|What went wrong" | Select-Object -First 25 |
            ForEach-Object { $_.Line }
        throw "failed (exit $code): see $Log"
    }
}

$jni = Join-Path $android "jniLibs"
# The game module that carries the mod rows: not in an app with no mod support, unless it is a test build. (Typed arrays: a
# one-name result of an if would be a string, and splatting a string passes its characters.)
$rowsModule = (-not $NoMods) -or ($Harness -ne "")
[string[]]$gameTargets = if ($rowsModule) { @("smg_game", "smg_game_plain") } else { @("smg_game_plain") }
[string[]]$hostTargets = if ($NoMods) { @("main") } else { @("main", "nebula_mods") }
foreach ($abi in $Abis -split ",") {
    $triple = @{ "arm64-v8a" = "aarch64-linux-android"; "x86_64" = "x86_64-linux-android" }[$abi]
    if (-not $triple) { throw "unknown ABI $abi" }
    "== $abi"
    $module = "$Work/module-$abi" -replace '\\', '/'
    Invoke-Logged "$Work\module-$abi.log" {
        cmake -G Ninja -S "$root/native" -B $module "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DANDROID_ABI=$abi" `
            "-DANDROID_PLATFORM=android-31" -DCMAKE_BUILD_TYPE=Release "-DNEBULA_PETARI_DIR=$root/native/petari" "-DNEBULA_LTO=$lto"
        # Both game modules: the host loads libsmg_game_plain.so unless a mod's code needs the rows (no mod support: the
        # plain one only, but in a test build, whose game half is a mod).
        if ($LASTEXITCODE -eq 0) { cmake --build $module --target @gameTargets }
    }
    $hostBuild = "$Work/host-$abi" -replace '\\', '/'
    Invoke-Logged "$Work\host-$abi.log" {
        cmake -G Ninja -S "$root/platform/sdl" -B $hostBuild "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DANDROID_ABI=$abi" `
            "-DANDROID_PLATFORM=android-31" -DCMAKE_BUILD_TYPE=Release "-DNEBULA_DEPS=$depsSlash" "-DNEBULA_HOST_TOOLS=$hostToolsSlash" "-DNEBULA_HOST_IPO=$lto" "-DNEBULA_TEST_TOOLS=$(if ($TestTools) { 'ON' } else { 'OFF' })" @ffxArgs
        if ($LASTEXITCODE -eq 0) { cmake --build $hostBuild --target @hostTargets }
    }
    $upper = ($triple -replace '-', '_').ToUpper()
    $lower = $triple -replace '-', '_'
    Set-Item "env:CARGO_TARGET_${upper}_LINKER" "$llvm\${triple}31-clang.cmd"
    Set-Item "env:CC_$lower" "$llvm\${triple}31-clang.cmd"
    Set-Item "env:CXX_$lower" "$llvm\${triple}31-clang++.cmd"
    Set-Item "env:AR_$lower" "$llvm\llvm-ar.exe"
    $env:CARGO_TARGET_DIR = "$Work\cargo"
    # Source paths the library keeps (panic messages, the C its crates build) name no build machine's folders.
    $cargoHome = if ($env:CARGO_HOME) { $env:CARGO_HOME } else { Join-Path $env:USERPROFILE ".cargo" }
    $rustupHome = if ($env:RUSTUP_HOME) { $env:RUSTUP_HOME } else { Join-Path $env:USERPROFILE ".rustup" }
    $remaps = @(@($cargoHome, "cargo"), @($rustupHome, "rustup"), @($root, "nebula"))
    $env:CARGO_ENCODED_RUSTFLAGS = ($remaps | ForEach-Object { "--remap-path-prefix=$($_[0])=$($_[1])" }) -join [char]0x1f
    Set-Item "env:CFLAGS_$lower" (($remaps | ForEach-Object {
        "-ffile-prefix-map=$($_[0])=$($_[1]) -ffile-prefix-map=$($_[0] -replace '\\', '/')=$($_[1])" }) -join " ")
    Invoke-Logged "$Work\import-$abi.log" {
        Push-Location $root
        try { cargo build --profile dist --locked -p nebula-import --lib --target $triple } finally { Pop-Location }
    }
    $dest = Join-Path $jni $abi
    New-Item -ItemType Directory -Force $dest | Out-Null
    # The test extension only in a test build: one left in jniLibs by an earlier test build never reaches another.
    Remove-Item -LiteralPath (Join-Path $dest "libnebula_test_extension.so") -Force -ErrorAction SilentlyContinue
    # Files an earlier build with mod support left in jniLibs never reach one without.
    if (-not $rowsModule) { Remove-Item -LiteralPath (Join-Path $dest "libsmg_game.so") -Force -ErrorAction SilentlyContinue }
    if ($NoMods) { Remove-Item -LiteralPath (Join-Path $dest "libnebula_mods.so") -Force -ErrorAction SilentlyContinue }
    $built = @("$module/libsmg_game_plain.so", "$hostBuild/libmain.so", "$hostBuild/SDL3/libSDL3.so", "$Work\cargo\$triple\dist\libnebula_import.so")
    if ($rowsModule) { $built += "$module/libsmg_game.so" }
    if (-not $NoMods) { $built += "$hostBuild/libnebula_mods.so" }
    Copy-Item $built $dest -Force
    # libadrenotools' hooks for an imported Vulkan driver (arm64 only; platform/sdl/CMakeLists.txt).
    if ($abi -eq "arm64-v8a") {
        Copy-Item "$hostBuild/adrenotools/src/hook/libhook_impl.so", "$hostBuild/adrenotools/src/hook/libmain_hook.so" $dest -Force
    }
    if ($Harness -ne "") {
        # The test runs' extension (the test driver's test_driver.cmake sources) in the app, and the runner's bin
        # folder: the game modules, the extension, the game half (built as tools/gate/build_windows.ps1 builds it for
        # Windows) and the game's self-description. The test driver is the folder two above the harness's.
        $driver = Split-Path (Split-Path $Harness -Parent) -Parent
        $testBin = Join-Path $Work "test-bin-$abi"
        New-Item -ItemType Directory -Force $testBin | Out-Null
        $ext = "$driver/extension"
        Invoke-Logged "$Work\test-extension-$abi.log" {
            & "$llvm\${triple}31-clang++.cmd" -std=c++20 -O2 -fPIC -shared -static-libstdc++ "-Wl,--no-undefined" `
                -Wall -Wextra -Werror "-I$root/runtime/include" "-I$root/native/include" `
                "$ext/test_extension.cpp" "$ext/harness.cpp" "$ext/test_services.cpp" "$ext/data_guard.cpp" "$ext/process_memory.cpp" `
                "$root/runtime/src/hidden_start.cpp" -ldl -llog -o "$testBin/libnebula_test_extension.so"
        }
        Invoke-Logged "$Work\test-half-$abi.log" {
            & "$llvm\${triple}31-clang.cmd" -O2 -shared -fPIC -nostdlib -ffreestanding -fno-stack-protector `
                -fvisibility=hidden -fno-use-init-array -fuse-ld=lld "-Wl,-T,$root/native/include/luma_ctors.ld" `
                "-Wl,--no-undefined" "-Wl,-z,defs" -Wall -Wextra -Werror "-I$root/native/include" `
                "$driver/luma_test/luma_test.c" -o "$testBin/luma-test.so"
        }
        Copy-Item "$testBin/libnebula_test_extension.so" $dest -Force
        Copy-Item "$module/libsmg_game.so", "$module/libsmg_game_plain.so", "$module/luma_types/luma_types.json" $testBin -Force
    }
}

# The mods that come with the game (GameFiles.shippedMods unpacks them, the game installs them).
$assetMods = Join-Path $Work "assets\mods"
if (Test-Path -LiteralPath $assetMods) { Remove-Item -Recurse -Force -LiteralPath $assetMods }
if ($NoMods) {
    # No mod support: the app carries no LumaKit.
} elseif ($ShippedMods) {
    $shipped = @(Get-ChildItem -LiteralPath $ShippedMods -File -Filter '*.luma')
    if (-not $shipped.Count) { throw "The shipped mods folder has no .luma file: $ShippedMods" }
    New-Item -ItemType Directory -Force $assetMods | Out-Null
    foreach ($file in $shipped) { Copy-Item -LiteralPath $file.FullName -Destination $assetMods }
} else {
    Write-Warning "No -ShippedMods: the app carries no LumaKit."
}

# The license texts the About screen opens (assets/licenses; the list is app/src/main/kotlin/.../Licenses.kt).
$licenses = Join-Path $Work "assets\licenses"
New-Item -ItemType Directory -Force $licenses | Out-Null
$texts = [ordered]@{
    "GPL-3.0.txt" = @("$root\LICENSE")
    "NOTICES.md" = @("$root\THIRD-PARTY-NOTICES.md")
    "Petari.txt" = @("$root\native\petari\LICENSE")
    "SDL.txt" = @("$Deps\SDL3-3.4.18\LICENSE.txt")
    "glslang.txt" = @("$Deps\glslang-src\LICENSE.txt")
    "SPIRV-Tools.txt" = @("$Deps\glslang-src\External\spirv-tools\LICENSE")
    "SPIRV-Headers.txt" = @("$Deps\glslang-src\External\spirv-tools\external\spirv-headers\LICENSE")
    "Vulkan-Headers.txt" = @("$Deps\Vulkan-Headers-vulkan-sdk-1.4.350.1\LICENSE.md",
        "$Deps\Vulkan-Headers-vulkan-sdk-1.4.350.1\LICENSES\Apache-2.0.txt",
        "$Deps\Vulkan-Headers-vulkan-sdk-1.4.350.1\LICENSES\MIT.txt")
    "volk.txt" = @("$Deps\volk-vulkan-sdk-1.4.350.1\LICENSE.md")
    "VulkanMemoryAllocator.txt" = @("$Deps\VulkanMemoryAllocator-v3.4.0\LICENSE.txt")
    "libadrenotools.txt" = @("$Deps\libadrenotools-8fae8ce\LICENSE", "$Deps\libadrenotools-8fae8ce\lib\linkernsbypass\LICENSE")
    "rust-dependencies-android.txt" = @("$root\docs\licenses\rust-dependencies-android.txt")
    "libarchive.txt" = @("$root\third_party\archive\libarchive\COPYING")
    "xz.txt" = @("$root\third_party\archive\xz\COPYING.0BSD")
    "zlib.txt" = @("$root\third_party\archive\zlib\LICENSE")
    "Apache-2.0.txt" = @("$Deps\Vulkan-Headers-vulkan-sdk-1.4.350.1\LICENSES\Apache-2.0.txt")
}
foreach ($name in $texts.Keys) {
    $parts = foreach ($file in $texts[$name]) {
        if (-not (Test-Path $file)) { throw "license text missing: $file" }
        [IO.File]::ReadAllText($file)
    }
    [IO.File]::WriteAllText((Join-Path $licenses $name), ($parts -join "`n`n"), [Text.UTF8Encoding]::new($false))
}

# Every activity method the native code calls by name (platform/sdl: call_activity("name"), ActivityCall{"name", ...})
# is in the built APK's GameActivity: R8 removes what Java never calls, and the native call then finds nothing.
function Test-ActivityCalls([string]$Apk) {
    $names = [System.Collections.Generic.SortedSet[string]]::new()
    foreach ($file in Get-ChildItem (Join-Path $root "platform\sdl") -Filter *.cpp) {
        $text = [IO.File]::ReadAllText($file.FullName)
        foreach ($m in [regex]::Matches($text, 'call_activity\("([A-Za-z_]\w*)"\)|ActivityCall\{"([A-Za-z_]\w*)"')) {
            [void]$names.Add($(if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Groups[2].Value }))
        }
    }
    if ($names.Count -eq 0) { throw "No activity calls found in platform/sdl: the check would pass on nothing." }
    $buildTools = Get-ChildItem (Join-Path $sdk "build-tools") -Directory | Sort-Object { [version]($_.Name -replace '[^\d.].*$', '') } | Select-Object -Last 1
    $dexdump = Join-Path $buildTools.FullName "dexdump.exe"
    if (-not (Test-Path $dexdump)) { throw "dexdump not found under $($buildTools.FullName)" }
    $dex = Join-Path $Work "dexcheck"
    Remove-Item $dex -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $dex | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Apk)
    try {
        foreach ($entry in $zip.Entries | Where-Object { $_.FullName -match '^classes\d*\.dex$' }) {
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $dex $entry.FullName), $true)
        }
    } finally { $zip.Dispose() }
    $found = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($file in Get-ChildItem $dex -Filter *.dex) {
        $inActivity = $false
        foreach ($line in & $dexdump $file.FullName) {
            if ($line -match "^  Class descriptor\s*: '(.*)'") { $inActivity = $Matches[1] -eq 'Ldev/nebula/galaxy/GameActivity;' }
            elseif ($inActivity -and $line -match "^\s+name\s*: '(\w+)'") { [void]$found.Add($Matches[1]) }
        }
    }
    $missing = @($names | Where-Object { -not $found.Contains($_) })
    if ($missing.Count) { throw "The APK's GameActivity lacks methods the native code calls by name: $($missing -join ', ') (platform/android/app/proguard-rules.pro)." }
    "activity calls: all $($names.Count) in the APK"
}

# Gradle, with Android Studio's JDK when no other is set.
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = "${env:ProgramFiles}\Android\Android Studio\jbr" }
$env:ANDROID_HOME = $sdk
$properties = @("-Pnebula.sdlDir=$Deps\SDL3-3.4.18", "-Pnebula.jniDir=$jni", "-Pnebula.assetsDir=$Work\assets")
if ($Signing -ne "") { $properties += "-Pnebula.signing=$Signing" }
if ($Unsigned) {
    if ($Signing -ne "") { throw "-Unsigned and -Signing exclude each other" }
    $properties += "-Pnebula.unsigned=true"
}
if ($Debuggable) { $properties += "-Pnebula.debuggable=true" }
if ($Harness -ne "") { $properties += "-Pnebula.harness=$Harness" }
if ($NoMods) { $properties += "-Pnebula.mods=false" }
if ($env:NEBULA_VERSION_SUFFIX) { $properties += "-Pnebula.versionSuffix=$($env:NEBULA_VERSION_SUFFIX)" }
Push-Location $android
try {
    $tasks = @("assembleRelease")
    if ($UnitTests) { $tasks += "testReleaseUnitTest" }
    Invoke-Logged "$Work\gradle.log" { & .\gradlew.bat --no-daemon @tasks "-PnebulaAbis=$Abis" @properties }
} finally { Pop-Location }
if ($Unsigned) {
    # installer/sign-release.ps1 signs it and then writes its .sig: one written now would not match the signed file.
    $apk = Join-Path $android "app\build\outputs\apk\release\app-release-unsigned.apk"
    Test-ActivityCalls $apk
    Copy-Item $apk (Join-Path $Work "Nebula-unsigned.apk") -Force
    "apk: $(Join-Path $Work 'Nebula-unsigned.apk') ($([math]::Round((Get-Item $apk).Length / 1MB, 1)) MB, not signed)"
    return
}
$apk = Join-Path $android "app\build\outputs\apk\release\app-release.apk"
Test-ActivityCalls $apk
Copy-Item $apk (Join-Path $Work "Nebula.apk") -Force
"apk: $(Join-Path $Work 'Nebula.apk') ($([math]::Round((Get-Item $apk).Length / 1MB, 1)) MB)"
# The release-key signature over the whole APK (RSA SHA-256, base64), as build-native-setup.ps1 signs the setup.
$signed = Join-Path $Work "Nebula.apk"
if ($ReleaseKey -and (Test-Path -LiteralPath $ReleaseKey)) {
    $publicKey = (Get-Content -LiteralPath (Join-Path $root "installer\release-public-key.xml") -Raw).Trim()
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
    $rsa.PersistKeyInCsp = $false
    $rsa.FromXmlString((Get-Content -LiteralPath $ReleaseKey -Raw))
    if ($rsa.ToXmlString($false) -ne $publicKey) { throw "The release key does not match installer/release-public-key.xml." }
    $signature = $rsa.SignData([IO.File]::ReadAllBytes($signed), [Security.Cryptography.CryptoConfig]::MapNameToOID("SHA256"))
    [IO.File]::WriteAllText("$signed.sig", [Convert]::ToBase64String($signature))
    "signed: $signed.sig"
} elseif ($ReleaseKey) {
    Write-Warning "No release key at $ReleaseKey`: Nebula.apk has no .sig and its updates will not install it."
}
