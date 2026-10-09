# Copy a tree while preserving NTFS alternate data streams, including the
# directory streams that PowerShell's stream enumeration cannot see.
#
# WHY THIS EXISTS
# ---------------
# Nebula's native NAND stores ISFS ownership/permission metadata in a stream
# named `galaxy.isfs.meta`, on DIRECTORIES as well as files. A copy that drops
# the directory streams hands the runtime a NAND whose contents are complete but
# whose ownership metadata is absent, and `probe_native_nand_root` rejects that
# as unrecoverable:
#
#     [boot] Unhandled exception: native NAND root preflight failed with ISFS result -102
#     exit.json -> { "exit": 9 }
#
# Measured on `D:\NebulaWork\perf-20261007`: the seed has metadata on 11
# directories and 5 files; the copy this function previously produced preserved
# all 5 file streams and lost all 11 directory streams, so every perf session
# died at boot and nothing could be measured.
#
# THE TWO BUGS BEING AVOIDED
# -------------------------
# 1. `Get-Item -Stream *` (and the `.Stream` property) enumerates NOTHING for a
#    directory — it returns 0 streams even when the directory demonstrably has
#    one via `cmd /c dir /r`. So the old loop copied only file streams.
# 2. `[IO.File]::ReadAllBytes` / `WriteAllBytes` throw
#    `NotSupportedException: "The given path's format is not supported."` for ANY
#    stream path on this .NET runtime, file or directory. So even the file-stream
#    copy relied on calls that cannot read a stream.
#
# What DOES work, verified byte-exact round-trip on both files and directories:
#     Get-Content -LiteralPath <path> -Stream <name> -Encoding Byte
#     Set-Content -LiteralPath <path> -Stream <name> -Value <byte[]> -Encoding Byte
#
# So this copy does not enumerate at all. The runtime uses a known, closed set of
# three stream names (see `native_host.cpp:20379-20383`), so each is probed by
# name and the ones that exist are copied.
#
# NOTE: `-Encoding Byte` is Windows PowerShell 5.1 syntax; `-AsByteStream` is
# PowerShell 7+ only. This targets 5.1 deliberately, because that is what the
# harness runs under (`$PSVersionTable.PSVersion` = 5.1.26100).

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
    # Fail loudly on a shortfall. This is the check whose absence let a copy that
    # preserved 5 of 16 metadata streams look successful, and it cost this machine
    # every measurement it might have taken.
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
