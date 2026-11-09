# release_check.ps1 -Path <folder> [-Installer <file>...] [-Allow <file>] [-RequireSigned] [-SkipDefender]
# The gate every release folder passes before it is published. It checks, and exits 1 listing each failure:
#   1. The release flag. tools/test_driver/switches.h is read: every test switch name sits in the NEBULA_DEV branch of its
#      function and the release branch reads no environment; the files of tools/test_driver compile to nothing without
#      NEBULA_DEV; each .inc there is included only under NEBULA_DEV; the CMake files define NEBULA_DEV only when
#      NEBULA_RELEASE is off.
#      A game-module file that names a test option (NEBULA_OPTION_TEST_*) holds it under a NEBULA_DEV conditional.
#   2. Every file of -Path and every -Installer file (and the entries of the .zip/.apk/.jar/.nupkg files among them),
#      as bytes (ASCII and UTF-8 text) and as UTF-16 text, holds none of: a test switch's name (the names read from switches.h, plus the
#      prefixes below: GALAXY_TEST_, NEBULA_OPTION_TEST_, NEBULA_TEST_, NEBULA_DEV and others), a Windows or Unix user-profile path (drive:\Users\<name>\, /Users/<name>/, /home/<name>/), an
#      e-mail address other than a GitHub noreply one, a MAC address (six hex pairs joined by colons or
#      hyphens, three hex quads joined by dots, or 12 hex digits after "mac"/"bdaddr"/"bluetooth"), or a personal term from the release key's
#      private-terms.txt (one per line; kept out of the repository, so no name is ever written here). A finding prints
#      its kind, file and offset with the private part masked. -Allow names a local file of literal match texts to let
#      through (one per line, # starts a comment): a third-party library's own address, say.
#      The same personal-detail checks (paths, e-mail, MAC, private terms; not the switch names, which sources hold on
#      purpose) run over every text file git tracks in this repository, except third_party/ and docs/licenses/ (their authors'
#      details). -SkipSource leaves that out.
#   3. Every .exe and .dll carries version information (product, file version, description).
#   4. Signatures. Authenticode of every .exe, .dll and .msi: Valid passes; NotSigned is reported as "unsigned" and is
#      not a failure (it is with -RequireSigned, for the Nebula-owned files); any other state (hash mismatch, untrusted)
#      is a failure. Each -Installer file's release-key signature (<file>.sig, RSA SHA-256 over the file, checked against
#      installer/release-public-key.xml): a .sig that does not verify is a failure; a missing .sig is a failure when
#      signing is configured (the release key's private file exists where installer/build-native-setup.ps1 looks for it,
#      or -RequireSigned is given) and otherwise reported "unsigned" (updates will not run it). Only the existence of the
#      private key file is tested; no key is read or written here.
#   5. Microsoft Defender finds nothing in any file (scanned without remediation); -SkipDefender leaves this out.
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [string[]]$Installer = @(),
    [string]$Allow = "",
    [switch]$RequireSigned,
    [switch]$SkipDefender,
    [switch]$SkipSource
)
$ErrorActionPreference = "Stop"
$failures = New-Object System.Collections.Generic.List[string]
$notes = New-Object System.Collections.Generic.List[string]
$repo = Split-Path -Parent $PSScriptRoot

