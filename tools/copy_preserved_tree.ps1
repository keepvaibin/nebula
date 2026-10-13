# Copies a tree and keeps NTFS alternate data streams, including the directory streams
# that Get-Item -Stream cannot enumerate.
#
# Nebula's native NAND stores ISFS ownership/permission metadata in a stream named
# `galaxy.isfs.meta`, on directories as well as files. A copy that drops the directory
# streams gives the runtime a NAND whose contents are complete but whose metadata is
# missing. probe_native_nand_root rejects that with ISFS result -102.
#
# Two PowerShell behaviours make this non-obvious:
# 1. `Get-Item -Stream *` (and the .Stream property) returns no streams for a directory,
#    even when `cmd /c dir /r` shows one. A loop built on it copies only file streams.
# 2. [IO.File]::ReadAllBytes / WriteAllBytes throw NotSupportedException for any stream
#    path on this .NET runtime, so they cannot read or write streams.
#
# This round trip is byte-exact on files and directories:
#     Get-Content -LiteralPath <path> -Stream <name> -Encoding Byte
#     Set-Content -LiteralPath <path> -Stream <name> -Value <byte[]> -Encoding Byte
#
# The copy does not enumerate. The runtime uses a closed set of three stream names
# (native_host.cpp:20379-20383), so each name is probed and the ones present are copied.
#
# -Encoding Byte is Windows PowerShell 5.1 syntax; -AsByteStream requires PowerShell 7.
# This script targets 5.1, the harness runtime.

function Get-PreservedStreamNames {
    # Kept in sync with native_host.cpp:20379-20383.
    @(
        'galaxy.isfs.meta'
        'galaxy.isfs.meta.pending'
        'galaxy.isfs.provisional'
    )
}

function Read-PreservedStream {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Name)
    try {
        # The wrapper distinguishes an existing zero-byte stream from absence;
        # PowerShell otherwise emits no objects for an empty byte stream.
        $bytes = [byte[]]@(Get-Content -LiteralPath $Path -Stream $Name -Encoding Byte -ErrorAction Stop)
        return [pscustomobject]@{ Bytes = $bytes }
    } catch {
        # Only this observed Windows PowerShell 5.1 ADS-not-found error is
        # optional. Access/sharing/I/O errors must abort preservation. Verify
        # the base node still exists rather than treating a lost source as an
        # absent optional stream.
        if ($_.FullyQualifiedErrorId -eq 'GetContentReaderFileNotFoundError,Microsoft.PowerShell.Commands.GetContentCommand' -and
            $_.Exception -is [IO.FileNotFoundException] -and $_.Exception.HResult -eq -2147024894) {
            $null = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
            return $null
        }
        throw
    }
}

function Copy-PreservedTree {
    param([Parameter(Mandatory)][string]$Source, [Parameter(Mandatory)][string]$Destination)
    $sourceRoot = (Get-Item -LiteralPath $Source -ErrorAction Stop).FullName.TrimEnd('\')
    if (Test-Path -LiteralPath $Destination) { throw "Destination already exists: $Destination" }
    $entries = @((Get-Item -LiteralPath $sourceRoot)) + @(Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force)
    if ($entries | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        throw 'A preserved-tree copy requires a physical tree, not junctions or symlinks.'
    }
    Copy-Item -LiteralPath $sourceRoot -Destination $Destination -Recurse -Force -ErrorAction Stop
    $targetRoot = (Get-Item -LiteralPath $Destination).FullName.TrimEnd('\')
    $streamNames = Get-PreservedStreamNames
    $copiedStreams = 0
    $directoryStreams = 0
    foreach ($entry in $entries) {
        $relative = $entry.FullName.Substring($sourceRoot.Length).TrimStart('\')
        $target = if ($relative) { Join-Path $targetRoot $relative } else { $targetRoot }
        foreach ($name in $streamNames) {
            # -Encoding Byte is mandatory here: the metadata is binary and a text
            # read would silently corrupt it into a string.
            $stream = Read-PreservedStream -Path $entry.FullName -Name $name
            if ($null -eq $stream) { continue }
            $sourceBytes = $stream.Bytes
            Set-Content -LiteralPath $target -Stream $name -Value $sourceBytes -Encoding Byte -ErrorAction Stop
            $copied = [byte[]](Get-Content -LiteralPath $target -Stream $name -Encoding Byte -ErrorAction Stop)
            if ($copied.Length -ne $sourceBytes.Length) {
                throw "Stream length mismatch after copy: $relative / $name ($($sourceBytes.Length) -> $($copied.Length))"
            }
            for ($index = 0; $index -lt $sourceBytes.Length; $index++) {
                if ($sourceBytes[$index] -ne $copied[$index]) {
                    throw "Stream byte mismatch after copy: $relative / $name at offset $index"
                }
            }
            $copiedStreams++
            if ($entry.PSIsContainer) { $directoryStreams++ }
        }
        if (-not $entry.PSIsContainer -and
            (Get-FileHash -LiteralPath $entry.FullName).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
            throw "File mismatch: $relative"
        }
    }
    # Fail on a shortfall: the number of copied streams must match the source. A copy that
    # drops streams must not report success.
    $expectedStreams = 0
    foreach ($entry in $entries) {
        foreach ($name in $streamNames) {
            if ($null -ne (Read-PreservedStream -Path $entry.FullName -Name $name)) { $expectedStreams++ }
        }
    }
    if ($copiedStreams -ne $expectedStreams) {
        throw "Preserved-tree copy copied $copiedStreams of $expectedStreams metadata streams."
    }
    Write-Verbose "Copy-PreservedTree: preserved $copiedStreams metadata streams ($directoryStreams on directories)."
}
