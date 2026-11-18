# Setup's uninstall keeps its window answering while it deletes (P-114). A portable copy of Nebula with many files
# (-Files, default 40000) is removed through the window: Setup --uninstall --root <copy>, its Remove button clicked,
# the question answered Yes with the box left unticked. While the files go, the window is asked WM_NULL every 50 ms
# and must answer each within a second (a window whose thread is deleting answers none until the end). Then: the program
# files are gone, data\ and portable.txt are kept, and Setup says it is done and closes.
#   run-setup-uninstall-window.ps1 -Work <folder> [-Setup <Nebula-Setup.exe>] [-Native <installer\native>]
# Without -Setup a test Setup is built from -Native (default this tree's) as build-native-setup.ps1 compiles it, with
# a payload of one file: an uninstall reads none of it. Its own folders and data root; English labels ("Remove").
# Exit 0 when every check passes.
param([Parameter(Mandatory = $true)][string]$Work, [string]$Setup = '', [string]$Native = '', [int]$Files = 40000)
$ErrorActionPreference = 'Stop'
$installer = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$repo = Split-Path -Parent $installer
if (-not $Native) { $Native = Join-Path $installer 'native' }
$failures = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "PASS: $what" } else { "FAIL: $what"; $script:failures++ } }

if (Test-Path -LiteralPath $Work) { Remove-Item -LiteralPath $Work -Recurse -Force }
New-Item -ItemType Directory -Force $Work | Out-Null
$Work = (Resolve-Path -LiteralPath $Work).Path

