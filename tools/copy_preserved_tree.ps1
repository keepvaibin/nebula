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
    foreach ($entry in $entries) {
        $relative = $entry.FullName.Substring($sourceRoot.Length).TrimStart('\')
        $target = if ($relative) { Join-Path $targetRoot $relative } else { $targetRoot }
        # Copy-Item preserves ordinary files but omits directory ADS. Native
        # ISFS ownership/permission metadata is stored in those streams too.
        foreach ($stream in (Get-Item -LiteralPath $entry.FullName -Stream * -ErrorAction Stop)) {
            if ($stream.Stream -eq ':$DATA' -or $stream.Stream -eq '$DATA') { continue }
            $bytes = [IO.File]::ReadAllBytes($entry.FullName + ':' + $stream.Stream)
            [IO.File]::WriteAllBytes($target + ':' + $stream.Stream, $bytes)
            $copied = [IO.File]::ReadAllBytes($target + ':' + $stream.Stream)
            if ($bytes.Length -ne $copied.Length) { throw "Stream mismatch: $relative / $($stream.Stream)" }
            for ($index = 0; $index -lt $bytes.Length; $index++) {
                if ($bytes[$index] -ne $copied[$index]) { throw "Stream mismatch: $relative / $($stream.Stream)" }
            }
        }
        if (-not $entry.PSIsContainer -and
            (Get-FileHash -LiteralPath $entry.FullName).Hash -ne (Get-FileHash -LiteralPath $target).Hash) {
            throw "File mismatch: $relative"
        }
    }
}
