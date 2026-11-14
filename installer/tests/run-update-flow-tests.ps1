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
$storeVersion=[uint32](Get-Content -LiteralPath (Join-Path $repo 'runtime\include\galaxy\store_version.inc') -Raw).Trim()
@"
namespace Nebula { internal static class BuildInfo {
 public const string Version="2.0.8-beta.1";
 public const string Commit="update-flow-tests";
 public const string Repository="keepvaibin/nebula";
 public const string ReleasePublicKeyXml="$public";
 public const uint StoreVersion=$storeVersion;
}}
"@ | Set-Content -LiteralPath $stub
$sources=@(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Common') -Filter *.cs | ForEach-Object FullName)+@(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Setup') -Filter *.cs | ForEach-Object FullName)+@($stub,(Join-Path $PSScriptRoot 'UpdateFlowTests.cs'))
$exe=Join-Path $scratch 'UpdateFlowTests.exe'
# Setup stand-ins that carry a product version, as a release's Setup does (AssemblyInfo.cs).
$stubs=Join-Path $scratch 'stubs'
New-Item -ItemType Directory $stubs | Out-Null
foreach($version in @('9.9.9-beta.1','2.0.1-beta.1')) {
    $source=Join-Path $stubs "setup-$version.cs"
    "[assembly: System.Reflection.AssemblyInformationalVersion(`"$version`")] static class Stub { static void Main() { } }" | Set-Content -LiteralPath $source
    & (Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe') /nologo /target:exe "/out:$stubs\setup-$version.exe" $source
    if($LASTEXITCODE -ne 0) {throw 'Setup stand-in compilation failed.'}
}
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /optimize+ /main:Nebula.Setup.UpdateFlowTests "/out:$exe" /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Core.dll @sources
if($LASTEXITCODE -ne 0) {throw 'C# update flow test compilation failed.'}
& $exe (Join-Path $scratch 'run') $private $stubs
$code=$LASTEXITCODE
Remove-Item -LiteralPath $scratch -Recurse -Force
if($code -ne 0) {throw 'Update flow checks failed.'}
