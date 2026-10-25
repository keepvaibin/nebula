# The update path end to end on a local release server (UpdateFlowTests.cs): check, notes, download, signature,
# the failures (offline, slow, corrupt, wrongly signed, missing, cut off, cancelled, published again) and rollback,
# with a signing key made for the run. Its own folders only: no install, save or setting of the player's.
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scratch=Join-Path $env:TEMP ('nebula-update-flow-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $scratch | Out-Null
$rsa=New-Object System.Security.Cryptography.RSACryptoServiceProvider 2048
$rsa.PersistKeyInCsp=$false
$private=Join-Path $scratch 'test-key.xml'
[IO.File]::WriteAllText($private,$rsa.ToXmlString($true))
$public=$rsa.ToXmlString($false).Replace('"','\"')
$stub=Join-Path $scratch 'BuildInfo.cs'
@"
namespace Nebula { internal static class BuildInfo {
 public const string Version="2.0.8-beta.1";
 public const string Commit="update-flow-tests";
 public const string Repository="keepvaibin/nebula";
 public const string ReleasePublicKeyXml="$public";
}}
"@ | Set-Content -LiteralPath $stub
$sources=@(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Common') -Filter *.cs | ForEach-Object FullName)+@(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Setup') -Filter *.cs | ForEach-Object FullName)+@($stub,(Join-Path $PSScriptRoot 'UpdateFlowTests.cs'))
$exe=Join-Path $scratch 'UpdateFlowTests.exe'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ /main:Nebula.Setup.UpdateFlowTests "/out:$exe" /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Core.dll @sources
if($LASTEXITCODE -ne 0) {throw 'C# update flow test compilation failed.'}
& $exe (Join-Path $scratch 'run') $private
$code=$LASTEXITCODE
Remove-Item -LiteralPath $scratch -Recurse -Force
if($code -ne 0) {throw 'Update flow checks failed.'}
