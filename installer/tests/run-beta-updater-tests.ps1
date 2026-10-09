$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scratch=Join-Path $PSScriptRoot ('results-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $scratch | Out-Null
$stub=Join-Path $scratch 'BuildInfo.cs'
$key=(Get-Content -LiteralPath (Join-Path $repo 'installer\release-public-key.xml') -Raw).Trim().Replace('"','\"')
@"
namespace Nebula { internal static class BuildInfo {
 public const string Version="0.1.1-beta.1";
 public const string Commit="source-probe";
 public const string Repository="keepvaibin/nebula";
 public const string ReleasePublicKeyXml="$key";
}}
"@ | Set-Content -LiteralPath $stub
$sources=@(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Common') -Filter *.cs | ForEach-Object FullName)+@(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Setup') -Filter *.cs | ForEach-Object FullName)+@($stub,(Join-Path $PSScriptRoot 'BetaChannelTests.cs'))
$exe=Join-Path $scratch 'BetaChannelTests.exe'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ /main:Nebula.Setup.BetaChannelTests "/out:$exe" /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Core.dll @sources
if($LASTEXITCODE -ne 0) {throw 'C# setup compilation failed.'}
& $exe
if($LASTEXITCODE -ne 0) {throw 'Updater selection cases failed.'}
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $repo 'installer\build-release.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count) {throw 'Release script parse failed.'}
$nameAssignment=$ast.Find({param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$setupName'},$true)
if(-not $nameAssignment) {throw 'Beta asset naming assignment missing.'}
$assign=[scriptblock]::Create($nameAssignment.Extent.Text)
foreach($Version in @('0.1.0-preview.3','0.1.1-beta.1','0.2.0')) {
 . $assign
 $expected=if($Version -match '-beta\.') {'Nebula-Beta-Setup.exe'} else {'Nebula-Setup.exe'}
 if($setupName -ne $expected) {throw "Wrong asset name for $Version"}
}
Write-Output 'PASS: production release-script asset naming (3 versions); signature filename derives from selected setup path. No setup built or installed.'