# ---- 1. The release flag --------------------------------------------------------------------------------------------
$switchesFile = Join-Path $repo "tools\test_driver\switches.h"
$switchNames = New-Object System.Collections.Generic.HashSet[string]
if (-not (Test-Path -LiteralPath $switchesFile)) {
    $failures.Add("tools/test_driver/switches.h is missing: the test switches could not be listed")
} else {
    $mode = "none"
    $ifs = 0
    $elses = 0
    $number = 0
    foreach ($line in [IO.File]::ReadAllLines($switchesFile)) {
        $number++
        if ($line -match '^#if !defined\(NEBULA_DEV\)\s*$') { $mode = "release"; $ifs++; continue }
        if ($line -match '^#else\s*$') { if ($mode -eq "release") { $mode = "dev"; $elses++ } else { $failures.Add("switches.h:$number a #else outside a NEBULA_DEV pair") }; continue }
        if ($line -match '^#endif\s*$') { $mode = "none"; continue }
        if ($line -match '^#\s*(if|ifdef|ifndef|elif)\b' -and $line -notmatch '^#if defined\(_MSC_VER\)\s*$') {
            $failures.Add("switches.h:$number has a conditional this check does not read: $line")
            continue
        }
        if ($line -match '^#if defined\(_MSC_VER\)\s*$') { continue }
        $names = [regex]::Matches($line, 'L?"((?:GALAXY|NEBULA)_[A-Z0-9_]+)"')
        foreach ($m in $names) {
            $name = $m.Groups[1].Value
            if ($mode -eq "dev") { [void]$switchNames.Add($name) }
            elseif ($mode -eq "release") { $failures.Add("switches.h:$number $name sits in the release branch, so a release build holds it") }
            else { $failures.Add("switches.h:$number $name is outside any NEBULA_DEV branch, so a release build holds it") }
        }
        if ($mode -eq "release" -and $line -match 'getenv|GetEnvironmentVariable') {
            $failures.Add("switches.h:$number the release branch reads the environment: $($line.Trim())")
        }
    }
    if ($ifs -eq 0 -or $ifs -ne $elses) { $failures.Add("switches.h has $ifs release branches and $elses dev branches: the pairs do not match") }
    if ($switchNames.Count -eq 0) { $failures.Add("switches.h lists no test switch names: the check could not read it") }
}
# A file that is test code is whole inside NEBULA_DEV.
foreach ($name in @("run_options.cpp", "scripted_input.cpp", "data_guard.cpp")) {
    $file = Join-Path $repo "tools\test_driver\$name"
    if (-not (Test-Path -LiteralPath $file)) { $failures.Add("tools/test_driver/$name is missing"); continue }
    $text = [IO.File]::ReadAllText($file)
    if ($text -notmatch '(?s)\A(?:\s*//[^\n]*\n)*\s*#if defined\(NEBULA_DEV\)' -or $text -notmatch '#endif\s*\z') {
        $failures.Add("tools/test_driver/$name is not whole inside #if defined(NEBULA_DEV) ... #endif")
    }
}
# Each .inc of the test driver is included only where NEBULA_DEV is defined.
$incNames = @(Get-ChildItem -LiteralPath (Join-Path $repo "tools\test_driver") -Filter "*.inc" | ForEach-Object { $_.Name })
foreach ($folder in @("native", "runtime", "platform", "tools")) {
    $root = Join-Path $repo $folder
    if (-not (Test-Path -LiteralPath $root)) { continue }
    foreach ($source in Get-ChildItem -LiteralPath $root -Recurse -File -Include "*.cpp", "*.h", "*.hpp", "*.c", "*.inc") {
        if ($source.DirectoryName -like "*\tools\test_driver") { continue }
        $text = [IO.File]::ReadAllText($source.FullName)
        $hit = $false
        foreach ($inc in $incNames) { if ($text.Contains($inc)) { $hit = $true; break } }
        if (-not $hit) { continue }
        $stack = New-Object System.Collections.Generic.List[object]
        $number = 0
        foreach ($line in [IO.File]::ReadAllLines($source.FullName)) {
            $number++
            if ($line -match '^\s*#\s*(ifdef|ifndef|if)\b(.*)$') {
                $kind = $Matches[1]
                $cond = $Matches[2].Trim()
                if ($kind -eq "ifdef") { $cond = "defined($cond)" } elseif ($kind -eq "ifndef") { $cond = "!defined($cond)" }
                $stack.Add(@{ Cond = $cond; Else = $false })
                continue
            }
            if ($line -match '^\s*#\s*elif\b(.*)$' -and $stack.Count -gt 0) { $stack[$stack.Count - 1] = @{ Cond = $Matches[1].Trim(); Else = $false }; continue }
            if ($line -match '^\s*#\s*else\b' -and $stack.Count -gt 0) { $stack[$stack.Count - 1].Else = $true; continue }
            if ($line -match '^\s*#\s*endif\b' -and $stack.Count -gt 0) { $stack.RemoveAt($stack.Count - 1); continue }
            if ($line -match '^\s*#\s*include\s+"[^"]*?([A-Za-z0-9_]+\.inc)"') {
                $incName = $Matches[1]
                if ($incNames -notcontains $incName) { continue }
                $guarded = $false
                foreach ($entry in $stack) {
                    $positive = ($entry.Cond -match '(?<!!)defined\(NEBULA_DEV\)') -and ($entry.Cond -notmatch '\|\|') -and ($entry.Cond -notmatch '!\s*defined\(NEBULA_DEV\)')
                    $negative = $entry.Cond -match '!\s*defined\(NEBULA_DEV\)'
                    if (($positive -and -not $entry.Else) -or ($negative -and $entry.Else)) { $guarded = $true }
                }
                if (-not $guarded) { $failures.Add("$($source.FullName.Substring($repo.Length + 1)):$number includes $incName outside NEBULA_DEV") }
            }
        }
    }
}
# The build defines NEBULA_DEV only when NEBULA_RELEASE is off.
foreach ($cmake in @("CMakeLists.txt", "native\CMakeLists.txt", "platform\sdl\CMakeLists.txt")) {
    $file = Join-Path $repo $cmake
    if (-not (Test-Path -LiteralPath $file)) { $failures.Add("$cmake is missing"); continue }
    $text = [IO.File]::ReadAllText($file)
    if ($text -notmatch '(?s)if\(NEBULA_RELEASE\)[^\r\n]*\r?\n[^\r\n]*NEBULA_RELEASE=1[^\r\n]*\r?\nelse\(\)\s*\r?\n[^\r\n]*NEBULA_DEV=1') {
        $failures.Add("$cmake does not define NEBULA_DEV only in the else() of if(NEBULA_RELEASE)")
    }
    if ([regex]::Matches($text, 'NEBULA_DEV=1').Count -ne 1) {
        $failures.Add("$cmake defines NEBULA_DEV in $([regex]::Matches($text, 'NEBULA_DEV=1').Count) places; exactly one, in the else() branch, is expected")
    }
}

