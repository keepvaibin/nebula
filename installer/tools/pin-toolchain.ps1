<#
.SYNOPSIS
Pins the compiler toolchain that Nebula-Setup downloads.

.DESCRIPTION
Reads one Visual Studio 2022 release manifest published by Microsoft and
writes installer/toolchain.json: the exact MSVC and Windows SDK payloads the
installer downloads, each with the SHA-256 Microsoft publishes for it, plus
the pinned CMake and Ninja release archives. The installer verifies every
download against these hashes before unpacking it.

Run this only when deliberately moving to a new toolchain; module
compatibility keys include the toolchain identity, so a change forces
regeneration on update.
#>
[CmdletBinding()]
param(
    [string]$ChannelUrl = 'https://aka.ms/vs/17/release/channel',
    [string]$MsvcVersion = '14.44.17.14',
    [string]$SdkPackage = 'Win11SDK_10.0.26100',
    [string]$Output = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Output) { $Output = Join-Path (Split-Path -Parent $PSCommandPath) '..\toolchain.json' }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$work = Join-Path ([IO.Path]::GetTempPath()) ("nebula-pin-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $work | Out-Null
$client = New-Object Net.WebClient

function Get-Sha256([string]$Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

try {
    $channelPath = Join-Path $work 'channel.json'
    $client.DownloadFile($ChannelUrl, $channelPath)
    $channel = Get-Content -LiteralPath $channelPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $manifestItem = $channel.channelItems | Where-Object { $_.id -eq 'Microsoft.VisualStudio.Manifests.VisualStudio' }
    $manifestPayload = $manifestItem.payloads[0]
    $manifestPath = Join-Path $work 'VisualStudio.vsman'
    # The channel's manifest hash is not a plain file digest, so the manifest
    # is trusted from Microsoft's HTTPS download host; every payload pinned
    # below carries the SHA-256 Microsoft publishes, and each MSI is checked.
    $client.DownloadFile($manifestPayload.url, $manifestPath)
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $packages = @{}
    foreach ($package in $manifest.packages) {
        if (-not $packages.ContainsKey($package.id)) { $packages[$package.id] = $package }
    }

    $payloads = New-Object Collections.ArrayList
    # C++ runtime headers and libraries only: clang-cl and lld-link compile and link.
    $msvcIds = @(
        "Microsoft.VC.$MsvcVersion.CRT.Headers.base",
        "Microsoft.VC.$MsvcVersion.CRT.x64.Desktop.base",
        "Microsoft.VC.$MsvcVersion.CRT.x64.Store.base",
        # App-local C++ runtime DLLs, copied next to the locally built binaries.
        "Microsoft.VC.$MsvcVersion.CRT.Redist.X64.base"
    )
    foreach ($id in $msvcIds) {
        $package = $packages[$id]
        if ($null -eq $package) { throw "Manifest has no package $id" }
        foreach ($payload in $package.payloads) {
            [void]$payloads.Add([ordered]@{
                    kind = 'vsix'; package = $id; fileName = $payload.fileName
                    url = $payload.url; sha256 = $payload.sha256.ToLowerInvariant(); size = [long]$payload.size
                })
        }
    }

    # SDK components needed to compile and link x64 desktop code, plus rc/mt.
    $sdkMsis = @(
        'Windows SDK for Windows Store Apps Tools-x86_en-us.msi',
        'Windows SDK for Windows Store Apps Headers-x86_en-us.msi',
        'Windows SDK for Windows Store Apps Headers OnecoreUap-x86_en-us.msi',
        'Windows SDK for Windows Store Apps Libs-x86_en-us.msi',
        'Universal CRT Headers Libraries and Sources-x86_en-us.msi',
        'Windows SDK Desktop Headers x86-x86_en-us.msi',
        'Windows SDK Desktop Headers x64-x86_en-us.msi',
        'Windows SDK OnecoreUap Headers x86-x86_en-us.msi',
        'Windows SDK OnecoreUap Headers x64-x86_en-us.msi',
        'Windows SDK Desktop Libs x64-x86_en-us.msi',
        'Windows SDK Desktop Tools x64-x86_en-us.msi'
    )
    $sdk = $packages[$SdkPackage]
    if ($null -eq $sdk) { throw "Manifest has no package $SdkPackage" }
    $byName = @{}
    foreach ($payload in $sdk.payloads) { $byName[[IO.Path]::GetFileName($payload.fileName)] = $payload }
    $cabs = New-Object 'Collections.Generic.SortedSet[string]'
    foreach ($msi in $sdkMsis) {
        $payload = $byName[$msi]
        if ($null -eq $payload) { throw "$SdkPackage has no $msi" }
        $local = Join-Path $work $msi
        $client.DownloadFile($payload.url, $local)
        if ((Get-Sha256 $local) -ne $payload.sha256.ToLowerInvariant()) { throw "$msi failed its manifest hash" }
        $text = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($local))
        foreach ($match in [regex]::Matches($text, '[0-9a-f]{32}\.cab')) { [void]$cabs.Add($match.Value) }
        [void]$payloads.Add([ordered]@{
                kind = 'msi'; package = $SdkPackage; fileName = $msi
                url = $payload.url; sha256 = $payload.sha256.ToLowerInvariant(); size = [long]$payload.size
            })
    }
    foreach ($cab in $cabs) {
        $payload = $byName[$cab]
        if ($null -eq $payload) { throw "$SdkPackage has no $cab" }
        [void]$payloads.Add([ordered]@{
                kind = 'cab'; package = $SdkPackage; fileName = $cab
                url = $payload.url; sha256 = $payload.sha256.ToLowerInvariant(); size = [long]$payload.size
            })
    }

    $total = [long]0
    foreach ($payload in $payloads) { $total += $payload.size }
    $result = [ordered]@{
        schema = 'nebula.toolchain.v1'
        description = "clang-cl and lld-link from llvm-mingw; MSVC $MsvcVersion C++ runtime headers/libraries and Windows SDK $SdkPackage from Microsoft's Visual Studio 2022 release manifest"
        visualStudio = [ordered]@{
            channel = $ChannelUrl
            productVersion = $channel.info.productDisplayVersion
            manifestUrl = $manifestPayload.url
            manifestFileSha256 = (Get-Sha256 $manifestPath)
            licenseUrl = 'https://go.microsoft.com/fwlink/?LinkId=2179911'
        }
        msvcVersion = $MsvcVersion
        sdkVersion = $SdkPackage
        downloadBytes = $total
        payloads = $payloads
        # Only the compiler, linker and their headers are unpacked.
        llvm = [ordered]@{
            version = '20260922'
            url = 'https://github.com/mstorsjo/llvm-mingw/releases/download/20260922/llvm-mingw-20260922-ucrt-x86_64.zip'
            sha256 = 'e3ad77d117a4bea19a7a3b333341824d79a5a371004a10e25b8504e7b3047666'
            size = 190725905
            hashSource = 'SHA-256 digest GitHub publishes for the release asset'
            extract = [ordered]@{
                'bin/clang-23.exe' = 'bin/clang-cl.exe'
                'bin/ld.lld.exe' = 'bin/lld-link.exe'
                'bin/libLLVM-23.dll' = 'bin/libLLVM-23.dll'
                'bin/libclang-cpp.dll' = 'bin/libclang-cpp.dll'
                'bin/libc++.dll' = 'bin/libc++.dll'
                'bin/libunwind.dll' = 'bin/libunwind.dll'
                'bin/libwinpthread-1.dll' = 'bin/libwinpthread-1.dll'
                'lib/clang/23/include/' = 'lib/clang/23/include/'
            }
        }
        cmake = [ordered]@{
            version = '3.31.6'
            url = 'https://github.com/Kitware/CMake/releases/download/v3.31.6/cmake-3.31.6-windows-x86_64.zip'
            sha256 = 'd163cd3ab4959b0a53fa8988f2ddbd2e6c501658201e6a154386bad9dbe4f836'
            size = 46473549
            hashSource = 'cmake-3.31.6-SHA-256.txt published with the Kitware release'
        }
        ninja = [ordered]@{
            version = '1.12.1'
            url = 'https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip'
            sha256 = 'f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a'
            size = 275425
            hashSource = 'recorded from the official release download; Ninja publishes no checksum file'
        }
    }
    $json = $result | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($Output), $json + "`n", (New-Object Text.UTF8Encoding $false))
    Write-Host ("Pinned {0} payloads, {1:N0} MB, to {2}" -f $payloads.Count, ($total / 1MB), $Output)
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
