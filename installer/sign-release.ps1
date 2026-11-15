<#
.SYNOPSIS
Signs a Nebula release on the PC that holds the keys: the Android app with its signing key, and every package's
update signature with the release key.

.DESCRIPTION
The release workflow (.github/workflows/release.yml) builds every package into a draft release without a signature.
The keys stay on this PC, so this script finishes the draft:

  installer/sign-release.ps1 -Tag v2.1.0 -AndroidSigning <signing.properties>

1. The Android app (Nebula-Android-unsigned.apk) is signed with the key signing.properties names (the file
   platform/android/app/build.gradle.kts reads: storeFile, storePassword, keyAlias, keyPassword), checked with
   apksigner, and uploaded as Nebula-Android.apk. The unsigned file is removed from the release.
2. The release-key signatures the updates check (RSA PKCS#1 v1.5, SHA-256, base64) are added: Nebula-Setup.exe.sig
   and Nebula-Android.apk.sig over the files, and <package>.manifest.sig over the Linux and macOS packages' manifests
   (tools/release_manifest.py).
3. SHA256SUMS lists the SHA-256 of every package.

What the release already has signed is left as it is, and the script names what the release does not have yet: run it
again once the workflow adds it. The release stays a draft: publish it on GitHub, or with
gh release edit <tag> --repo keepvaibin/nebula --draft=false. A beta's files are named Nebula-Beta-...

Requires PowerShell 5.1 or later, the GitHub CLI (gh, signed in to GitHub) and the Android SDK's build-tools (apksigner,
from ANDROID_HOME or the default SDK folder).
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Tag,
    [string]$Repository = 'keepvaibin/nebula',
    # The app signing key: a properties file as platform/android/app/build.gradle.kts's nebula.signing reads it.
    [string]$AndroidSigning = '',
    # The release key's private half (RSA XML).
    [string]$SigningKey = (Join-Path $env:USERPROFILE '.nebula-release-key\nebula-release-private.xml')
)
$ErrorActionPreference = 'Stop'
$installer = Split-Path -Parent $PSCommandPath

function Invoke-Checked([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

# A Java properties file (key=value or key: value), with its escapes (\\, \:, \=) read as Gradle reads them.
function Read-Properties([string]$Path) {
    $values = @{}
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith('#') -or $text.StartsWith('!')) { continue }
        $match = [regex]::Match($text, '^((?:[^\\=:]|\\.)+?)\s*[=:]\s*(.*)$')
        if (-not $match.Success) { continue }
        $unescape = { param($s) [regex]::Replace($s, '\\(.)', { param($m) $m.Groups[1].Value }) }
        $values[(& $unescape $match.Groups[1].Value)] = & $unescape $match.Groups[2].Value
    }
    return $values
}

function Find-ApkSigner {
    $sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    $tools = Get-ChildItem -LiteralPath (Join-Path $sdk 'build-tools') -Directory -ErrorAction SilentlyContinue |
        Sort-Object { [version]($_.Name -replace '[^0-9.].*$', '') } -Descending
    foreach ($folder in $tools) {
        $signer = Join-Path $folder.FullName 'apksigner.bat'
        if (Test-Path -LiteralPath $signer) { return $signer }
    }
    throw "No apksigner in $sdk\build-tools: install the Android SDK build-tools"
}

function Sign-Apk([string]$Unsigned, [string]$Signed, [hashtable]$Key) {
    foreach ($name in 'storeFile', 'storePassword', 'keyAlias', 'keyPassword') {
        if (-not $Key.ContainsKey($name)) { throw "$AndroidSigning has no $name" }
    }
    $signer = Find-ApkSigner
    # The passwords reach apksigner through its environment, never its command line.
    $env:NEBULA_STORE_PASSWORD = $Key['storePassword']
    $env:NEBULA_KEY_PASSWORD = $Key['keyPassword']
    try {
        # Every scheme, as the Gradle build signs (build.gradle.kts): an installer that reads only the oldest still finds
        # the certificates. The workflow's build is aligned already (the Android Gradle plugin aligns every release).
        Invoke-Checked $signer @('sign', '--ks', $Key['storeFile'], '--ks-key-alias', $Key['keyAlias'],
            '--ks-pass', 'env:NEBULA_STORE_PASSWORD', '--key-pass', 'env:NEBULA_KEY_PASSWORD',
            '--v1-signing-enabled', 'true', '--v2-signing-enabled', 'true', '--v3-signing-enabled', 'true',
            '--out', $Signed, $Unsigned)
    } finally {
        Remove-Item Env:NEBULA_STORE_PASSWORD, Env:NEBULA_KEY_PASSWORD -ErrorAction SilentlyContinue
    }
    Invoke-Checked $signer @('verify', $Signed)
    Write-Host "Signed $(Split-Path -Leaf $Signed) with the app signing key."
}