# A game-module file that names a test option holds the read under a NEBULA_DEV conditional.
$petariSource = Join-Path $repo "native\petari\src"
if (Test-Path -LiteralPath $petariSource) {
    foreach ($source in Get-ChildItem -LiteralPath $petariSource -Recurse -File -Include "*.cpp", "*.hpp", "*.c", "*.h") {
        $text = [IO.File]::ReadAllText($source.FullName)
        if ($text.Contains("NEBULA_OPTION_TEST_") -and $text -notmatch 'defined\(NEBULA_DEV\)') {
            $failures.Add("$($source.FullName.Substring($repo.Length + 1)) names a test option outside any NEBULA_DEV conditional")
        }
    }
}

# ---- 2. What may not be in any shipped file ------------------------------------------------------------------------
$prefixes = @("GALAXY_TEST_", "NEBULA_OPTION_TEST_", "NEBULA_TEST_", "NEBULA_DEV", "GALAXY_TRACE_", "GALAXY_DUMP_", "GALAXY_EXPORT_", "GALAXY_DEBUG_", "GALAXY_INPUT_",
              "GALAXY_PRESENT_SNAPSHOT", "GALAXY_INTERP_SNAPSHOT", "GALAXY_AUDIO_WAV", "GALAXY_HIDE_WINDOW",
              "GALAXY_BOOT_FAST", "GALAXY_PROFILE_GAME", "GALAXY_INSPECT_", "GALAXY_CRASH_DUMP_DIR", "GALAXY_VK_")
