param([string]$Setup = '')
# Launcher and Setup checks without a visible window or the user's %LOCALAPPDATA%\Nebula: first-start decision,
# settings form rendered offscreen, launch arguments (stub game), folder moves, nebula.ico in every executable
# (-Setup adds a built Setup). Writes PNGs to the scratch folder.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scratch = Join-Path $env:TEMP ('nebula-launcher-tests-' + [guid]::NewGuid().ToString('N'))
$app = Join-Path $scratch 'root\versions\test'
$data = Join-Path $scratch 'data'
New-Item -ItemType Directory -Force $app, $data, (Join-Path $scratch 'root\content') | Out-Null
Set-Content -LiteralPath (Join-Path $scratch 'root\content\game.pak') -Value 'stub'
$stub = Join-Path $scratch 'BuildInfo.cs'
@"
namespace Nebula { internal static class BuildInfo {
 public const string Version="0.0.0-test";
 public const string Commit="test";
 public const string Repository="keepvaibin/nebula";
 public const string ReleasePublicKeyXml="";
}}
"@ | Set-Content -LiteralPath $stub
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$refs = @('/r:System.Web.Extensions.dll', '/r:System.IO.Compression.dll', '/r:System.IO.Compression.FileSystem.dll',
          '/r:System.Windows.Forms.dll', '/r:System.Drawing.dll', '/r:System.Core.dll')
$common = @(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Common') -Filter *.cs | ForEach-Object FullName) + $stub
$launcher = @(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Launcher') -Filter *.cs | ForEach-Object FullName)
$icon = Join-Path $repo 'installer\assets\nebula.ico'
$manifest = Join-Path $repo 'installer\app.manifest'

# The two shipped launchers, built as build-native-setup.ps1 builds them.
& $csc /nologo /target:winexe /platform:x64 /optimize+ "/win32manifest:$manifest" "/win32icon:$icon" "/out:$app\Nebula.exe" @refs @common @launcher
if ($LASTEXITCODE -ne 0) { throw 'Nebula.exe compilation failed.' }
& $csc /nologo /target:winexe /platform:x64 /optimize+ /define:NEBULA_SETTINGS_APP "/win32manifest:$manifest" "/win32icon:$icon" "/out:$app\NebulaSettings.exe" @refs @common @launcher
if ($LASTEXITCODE -ne 0) { throw 'NebulaSettings.exe compilation failed.' }
& $csc /nologo /target:exe /platform:x64 "/win32manifest:$manifest" "/win32icon:$icon" /main:Nebula.Launcher.LauncherTests "/out:$app\LauncherTests.exe" @refs @common @launcher (Join-Path $PSScriptRoot 'LauncherTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'LauncherTests.exe compilation failed.' }
# The game: lists no mods, and records the arguments it is started with.
$gameStub = Join-Path $scratch 'GameStub.cs'
@'
using System; using System.IO;
static class GameStub { static void Main(string[] a) {
 if (a.Length > 0 && a[0] == "--list-mods") { Console.WriteLine("{\"ok\":true,\"folder\":\"\",\"mods\":[]}"); return; }
 File.WriteAllLines(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "stub-launch.txt"), a); } }
'@ | Set-Content -LiteralPath $gameStub
& $csc /nologo /target:exe /platform:x64 "/out:$app\NebulaNative.exe" $gameStub
if ($LASTEXITCODE -ne 0) { throw 'Game stub compilation failed.' }

$env:NEBULA_DATA_ROOT = $data
& "$app\LauncherTests.exe" $scratch
$code = $LASTEXITCODE
Remove-Item Env:NEBULA_DATA_ROOT
if ($code -ne 0) { throw 'Launcher cases failed.' }

$setupSources = @(Get-ChildItem -LiteralPath (Join-Path $repo 'installer\src\Setup') -Filter *.cs | ForEach-Object FullName)
& $csc /nologo /target:exe /platform:x64 /main:Nebula.Setup.SetupTests "/out:$scratch\SetupTests.exe" @refs @common @setupSources (Join-Path $PSScriptRoot 'SetupTests.cs')
if ($LASTEXITCODE -ne 0) { throw 'SetupTests.exe compilation failed.' }
& "$scratch\SetupTests.exe" $scratch
if ($LASTEXITCODE -ne 0) { throw 'Setup cases failed.' }

# nebula.ico: frame sizes, and the same 32 px image in every executable.
Add-Type -AssemblyName System.Drawing
$bytes = [IO.File]::ReadAllBytes($icon)
$count = [BitConverter]::ToUInt16($bytes, 4)
$sizes = for ($i = 0; $i -lt $count; $i++) { $w = $bytes[6 + 16 * $i]; if ($w -eq 0) { 256 } else { $w } }
Write-Output ("PASS: nebula.ico frames " + (($sizes | Sort-Object) -join ','))
function Pixels([System.Drawing.Icon]$i) {
    $b = $i.ToBitmap(); $ms = New-Object IO.MemoryStream; $b.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    [Convert]::ToBase64String($ms.ToArray())
}
# The frames are PNG-compressed, which System.Drawing.Icon cannot decode: load through USER32.
Add-Type -Namespace NebulaTest -Name User32 -MemberDefinition '[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr LoadImageW(IntPtr h, string name, uint type, int cx, int cy, uint flags);'
$expected = Pixels ([System.Drawing.Icon]::FromHandle([NebulaTest.User32]::LoadImageW([IntPtr]::Zero, $icon, 1, 32, 32, 0x10)))
$exes = @("$app\Nebula.exe", "$app\NebulaSettings.exe")
if ($Setup -ne '') { $exes += $Setup }
foreach ($exe in $exes) {
    $got = Pixels ([System.Drawing.Icon]::ExtractAssociatedIcon($exe))
    if ($got -ne $expected) { throw "Icon mismatch: $exe" }
    Write-Output "PASS: nebula.ico in $(Split-Path -Leaf $exe)"
}
Write-Output "Form renders: $scratch"