function Write-Signature([string]$Path, [Security.Cryptography.RSACryptoServiceProvider]$Rsa) {
    $signature = $Rsa.SignData([IO.File]::ReadAllBytes($Path), [Security.Cryptography.CryptoConfig]::MapNameToOID('SHA256'))
    [IO.File]::WriteAllText("$Path.sig", [Convert]::ToBase64String($signature))
    Write-Host "Signed $(Split-Path -Leaf $Path) with the release key."
}

if (-not (Test-Path -LiteralPath $SigningKey)) { throw "No release key at $SigningKey" }
$publicKey = (Get-Content -LiteralPath (Join-Path $installer 'release-public-key.xml') -Raw).Trim()
$rsa = New-Object Security.Cryptography.RSACryptoServiceProvider
$rsa.PersistKeyInCsp = $false
$folder = Join-Path ([IO.Path]::GetTempPath()) ('nebula-sign-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $folder | Out-Null
try {
    $rsa.FromXmlString((Get-Content -LiteralPath $SigningKey -Raw))
    if ($rsa.ToXmlString($false) -ne $publicKey) { throw 'The signing key does not match installer/release-public-key.xml.' }

    $version = $Tag -replace '^v', ''
    $prefix = if ($version.ToLowerInvariant().Contains('-beta.')) { 'Nebula-Beta' } else { 'Nebula' }
    $setup = "$prefix-Setup.exe"
    $apk = "$prefix-Android.apk"
    $unsignedApk = "$prefix-Android-unsigned.apk"
    $manifests = @("$prefix-Linux-x64.tar.gz.manifest", "$prefix-macOS.zip.manifest")
    $packages = @($setup, $apk, "$prefix-Linux-x64.tar.gz", "$prefix-macOS.zip")
    $assets = @(Invoke-Checked gh @('release', 'view', $Tag, '--repo', $Repository, '--json', 'assets', '--jq', '.assets[].name'))
    $upload = @()

    # 1. The Android app.
    if ($assets -contains $apk) {
        Write-Host "$apk is signed already."
    } elseif ($assets -contains $unsignedApk) {
        if (-not $AndroidSigning) { throw "$unsignedApk needs the app signing key: pass -AndroidSigning <signing.properties>" }
        Invoke-Checked gh @('release', 'download', $Tag, '--repo', $Repository, '--pattern', $unsignedApk, '--dir', $folder)
        Sign-Apk (Join-Path $folder $unsignedApk) (Join-Path $folder $apk) (Read-Properties $AndroidSigning)
        Invoke-Checked gh @('release', 'upload', $Tag, '--repo', $Repository, '--clobber', (Join-Path $folder $apk))
        Invoke-Checked gh @('release', 'delete-asset', $Tag, $unsignedApk, '--repo', $Repository, '--yes')
        $assets = @($assets | Where-Object { $_ -ne $unsignedApk }) + $apk
    }

    # 2. The release-key signatures.
    foreach ($name in @($setup, $apk) + $manifests) {
        if ($assets -notcontains $name) { Write-Host "Not on $Tag yet (run this again once the release workflow adds it): $name"; continue }
        if ($assets -contains "$name.sig") { Write-Host "$name.sig is there already."; continue }
        $local = Join-Path $folder $name
        if (-not (Test-Path -LiteralPath $local)) {
            Invoke-Checked gh @('release', 'download', $Tag, '--repo', $Repository, '--pattern', $name, '--dir', $folder) | Out-Null
        }
        if ($name.EndsWith('.manifest')) {
            # A manifest names this release's version: one for another release would be refused by every update.
            $named = ((Get-Content -LiteralPath $local) | Where-Object { $_ -like 'version=*' }) -replace '^version=', ''
            if ($named -ne $version) { throw "$name names version $named, not $version" }
        }
        Write-Signature $local $rsa
        $upload += "$local.sig"
    }
    if ($upload.Count) {
        Invoke-Checked gh (@('release', 'upload', $Tag, '--repo', $Repository, '--clobber') + $upload)
        Write-Host "Uploaded $($upload.Count) signatures to $Tag."
    }

    # 3. The packages' SHA-256.
    $present = @($packages | Where-Object { $assets -contains $_ })
    $sums = foreach ($name in $present) {
        $local = Join-Path $folder $name
        if (-not (Test-Path -LiteralPath $local)) {
            Invoke-Checked gh @('release', 'download', $Tag, '--repo', $Repository, '--pattern', $name, '--dir', $folder) | Out-Null
        }
        '{0} *{1}' -f (Get-FileHash -LiteralPath $local -Algorithm SHA256).Hash.ToLowerInvariant(), $name
    }
    if ($present.Count) {
        $sumsFile = Join-Path $folder 'SHA256SUMS'
        [IO.File]::WriteAllText($sumsFile, (($sums -join "`n") + "`n"), (New-Object Text.UTF8Encoding $false))
        Invoke-Checked gh @('release', 'upload', $Tag, '--repo', $Repository, '--clobber', $sumsFile)
        Write-Host "SHA256SUMS lists $($present.Count) packages."
    }
    Write-Host "The release is still a draft: gh release edit $Tag --repo $Repository --draft=false publishes it."
}
finally {
    $rsa.Dispose()
    Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue
}