$terms = New-Object System.Collections.Generic.List[string]
$privateTerms = Join-Path $env:USERPROFILE ".nebula-release-key\private-terms.txt"
if (Test-Path -LiteralPath $privateTerms) {
    foreach ($line in Get-Content -LiteralPath $privateTerms) { if ($line.Trim() -ne "") { $terms.Add($line.Trim()) } }
} else {
    $failures.Add("$privateTerms is missing: the personal terms were not checked")
}
$allowed = New-Object System.Collections.Generic.List[string]
if ($Allow -ne "") {
    if (-not (Test-Path -LiteralPath $Allow)) { $failures.Add("-Allow file $Allow is missing") }
    else { foreach ($line in Get-Content -LiteralPath $Allow) { $t = $line.Trim(); if ($t -ne "" -and -not $t.StartsWith("#")) { $allowed.Add($t) } } }
}

$scanner = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Text;
using System.Text.RegularExpressions;

public sealed class NebulaScanOptions
{
    public string[] Prefixes = new string[0];
    public string[] Names = new string[0];
    public string[] Terms = new string[0];
    public string[] Allow = new string[0];
    public int MaxPerKind = 8;
}

// Reads a file (and the entries of a zip-format file) in windows of 8 MiB that overlap by 2 KiB, as Latin-1 text (bytes, ASCII and
// UTF-8), and as UTF-16 text at both byte alignments. A finding is reported once, by the window that holds its start.
public static class NebulaReleaseScan
{
    private const int Chunk = 8 * 1024 * 1024;
    private const int Overlap = 2048;
    private static readonly Encoding Latin1 = Encoding.GetEncoding(28591);
    private static readonly TimeSpan Limit = TimeSpan.FromSeconds(120);
    private const RegexOptions Opt = RegexOptions.CultureInvariant | RegexOptions.Compiled;

    private static readonly Regex Token = new Regex("(?:GALAXY|NEBULA)_[A-Z0-9_]+", Opt, Limit);
    private static readonly Regex WinUser = new Regex(
        @"(?<![A-Za-z0-9])[A-Za-z]:[\\/]{1,2}Users[\\/]{1,2}(?!(?:Public|Default|All Users|Default User)[\\/])(?![%<{$\[])(?!(?:user|username|name|yourname|you)[\\/])[^\\/:*?""<>|\x00-\x1f]{1,64}[\\/]",
        Opt | RegexOptions.IgnoreCase, Limit);
    private static readonly Regex UnixUser = new Regex(
        @"(?<![A-Za-z0-9._~-])/(?:Users|home)/(?!(?:user|username|name|yourname|you)/)[A-Za-z0-9._-]{1,32}/", Opt, Limit);
    private static readonly Regex Email = new Regex(
        @"(?<![A-Za-z0-9._%+-])([A-Za-z0-9._%+-]{1,64})@((?:[A-Za-z0-9-]{1,63}\.){1,4}[A-Za-z]{2,24})(?![A-Za-z0-9-])", Opt, Limit);
    private static readonly Regex MacSeparated = new Regex(
        @"(?<![0-9A-Fa-f:-])[0-9A-Fa-f]{2}([:-])[0-9A-Fa-f]{2}(?:\1[0-9A-Fa-f]{2}){4}(?![0-9A-Fa-f:-])", Opt, Limit);
    private static readonly Regex MacDotted = new Regex(
        @"(?<![0-9A-Fa-f.])[0-9A-Fa-f]{4}\.[0-9A-Fa-f]{4}\.[0-9A-Fa-f]{4}(?![0-9A-Fa-f.])", Opt, Limit);
    private static readonly Regex MacBare = new Regex(
        @"(?:bd_?addr|mac(?:[ _-]?addr(?:ess)?)?|bluetooth(?: address)?)\W{1,4}([0-9a-f]{12})(?![0-9a-f])",
        Opt | RegexOptions.IgnoreCase, Limit);