if (-not $Setup) {
    # The test Setup: the sources of -Native, the build information and Setup's words as build-native-setup.ps1 writes
    # them, a payload zip of one file.
    $gen = Join-Path $Work 'gen'
    $obj = Join-Path $Work 'obj'
    New-Item -ItemType Directory -Force $gen, $obj | Out-Null
    $utf8 = New-Object Text.UTF8Encoding $false
    $key = (Get-Content -LiteralPath (Join-Path $installer 'release-public-key.xml') -Raw).Trim().Replace('\', '\\').Replace('"', '\"')
    $store = (Get-Content -LiteralPath (Join-Path $repo 'runtime\include\galaxy\store_version.inc') -Raw).Trim()
    [IO.File]::WriteAllText((Join-Path $gen 'build_info.h'), @"
#pragma once
namespace nebula::build {
inline constexpr const wchar_t kVersion[] = L"2.0.0";
inline constexpr const wchar_t kFileVersion[] = L"2.0.0.0";
inline constexpr const wchar_t kCommit[] = L"test";
inline constexpr const wchar_t kRepository[] = L"keepvaibin/nebula";
inline constexpr const wchar_t kReleasePublicKeyXml[] = L"$key";
inline constexpr unsigned kStoreVersion = $store;
}
"@, $utf8)
    $rows = [IO.File]::ReadAllLines((Join-Path $repo 'localization\NebulaText.tsv'), $utf8)
    $table = Join-Path $gen 'NebulaText.tsv'
    [IO.File]::WriteAllText($table, ((@($rows[0]) + @($rows | Where-Object { $_.StartsWith('L.') })) -join "`n"), $utf8)
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $payload = Join-Path $gen 'payload.zip'
    $zip = [IO.Compression.ZipFile]::Open($payload, [IO.Compression.ZipArchiveMode]::Create)
    try {
        $entry = $zip.CreateEntry('app/readme.txt')
        $w = New-Object IO.StreamWriter($entry.Open()); $w.Write('test'); $w.Dispose()
    } finally { $zip.Dispose() }
    $rc = Join-Path $gen 'setup.rc'
    [IO.File]::WriteAllText($rc, ("1 24 `"{0}`"`r`nPAYLOAD RCDATA `"{1}`"`r`nNEBULATEXT RCDATA `"{2}`"`r`n" -f
        (Join-Path $Native 'app.manifest').Replace('\', '\\'), $payload.Replace('\', '\\'), $table.Replace('\', '\\')), $utf8)
    $miniz = Join-Path $repo 'third_party\miniz'
    $Setup = Join-Path $Work 'Nebula-Setup.exe'
    $sources = ('common', 'archive', 'ui', 'text', 'setup', 'setup_engine' | ForEach-Object { "`"$(Join-Path $Native "$_.cpp")`"" }) -join ' '
    $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    $cmd = "call `"$vcvars`" >nul && cd /d `"$obj`" && " +
        "cl /nologo /c /std:c++20 /O2 /MT /DNDEBUG /EHsc /W4 /permissive- /utf-8 /Zc:__cplusplus /DUNICODE /D_UNICODE " +
        "/D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /I`"$gen`" /I`"$Native`" /I`"$miniz`" $sources && " +
        "cl /nologo /c /O2 /MT /DNDEBUG /W3 /utf-8 `"$(Join-Path $miniz 'miniz.c')`" && rc /nologo /fo setup.res `"$rc`" && " +
        "link /nologo /SUBSYSTEM:WINDOWS /OUT:`"$Setup`" setup.res common.obj archive.obj ui.obj text.obj setup.obj setup_engine.obj miniz.obj"
    cmd.exe /d /c "($cmd) > `"$Work\build.log`" 2>&1"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $Setup)) { "FAIL: the test Setup did not build ($Work\build.log)"; exit 1 }
}

# The portable copy: the marker, an installed version with many files, data of its own.
$root = Join-Path $Work 'Nebula'
$version = Join-Path $root 'versions\2.0.0'
New-Item -ItemType Directory -Force $version, (Join-Path $root 'data\saves'), (Join-Path $root 'data\settings') | Out-Null
[IO.File]::WriteAllText((Join-Path $root 'portable.txt'), 'portable')
[IO.File]::WriteAllText((Join-Path $root 'current.json'), '{"version":"2.0.0"}')
[IO.File]::WriteAllText((Join-Path $version 'install.json'), '{"version":"2.0.0","files":{}}')
[IO.File]::WriteAllBytes((Join-Path $root 'data\saves\GameData.bin'), [byte[]](1..200))
[IO.File]::WriteAllText((Join-Path $root 'data\settings\config.ini'), "bloom = 140`n")
$bytes = [byte[]](1..64)
for ($d = 0; $d -lt [Math]::Ceiling($Files / 1000); $d++) {
    $dir = Join-Path $version ("part{0:D3}" -f $d)
    [void][IO.Directory]::CreateDirectory($dir)
    for ($i = 0; $i -lt 1000 -and $d * 1000 + $i -lt $Files; $i++) { [IO.File]::WriteAllBytes((Join-Path $dir "f$i.bin"), $bytes) }
}

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class SetupWindows {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc proc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassNameW(IntPtr hwnd, StringBuilder name, int size);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hwnd, StringBuilder text, int size);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeoutW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l, uint flags, uint ms, out IntPtr result);
    public static string Class(IntPtr h) { var s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
    public static string Text(IntPtr h) { var s = new StringBuilder(512); GetWindowTextW(h, s, 512); return s.ToString(); }
    public static List<IntPtr> TopLevel(uint pid, bool visible) {
        var found = new List<IntPtr>();
        EnumWindows((h, l) => { uint p; GetWindowThreadProcessId(h, out p); if (p == pid && (!visible || IsWindowVisible(h))) found.Add(h); return true; }, IntPtr.Zero);
        return found;
    }
    public static IntPtr Child(IntPtr parent, string cls, string text) {
        IntPtr hit = IntPtr.Zero;
        EnumChildWindows(parent, (h, l) => { if (Class(h) == cls && Text(h).Replace("&", "") == text) { hit = h; return false; } return true; }, IntPtr.Zero);
        return hit;
    }
    // The texts of a dialog's static controls (a message box's message).
    public static string Says(IntPtr parent) {
        var said = new StringBuilder();
        EnumChildWindows(parent, (h, l) => { if (Class(h) == "Static" && Text(h).Length > 0) said.Append(Text(h)).Append(' '); return true; }, IntPtr.Zero);
        return said.ToString().Trim();
    }
    // Milliseconds the window took to answer WM_NULL, or -1 when it did not within `limit`.
    public static long Answers(IntPtr h, uint limit) {
        var watch = System.Diagnostics.Stopwatch.StartNew();
        IntPtr r;
        IntPtr ok = SendMessageTimeoutW(h, 0, IntPtr.Zero, IntPtr.Zero, 0x0002, limit, out r);
        return ok == IntPtr.Zero ? -1 : watch.ElapsedMilliseconds;
    }
}
"@

function Wait-For([scriptblock]$What, [int]$Seconds, [string]$Name) {
    $limit = [DateTime]::Now.AddSeconds($Seconds)
    while ([DateTime]::Now -lt $limit) { $v = & $What; if ($v) { return $v }; Start-Sleep -Milliseconds 50 }
    throw "no $Name in $Seconds s"
}

$env:LOCALAPPDATA = Join-Path $Work 'localappdata'
New-Item -ItemType Directory -Force $env:LOCALAPPDATA | Out-Null
# Its window hidden (the questions still show, briefly).
$p = Start-Process -FilePath $Setup -ArgumentList @('--uninstall', '--root', "`"$root`"") -PassThru -WindowStyle Hidden
$pid_ = [uint32]$p.Id
try {
    $form = Wait-For { [SetupWindows]::TopLevel($pid_, $false) | Where-Object { [SetupWindows]::Text($_) -eq 'Nebula Setup' -and [SetupWindows]::Class($_) -ne '#32770' } | Select-Object -First 1 } 30 'Setup window'
    $remove = Wait-For { $b = [SetupWindows]::Child($form, 'Button', 'Remove'); if ($b -ne [IntPtr]::Zero) { $b } } 10 'Remove button'
    # Posted, not sent: the click opens a modal question, which a sent message would wait behind.
    [void][SetupWindows]::PostMessageW($remove, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # BM_CLICK
    $question = Wait-For { [SetupWindows]::TopLevel($pid_, $true) | Where-Object { [SetupWindows]::Class($_) -eq '#32770' } | Select-Object -First 1 } 10 'question'
    # Yes: TDM_CLICK_BUTTON for a task dialog, WM_COMMAND for a message box (the question before P-113).
    [void][SetupWindows]::PostMessageW($question, 0x0466, [IntPtr]6, [IntPtr]::Zero)
    [void][SetupWindows]::PostMessageW($question, 0x0111, [IntPtr]6, [IntPtr]::Zero)
    $versions = Join-Path $root 'versions'
    $probes = 0; $missed = 0; $slowest = 0
    $end = [DateTime]::Now.AddSeconds(300)
    while ((Test-Path -LiteralPath $versions) -and -not $p.HasExited -and [DateTime]::Now -lt $end) {
        $ms = [SetupWindows]::Answers($form, 1000)
        $probes++
        if ($ms -lt 0) { $missed++ } elseif ($ms -gt $slowest) { $slowest = $ms }
        Start-Sleep -Milliseconds 50
    }
    "probes while deleting: $probes, unanswered within 1000 ms: $missed, slowest answer: $slowest ms"
    Check ($probes -ge 5) "the delete lasted long enough to probe ($probes probes)"
    Check ($missed -eq 0) "the window answered every probe while the files were deleted"
    # The done message (a message box captioned Nebula Setup) answered OK until Setup ends.
    $said = $false
    $end = [DateTime]::Now.AddSeconds(120)
    while (-not $p.HasExited -and [DateTime]::Now -lt $end) {
        foreach ($box in [SetupWindows]::TopLevel($pid_, $true) | Where-Object { [SetupWindows]::Class($_) -eq '#32770' -and [SetupWindows]::Text($_) -eq 'Nebula Setup' }) {
            if (-not $said) { "done message: " + [SetupWindows]::Says($box) }
            $said = $true
            $ok = [SetupWindows]::Child($box, 'Button', 'OK')
            if ($ok -ne [IntPtr]::Zero) { [void][SetupWindows]::PostMessageW($ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero) }  # BM_CLICK
        }
        Start-Sleep -Milliseconds 300
    }
    Check $said "Setup said it was done"
    Check $p.HasExited "Setup closed after its done message"
    if (-not $p.HasExited) {
        foreach ($h in [SetupWindows]::TopLevel($pid_, $false)) {
            "  still open: {0} '{1}' visible={2}" -f [SetupWindows]::Class($h), [SetupWindows]::Text($h), [SetupWindows]::IsWindowVisible($h)
        }
    }
} catch {
    "FAIL: $_"
    $failures++
} finally {
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
}
Check (-not (Test-Path -LiteralPath (Join-Path $root 'versions'))) "the program files are gone"
Check (-not (Test-Path -LiteralPath (Join-Path $root 'current.json'))) "current.json is gone"
Check ((Test-Path -LiteralPath (Join-Path $root 'data\saves\GameData.bin')) -and (Test-Path -LiteralPath (Join-Path $root 'portable.txt'))) "data\ and portable.txt are kept"
if ($failures -ne 0) { "run-setup-uninstall-window: $failures failed"; exit 1 }
"run-setup-uninstall-window: pass"
exit 0
