[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$SymbolsPath,
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) {
    $OutputPath = Join-Path $Root "metadata\rmge01\functions.csv"
}

$Pattern = '^[^=]+ = [^:]+:0x([0-9A-Fa-f]+); // type:function size:0x([0-9A-Fa-f]+)'
$ByAddress = @{}
foreach ($Line in Get-Content -LiteralPath $SymbolsPath) {
    if ($Line -match $Pattern) {
        $Address = [Convert]::ToUInt32($Matches[1], 16)
        $Size = [Convert]::ToUInt32($Matches[2], 16)
        if ($Size -eq 0 -or ($Address % 4) -ne 0 -or ($Size % 4) -ne 0) {
            throw "Invalid function range at 0x$($Address.ToString('X8'))"
        }
        if (-not $ByAddress.ContainsKey($Address) -or $ByAddress[$Address] -lt $Size) {
            $ByAddress[$Address] = $Size
        }
    }
}

$Rows = @($ByAddress.GetEnumerator() | Sort-Object { [uint64]$_.Key })
for ($Index = 1; $Index -lt $Rows.Count; $Index++) {
    $PreviousEnd = [uint64]$Rows[$Index - 1].Key + [uint64]$Rows[$Index - 1].Value
    if ($PreviousEnd -gt [uint64]$Rows[$Index].Key) {
        throw "Overlapping function ranges at 0x$($Rows[$Index].Key.ToString('X8'))"
    }
}

$Lines = [System.Collections.Generic.List[string]]::new($Rows.Count + 1)
$Lines.Add("address,size")
foreach ($Row in $Rows) {
    $Lines.Add(
        [string]::Format(
            "{0:X8},{1:X8}",
            [uint32]$Row.Key,
            [uint32]$Row.Value
        )
    )
}

$Directory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $Directory | Out-Null
[IO.File]::WriteAllLines($OutputPath, $Lines, [Text.UTF8Encoding]::new($false))
Write-Host "Wrote $($Rows.Count) unique function ranges to $OutputPath"
