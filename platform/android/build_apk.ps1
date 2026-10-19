# SPDX-License-Identifier: GPL-3.0-only
# The Android app (docs/ANDROID.md), from source to a signed APK:
#   1. per ABI: the game module (native/, libsmg_game.so), the SDL host with the Vulkan renderer (platform/sdl,
#      libmain.so and libSDL3.so) and setup's importer (crates/nebula-import, libnebula_import.so), with the NDK;
#   2. the license texts the app shows;
#   3. Gradle: the launcher, SDL's Java half and the libraries, signed with the key -Signing names (debug key without).
#
#   build_apk.ps1 -Deps <folder of SDL3-3.4.18, glslang-src, Vulkan-Headers, volk, VMA> -Work <build folder>
#                 [-SeedTool <vk_shader_seed.exe>] [-Signing <signing.properties>] [-Abis arm64-v8a,x86_64]
param(
    [Parameter(Mandatory = $true)][string]$Deps,
    [Parameter(Mandatory = $true)][string]$Work,
    [string]$SeedTool = "",
    [string]$Signing = "",
    [string]$Abis = "arm64-v8a,x86_64"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot\..\..").Path
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
New-Item -ItemType Directory -Force $Work | Out-Null

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
foreach ($abi in $Abis -split ",") {
    $triple = @{ "arm64-v8a" = "aarch64-linux-android"; "x86_64" = "x86_64-linux-android" }[$abi]
    if (-not $triple) { throw "unknown ABI $abi" }
    "== $abi"
    $module = "$Work/module-$abi" -replace '\\', '/'
    Invoke-Logged "$Work\module-$abi.log" {
        cmake -G Ninja -S "$root/native" -B $module "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DANDROID_ABI=$abi" `
            "-DANDROID_PLATFORM=android-31" -DCMAKE_BUILD_TYPE=Release "-DNEBULA_PETARI_DIR=$root/native/petari"
        if ($LASTEXITCODE -eq 0) { cmake --build $module --target smg_game }
    }
    $hostBuild = "$Work/host-$abi" -replace '\\', '/'
    Invoke-Logged "$Work\host-$abi.log" {
        cmake -G Ninja -S "$root/platform/sdl" -B $hostBuild "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DANDROID_ABI=$abi" `
            "-DANDROID_PLATFORM=android-31" -DCMAKE_BUILD_TYPE=Release "-DNEBULA_DEPS=$depsSlash"
        if ($LASTEXITCODE -eq 0) { cmake --build $hostBuild --target main }
    }
    $upper = ($triple -replace '-', '_').ToUpper()
    $lower = $triple -replace '-', '_'
    Set-Item "env:CARGO_TARGET_${upper}_LINKER" "$llvm\${triple}31-clang.cmd"
    Set-Item "env:CC_$lower" "$llvm\${triple}31-clang.cmd"
    Set-Item "env:CXX_$lower" "$llvm\${triple}31-clang++.cmd"
    Set-Item "env:AR_$lower" "$llvm\llvm-ar.exe"
    $env:CARGO_TARGET_DIR = "$Work\cargo"
    Invoke-Logged "$Work\import-$abi.log" {
        Push-Location $root
        try { cargo build --release --locked -p nebula-import --lib --target $triple } finally { Pop-Location }
    }
    $dest = Join-Path $jni $abi
    New-Item -ItemType Directory -Force $dest | Out-Null
    Copy-Item "$module/libsmg_game.so", "$hostBuild/libmain.so", "$hostBuild/SDL3/libSDL3.so",
        "$Work\cargo\$triple\release\libnebula_import.so" $dest -Force
}

# The pipelines the whole game uses (platform/android/vk_seed_keys.bin, captured by the PC's galaxy tour):
# GameActivity installs them beside the pipeline cache, which builds them from the first launch. -SeedTool also ships
# their SPIR-V (vk_seed_shaders.bin); the device uses it only when its compiler is the tool's (compiler_identity), so a
# tool built against another glslang build than the app's adds size and nothing else.
New-Item -ItemType Directory -Force (Join-Path $Work "assets") | Out-Null
$seedKeys = Join-Path $root "platform\android\vk_seed_keys.bin"
Copy-Item $seedKeys (Join-Path $Work "assets\vk_seed_keys.bin") -Force
Remove-Item (Join-Path $Work "assets\vk_seed_shaders.bin") -ErrorAction SilentlyContinue
if ($SeedTool) {
    & $SeedTool $seedKeys (Join-Path $Work "assets\vk_seed_shaders.bin")
    if ($LASTEXITCODE -ne 0) { throw "vk_shader_seed failed" }
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
    "rust-dependencies-android.txt" = @("$root\docs\licenses\rust-dependencies-android.txt")
    "Apache-2.0.txt" = @("$Deps\Vulkan-Headers-vulkan-sdk-1.4.350.1\LICENSES\Apache-2.0.txt")
}
foreach ($name in $texts.Keys) {
    $parts = foreach ($file in $texts[$name]) {
        if (-not (Test-Path $file)) { throw "license text missing: $file" }
        [IO.File]::ReadAllText($file)
    }
    [IO.File]::WriteAllText((Join-Path $licenses $name), ($parts -join "`n`n"), [Text.UTF8Encoding]::new($false))
}

# Gradle, with Android Studio's JDK when no other is set.
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = "${env:ProgramFiles}\Android\Android Studio\jbr" }
$env:ANDROID_HOME = $sdk
$properties = @("-Pnebula.sdlDir=$Deps\SDL3-3.4.18", "-Pnebula.jniDir=$jni", "-Pnebula.assetsDir=$Work\assets")
if ($Signing -ne "") { $properties += "-Pnebula.signing=$Signing" }
Push-Location $android
try {
    Invoke-Logged "$Work\gradle.log" { & .\gradlew.bat --no-daemon assembleRelease @properties }
} finally { Pop-Location }
$apk = Join-Path $android "app\build\outputs\apk\release\app-release.apk"
Copy-Item $apk (Join-Path $Work "Nebula.apk") -Force
"apk: $(Join-Path $Work 'Nebula.apk') ($([math]::Round((Get-Item $apk).Length / 1MB, 1)) MB)"