    public static List<string> ScanFile(string path, string label, NebulaScanOptions options)
    {
        List<string> results = new List<string>();
        using (FileStream stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
        {
            ScanStream(stream, label, options, results, 0);
        }
        return results;
    }

    private static bool IsZip(string name)
    {
        string lower = name.ToLowerInvariant();
        return lower.EndsWith(".zip") || lower.EndsWith(".apk") || lower.EndsWith(".jar") || lower.EndsWith(".nupkg") || lower.EndsWith(".aar");
    }

    private static void ScanStream(Stream stream, string label, NebulaScanOptions o, List<string> results, int depth)
    {
        Dictionary<string, int> counts = new Dictionary<string, int>();
        byte[] buffer = new byte[Chunk + Overlap];
        int carry = 0;
        long baseOffset = 0;
        while (true)
        {
            int got = 0;
            while (carry + got < buffer.Length)
            {
                int n = stream.Read(buffer, carry + got, buffer.Length - carry - got);
                if (n <= 0) break;
                got += n;
            }
            int length = carry + got;
            if (length == 0) break;
            bool last = carry + got < buffer.Length;
            int limit = last ? length : length - Overlap;
            string latin = Latin1.GetString(buffer, 0, length);
            ScanText(latin, 0, 1, baseOffset, limit, label, o, results, counts, false);
            for (int align = 0; align < 2; align++)
            {
                int wideBytes = (length - align) & ~1;
                if (wideBytes <= 0) continue;
                string wide = Encoding.Unicode.GetString(buffer, align, wideBytes);
                ScanText(wide, align, 2, baseOffset, limit, label, o, results, counts, true);
            }
            if (last) break;
            Buffer.BlockCopy(buffer, length - Overlap, buffer, 0, Overlap);
            baseOffset += length - Overlap;
            carry = Overlap;
        }
        foreach (KeyValuePair<string, int> pair in counts)
        {
            if (pair.Value > o.MaxPerKind) results.Add(label + ": " + pair.Key + ": " + (pair.Value - o.MaxPerKind) + " more");
        }
        if (depth < 2 && stream.CanSeek && IsZip(label))
        {
            stream.Position = 0;
            try
            {
                using (ZipArchive archive = new ZipArchive(stream, ZipArchiveMode.Read, true))
                {
                    foreach (ZipArchiveEntry entry in archive.Entries)
                    {
                        if (entry.Length == 0) continue;
                        using (Stream inner = entry.Open())
                        {
                            if (IsZip(entry.FullName) && entry.Length < int.MaxValue)
                            {
                                MemoryStream copy = new MemoryStream((int)entry.Length);
                                inner.CopyTo(copy);
                                copy.Position = 0;
                                ScanStream(copy, label + "!" + entry.FullName, o, results, depth + 1);
                            }
                            else
                            {
                                ScanStream(inner, label + "!" + entry.FullName, o, results, depth + 1);
                            }
                        }
                    }
                }
            }
            catch (InvalidDataException)
            {
                results.Add(label + ": an archive that could not be opened, its entries were not read");
            }
        }
    }

    private static void Add(string kind, string shown, long offset, string raw, string label, NebulaScanOptions o,
                            List<string> results, Dictionary<string, int> counts)
    {
        foreach (string allowed in o.Allow)
        {
            if (raw.IndexOf(allowed, StringComparison.OrdinalIgnoreCase) >= 0) return;
        }
        int count;
        counts.TryGetValue(kind, out count);
        counts[kind] = count + 1;
        if (count < o.MaxPerKind) results.Add(label + ": " + kind + " at 0x" + offset.ToString("x") + " (" + shown + ")");
    }

    private static string Hide(string raw, int keep)
    {
        return raw.Substring(0, Math.Min(keep, raw.Length)) + "*** (" + raw.Length + " chars)";
    }

    private static bool Inside(Match m, int align, int step, long baseOffset, int limit, out long offset)
    {
        long local = align + (long)step * m.Index;
        offset = baseOffset + local;
        return local < limit;
    }

    private static bool Same(string hex)
    {
        string digits = Regex.Replace(hex, "[^0-9A-Fa-f]", "");
        for (int i = 2; i < digits.Length; i += 2)
        {
            if (!string.Equals(digits.Substring(i, 2), digits.Substring(0, 2), StringComparison.OrdinalIgnoreCase)) return false;
        }
        return true;
    }

    private static bool EmailAllowed(string local, string domain)
    {
        string d = domain.ToLowerInvariant();
        string l = local.ToLowerInvariant();
        if (d == "users.noreply.github.com") return true;
        if (d == "github.com" && (l == "noreply" || l == "git")) return true;
        if (d == "example.com" || d == "example.org" || d == "example.net") return true;
        if (Regex.IsMatch(d, @"^[0-9]+x\.")) return true;
        return false;
    }

    private static void ScanText(string text, int align, int step, long baseOffset, int limit, string label, NebulaScanOptions o,
                                 List<string> results, Dictionary<string, int> counts, bool wide)
    {
        long offset;
        foreach (Match m in Token.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset)) continue;
            string name = m.Value;
            bool bad = false;
            foreach (string prefix in o.Prefixes) { if (name.StartsWith(prefix, StringComparison.Ordinal)) { bad = true; break; } }
            if (!bad) { foreach (string known in o.Names) { if (name.StartsWith(known, StringComparison.Ordinal)) { bad = true; break; } } }
            if (bad) Add("test switch", name, offset, name, label, o, results, counts);
        }
        foreach (Match m in WinUser.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset)) continue;
            int at = m.Value.IndexOf("Users", StringComparison.OrdinalIgnoreCase) + 5;
            while (at < m.Value.Length && (m.Value[at] == '\\' || m.Value[at] == '/')) at++;
            Add("user path", m.Value.Substring(0, at) + "***", offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in UnixUser.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset)) continue;
            int at = m.Value.IndexOf('/', 1) + 1;
            Add("user path", m.Value.Substring(0, at) + "***", offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in Email.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset)) continue;
            if (EmailAllowed(m.Groups[1].Value, m.Groups[2].Value)) continue;
            Add("e-mail address", "***@" + m.Groups[2].Value, offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in MacSeparated.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Same(m.Value)) continue;
            Add("MAC address", Hide(m.Value, 0), offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in MacDotted.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Same(m.Value) || !Regex.IsMatch(m.Value, "[A-Fa-f]")) continue;
            Add("MAC address", Hide(m.Value, 0), offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in MacBare.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Same(m.Groups[1].Value)) continue;
            Add("MAC address", Hide(m.Value, 0), offset, m.Value, label, o, results, counts);
        }
        for (int t = 0; t < o.Terms.Length; t++)
        {
            string term = wide ? o.Terms[t] : Latin1.GetString(Encoding.UTF8.GetBytes(o.Terms[t]));
            int start = 0;
            while (term.Length > 0 && (start = text.IndexOf(term, start, StringComparison.OrdinalIgnoreCase)) >= 0)
            {
                long local = align + (long)step * start;
                if (local < limit)
                {
                    Add("personal term", "term " + (t + 1) + " of private-terms.txt", baseOffset + local, term, label, o, results, counts);
                }
                start += term.Length;
            }
        }
    }
}
'@
if (-not ('NebulaReleaseScan' -as [type])) {
    Add-Type -TypeDefinition $scanner -ReferencedAssemblies "System.IO.Compression"
}
$options = New-Object NebulaScanOptions
$options.Prefixes = [string[]]$prefixes
$options.Names = [string[]]@($switchNames)
$options.Terms = [string[]]$terms.ToArray()
$options.Allow = [string[]]$allowed.ToArray()

# ---- 2b. The files git tracks --------------------------------------------------------------------------------------------
if ($SkipSource) {
    $notes.Add("tracked files were not scanned (-SkipSource)")
} else {
    $sourceOptions = New-Object NebulaScanOptions
    $sourceOptions.Terms = $options.Terms
    $sourceOptions.Allow = $options.Allow
    $gitExe = Join-Path $env:ProgramFiles "Git\cmd\git.exe"
    if (-not (Test-Path -LiteralPath $gitExe)) {
        $found = Get-Command git -ErrorAction SilentlyContinue
        $gitExe = if ($null -ne $found) { $found.Source } else { "" }
    }
    if ($gitExe -eq "") {
        $failures.Add("git was not found: the tracked files were not checked for personal names, paths, e-mail or Bluetooth addresses")
    } else {
        $tracked = @(& $gitExe -c core.quotepath=false -C $repo ls-files)
        if ($LASTEXITCODE -ne 0 -or $tracked.Count -eq 0) {
            $failures.Add("git ls-files failed: the tracked files were not checked")
        } else {
            $vendored = 0
            $unread = 0
            $binaryName = '\.(png|jpe?g|gif|ico|webp|ttf|otf|woff2?|zip|7z|gz|bin|dds|ktx2?|mp4|ogg|wav|jar|aar|dll|exe|lib|a|so|pdf)$'
            foreach ($relative in $tracked) {
                if ($relative -like "third_party/*" -or $relative -like "docs/licenses/*" -or $relative -match $binaryName) { $vendored++; continue }
                $full = Join-Path $repo $relative
                if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { $unread++; continue }
                foreach ($finding in [NebulaReleaseScan]::ScanFile($full, "source " + $relative, $sourceOptions)) { $failures.Add($finding) }
            }
            if ($vendored -ne 0) { $notes.Add("$vendored tracked files (third_party/, docs/licenses/, binary pictures, fonts and archives) were not scanned: they hold their authors' details or arbitrary bytes") }
            if ($unread -ne 0) { $notes.Add("$unread tracked paths could not be opened (deleted in the tree, or a name outside the console code page)") }
        }
    }
}

$root = (Resolve-Path -LiteralPath $Path).Path
$files = @(Get-ChildItem -LiteralPath $root -Recurse -File)
if ($files.Count -eq 0) { Write-Output "release_check: no files under $Path"; exit 1 }
$shipped = New-Object System.Collections.Generic.List[object]
foreach ($file in $files) { $shipped.Add([pscustomobject]@{ Full = $file.FullName; Label = $file.FullName.Substring($root.Length).TrimStart('\'); Installer = $false }) }
foreach ($setup in $Installer) {
    if (-not (Test-Path -LiteralPath $setup)) { $failures.Add("installer $setup is missing"); continue }
    $full = (Resolve-Path -LiteralPath $setup).Path
    $shipped.Add([pscustomobject]@{ Full = $full; Label = "installer " + [IO.Path]::GetFileName($full); Installer = $true })
}

foreach ($item in $shipped) {
    $extension = [IO.Path]::GetExtension($item.Full).ToLowerInvariant()
    if ($extension -in @(".exe", ".dll")) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($item.Full)
        if ([string]::IsNullOrWhiteSpace($info.ProductName) -or [string]::IsNullOrWhiteSpace($info.FileVersion) -or
            [string]::IsNullOrWhiteSpace($info.FileDescription)) {
            $failures.Add("$($item.Label): no version information (product, file version and description)")
        }
    }
    foreach ($finding in [NebulaReleaseScan]::ScanFile($item.Full, $item.Label, $options)) { $failures.Add($finding) }
}

# ---- 4. Signatures ----------------------------------------------------------------------------------------------------
$ours = '^(Nebula.*\.exe|smg_game.*\.dll)$'
foreach ($item in $shipped) {
    $extension = [IO.Path]::GetExtension($item.Full).ToLowerInvariant()
    if ($extension -notin @(".exe", ".dll", ".msi")) { continue }
    $signature = Get-AuthenticodeSignature -LiteralPath $item.Full
    $name = [IO.Path]::GetFileName($item.Full)
    switch ($signature.Status.ToString()) {
        "Valid" { }
        "NotSigned" {
            if ($RequireSigned -and ($item.Installer -or $name -match $ours)) { $failures.Add("$($item.Label): unsigned (-RequireSigned)") }
            else { $notes.Add("$($item.Label): unsigned") }
        }
        default { $failures.Add("$($item.Label): Authenticode signature is $($signature.Status): $($signature.StatusMessage)") }
    }
}
$publicKeyFile = Join-Path $repo "installer\release-public-key.xml"
# Signing is configured when the release key's private file is where the installer build looks for it (only its
# existence is tested), or when -RequireSigned says it must be.
$signingConfigured = $RequireSigned.IsPresent -or (Test-Path -LiteralPath (Join-Path $env:USERPROFILE ".nebula-release-key\nebula-release-private.xml"))
foreach ($item in $shipped) {
    if (-not $item.Installer) { continue }
    $signatureFile = $item.Full + ".sig"
    if (-not (Test-Path -LiteralPath $signatureFile)) {
        if ($signingConfigured) { $failures.Add("$($item.Label): no release-key .sig although signing is configured (updates will not run it)") }
        else { $notes.Add("$($item.Label): unsigned (no release-key .sig; updates will not run it)") }
        continue
    }
    if (-not (Test-Path -LiteralPath $publicKeyFile)) { $failures.Add("installer/release-public-key.xml is missing: $($item.Label) could not be verified"); continue }
    $rsa = New-Object System.Security.Cryptography.RSACryptoServiceProvider
    $rsa.PersistKeyInCsp = $false
    $rsa.FromXmlString((Get-Content -LiteralPath $publicKeyFile -Raw))
    try { $signatureBytes = [Convert]::FromBase64String((Get-Content -LiteralPath $signatureFile -Raw).Trim()) }
    catch { $failures.Add("$($item.Label): the .sig is not base64"); continue }
    $verified = $rsa.VerifyData([IO.File]::ReadAllBytes($item.Full), [Security.Cryptography.CryptoConfig]::MapNameToOID("SHA256"), $signatureBytes)
    if (-not $verified) { $failures.Add("$($item.Label): the release-key signature does not verify against installer/release-public-key.xml") }
}

# ---- 5. Defender ------------------------------------------------------------------------------------------------------
if ($SkipDefender) {
    $notes.Add("Defender scan skipped (-SkipDefender)")
} else {
    $defender = Join-Path $env:ProgramFiles "Windows Defender\MpCmdRun.exe"
    if (-not (Test-Path -LiteralPath $defender)) {
        $failures.Add("Microsoft Defender's command line scanner is missing: nothing was scanned")
    } else {
        foreach ($item in $shipped) {
            $output = & $defender -Scan -ScanType 3 -File $item.Full -DisableRemediation 2>&1
            if ($LASTEXITCODE -ne 0) {
                $failures.Add("$($item.Label): Defender reports a threat or failed to scan (exit $LASTEXITCODE): $(($output | Select-Object -Last 3) -join ' ')")
            }
        }
    }
}

foreach ($note in $notes) { Write-Output "release_check: note: $note" }
if ($failures.Count -ne 0) {
    Write-Output "release_check: $($failures.Count) failure(s) in $Path"
    $failures | ForEach-Object { Write-Output "  $_" }
    exit 1
}
Write-Output "release_check: $($shipped.Count) files passed (release flag, version information, no test switches, user paths, e-mail or MAC addresses, signatures, Defender)"
exit 0
