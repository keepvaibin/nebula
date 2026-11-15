# release_check.ps1 -Path <folder> [-Installer <file>...] [-Allow <file>] [-RequireSigned] [-SkipDefender]
# The gate every release folder passes before it is published. It checks, and exits 1 listing each failure:
#   1. One build. No production source (native, runtime, platform; tools/ and *_tests.* are test code) names
#      NEBULA_DEV, NEBULA_RELEASE or a test option (NEBULA_OPTION_TEST_), or includes tools/test_driver; no CMake file
#      defines either name; -Path holds no test extension (nebula_test_extension.*).
#   2. Every file of -Path and every -Installer file, as bytes (ASCII and UTF-8 text) and as UTF-16 text, holds none of
#      the following. A zip-format file (.zip, .apk, .jar, .nupkg, .aar, .luma) is read as its entries' names and
#      unpacked contents, and a .png as its text chunks (tEXt, zTXt, iTXt), since compressed bytes are arbitrary. The
#      checks: a test's environment name (the
#      prefixes below: GALAXY_TEST_, NEBULA_OPTION_TEST_, NEBULA_TEST_, NEBULA_DEV and others), a Windows or Unix user-profile path (drive:\Users\<name>\, /Users/<name>/, /home/<name>/), an
#      e-mail address other than a GitHub noreply one (a one-letter name right after a control byte is binary data, such
#      as a float constant's last byte "@" before a string; a license text's own authors are theirs to name: a file in a
#      licenses folder, or named LICENSE, NOTICE, COPYING or THIRD-PARTY-NOTICES), a MAC address (six hex pairs joined by colons or
#      hyphens, three hex quads joined by dots, or 12 hex digits after "mac"/"bdaddr"/"bluetooth"), or a personal term from the release key's
#      private-terms.txt (one per line; kept out of the repository, so no name is ever written here). A finding prints
#      its kind, file and offset with the private part masked. tools/release_check_allow.txt lists literal match texts
#      to let through, each a third-party project's own address with where it comes from; -Allow names a local file of
#      more (one per line, # starts a comment).
#      The same personal-detail checks (paths, e-mail, MAC, private terms; not the environment names, which the test tools hold
#      on purpose) run over every text file git tracks in this repository, except third_party/ and docs/licenses/ (their authors'
#      details). There a path, e-mail or MAC finding is let through when its line or the line above it says
#      "release-check: allow": a test's made-up text, marked where it is written. -SkipSource leaves the tracked files out.
#      An -Installer .exe with a PAYLOAD resource (Nebula Setup's zip of what it installs) has its entries unpacked and
#      checked as shipped files by 2, 3 and 5; its own bytes are read with the payload's left out (read as zeros).
#   3. Every .exe and .dll carries version information (product, file version, description), and none is a .NET
#      assembly (1.0.0 and 1.0.1, built with .NET, were flagged by antivirus programs; 1.0.2's native ones were not).
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

# ---- 1. One build ------------------------------------------------------------------------------------------------------
foreach ($folder in @("native", "runtime", "platform")) {
    $root = Join-Path $repo $folder
    if (-not (Test-Path -LiteralPath $root)) { $failures.Add("$folder is missing"); continue }
    # Filtered by Where-Object: Windows PowerShell 5.1 ignores -Include together with -LiteralPath and lists every file.
    foreach ($source in Get-ChildItem -LiteralPath $root -Recurse -File |
                        Where-Object { $_.Extension -in ".cpp", ".h", ".hpp", ".c", ".inc", ".mm", ".kt", ".java" }) {
        if ($source.BaseName -like "*_tests" -or $source.FullName -like "*\third_party\*") { continue }
        $text = [IO.File]::ReadAllText($source.FullName)
        $relative = $source.FullName.Substring($repo.Length + 1)
        foreach ($name in @("NEBULA_DEV", "NEBULA_RELEASE", "NEBULA_OPTION_TEST_", "tools/test_driver")) {
            if ($text.Contains($name)) { $failures.Add("$relative names ${name}: a release and a test run use the same source") }
        }
    }
}
foreach ($cmake in Get-ChildItem -LiteralPath $repo -Recurse -File |
                   Where-Object { ($_.Name -eq "CMakeLists.txt" -or $_.Extension -eq ".cmake") -and
                                  $_.FullName -notlike "*\third_party\*" -and $_.FullName -notlike "*\build*" }) {
    $text = [IO.File]::ReadAllText($cmake.FullName)
    if ($text -match 'NEBULA_DEV|NEBULA_RELEASE') {
        $failures.Add("$($cmake.FullName.Substring($repo.Length + 1)) defines a build kind: there is one build")
    }
}
foreach ($file in Get-ChildItem -LiteralPath $Path -Recurse -File -Filter "nebula_test_extension.*" -ErrorAction SilentlyContinue) {
    $failures.Add("$($file.FullName) is the test extension: tests attach it, a release never holds it")
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
foreach ($line in Get-Content -LiteralPath (Join-Path $PSScriptRoot "release_check_allow.txt")) {
    $t = $line.Trim(); if ($t -ne "" -and -not $t.StartsWith("#")) { $allowed.Add($t) }
}
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
    public bool LineMarkers = false;  // tracked files: "release-check: allow" on a finding's line or the line above
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

    // A file with one byte range read as zeros: an installer's payload, whose unpacked entries are checked on their own.
    public static List<string> ScanFile(string path, string label, NebulaScanOptions options, long skipAt, long skipLength)
    {
        byte[] bytes = File.ReadAllBytes(path);
        Array.Clear(bytes, checked((int)skipAt), checked((int)skipLength));
        List<string> results = new List<string>();
        using (MemoryStream stream = new MemoryStream(bytes, false))
        {
            ScanStream(stream, label, options, results, 0);
        }
        return results;
    }

    private static bool IsZip(string name)
    {
        string lower = name.ToLowerInvariant();
        return lower.EndsWith(".zip") || lower.EndsWith(".apk") || lower.EndsWith(".jar") || lower.EndsWith(".nupkg") ||
               lower.EndsWith(".aar") || lower.EndsWith(".luma");
    }

    private static void ScanStream(Stream stream, string label, NebulaScanOptions o, List<string> results, int depth)
    {
        // A zip-format file is read as its entries, a PNG as its text chunks; their bytes as they are only when they do
        // not open as one.
        if (depth < 2 && stream.CanSeek && IsZip(label) && ScanArchive(stream, label, o, results, depth)) return;
        if (label.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
        {
            MemoryStream copy = new MemoryStream();
            stream.CopyTo(copy);
            byte[] png = copy.ToArray();
            if (ScanPngText(png, label, o, results)) return;
            stream = new MemoryStream(png);
        }
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
    }

    // Each entry's name, then its unpacked contents; false when the file does not open as a zip archive.
    private static bool ScanArchive(Stream stream, string label, NebulaScanOptions o, List<string> results, int depth)
    {
        stream.Position = 0;
        try
        {
            using (ZipArchive archive = new ZipArchive(stream, ZipArchiveMode.Read, true))
            {
                Dictionary<string, int> counts = new Dictionary<string, int>();
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    string name = entry.FullName;
                    ScanText(name, 0, 1, 0, name.Length, label + "!" + name + " (its name)", o, results, counts, false);
                    if (entry.Length == 0) continue;
                    using (Stream inner = entry.Open())
                    {
                        if (IsZip(name) && entry.Length < int.MaxValue)
                        {
                            MemoryStream copy = new MemoryStream((int)entry.Length);
                            inner.CopyTo(copy);
                            copy.Position = 0;
                            ScanStream(copy, label + "!" + name, o, results, depth + 1);
                        }
                        else
                        {
                            ScanStream(inner, label + "!" + name, o, results, depth + 1);
                        }
                    }
                }
            }
            return true;
        }
        catch (InvalidDataException)
        {
            results.Add(label + ": an archive that could not be opened, its bytes were read as they are");
            stream.Position = 0;
            return false;
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

    // A PNG's text chunks (tEXt, and zTXt and iTXt unpacked), where a name or path would be; false when it is no PNG.
    private static bool ScanPngText(byte[] b, string label, NebulaScanOptions o, List<string> results)
    {
        if (b.Length < 8 || b[0] != 0x89 || b[1] != 0x50 || b[2] != 0x4E || b[3] != 0x47) return false;
        Dictionary<string, int> counts = new Dictionary<string, int>();
        int at = 8;
        while (at + 12 <= b.Length)
        {
            long length = ((long)b[at] << 24) | ((long)b[at + 1] << 16) | ((long)b[at + 2] << 8) | b[at + 3];
            if (at + 12 + length > b.Length) break;
            string type = Encoding.ASCII.GetString(b, at + 4, 4);
            int data = at + 8;
            byte[] text = type == "tEXt" ? Slice(b, data, (int)length)
                        : type == "zTXt" || type == "iTXt" ? ChunkText(type, b, data, (int)length) : null;
            if (text != null) ScanText(Latin1.GetString(text), 0, 1, data, text.Length, label + " (" + type + ")", o, results, counts, false);
            if (type == "IEND") break;
            at = data + (int)length + 4;
        }
        return true;
    }

    // A zTXt or iTXt chunk's keyword and text, the text unpacked when the chunk holds it compressed.
    private static byte[] ChunkText(string type, byte[] b, int data, int length)
    {
        int end = data + length;
        int nul = Array.IndexOf(b, (byte)0, data, length);
        if (nul < 0) return Slice(b, data, length);
        bool compressed;
        int at;
        if (type == "zTXt")
        {
            compressed = true;
            at = nul + 2;  // the compression method byte
        }
        else
        {
            compressed = nul + 1 < end && b[nul + 1] != 0;
            at = nul + 3;  // the compression flag and method; then the language tag and translated keyword, each ending in 0
            for (int field = 0; field < 2 && at < end; field++)
            {
                int zero = Array.IndexOf(b, (byte)0, at, end - at);
                at = zero < 0 ? end : zero + 1;
            }
        }
        if (at > end) at = end;
        byte[] body = Slice(b, at, end - at);
        if (compressed) body = Inflate(body);
        byte[] all = new byte[nul - data + 1 + body.Length];
        Buffer.BlockCopy(b, data, all, 0, nul - data);
        all[nul - data] = (byte)' ';
        Buffer.BlockCopy(body, 0, all, nul - data + 1, body.Length);
        return all;
    }

    // zlib data unpacked; as it is when it does not unpack, so nothing goes unread.
    private static byte[] Inflate(byte[] z)
    {
        if (z.Length < 2) return z;
        try
        {
            using (MemoryStream input = new MemoryStream(z, 2, z.Length - 2))
            using (DeflateStream inflate = new DeflateStream(input, CompressionMode.Decompress))
            using (MemoryStream output = new MemoryStream())
            {
                inflate.CopyTo(output);
                return output.ToArray();
            }
        }
        catch (InvalidDataException)
        {
            return z;
        }
    }

    private static byte[] Slice(byte[] b, int at, int count)
    {
        byte[] s = new byte[Math.Max(0, count)];
        Buffer.BlockCopy(b, at, s, 0, s.Length);
        return s;
    }

    // A license text names its authors: the shipped copy of what docs/licenses/ and third_party/ hold in the repository.
    private static bool LicenseText(string label)
    {
        string name = label.Substring(label.LastIndexOf('!') + 1).Replace('\\', '/').ToLowerInvariant();
        if (name.Contains("/licenses/") || name.StartsWith("licenses/")) return true;
        string file = name.Substring(name.LastIndexOf('/') + 1);
        return file.StartsWith("license") || file.StartsWith("licence") || file.StartsWith("notice") ||
               file.StartsWith("copying") || file.StartsWith("third-party-notices");
    }

    private const string Marker = "release-check: allow";

    // A tracked file marks a test's made-up text where it is written: the marker on the finding's line or the line above.
    private static bool Marked(string text, int index, NebulaScanOptions o)
    {
        if (!o.LineMarkers) return false;
        int start = text.LastIndexOf('\n', index) + 1;
        int above = start >= 2 ? text.LastIndexOf('\n', start - 2) + 1 : 0;
        int end = text.IndexOf('\n', index);
        if (end < 0) end = text.Length;
        return text.IndexOf(Marker, above, end - above, StringComparison.Ordinal) >= 0;
    }

    // A one-letter name glued to a control byte is binary data (a constant whose last byte is "@" before a string).
    private static bool BinaryEmail(Match m, string text)
    {
        if (m.Groups[1].Length != 1 || m.Index == 0) return false;
        char before = text[m.Index - 1];
        return before < ' ' && before != '\t' && before != '\n' && before != '\r';
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
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Marked(text, m.Index, o)) continue;
            int at = m.Value.IndexOf("Users", StringComparison.OrdinalIgnoreCase) + 5;
            while (at < m.Value.Length && (m.Value[at] == '\\' || m.Value[at] == '/')) at++;
            Add("user path", m.Value.Substring(0, at) + "***", offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in UnixUser.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Marked(text, m.Index, o)) continue;
            int at = m.Value.IndexOf('/', 1) + 1;
            Add("user path", m.Value.Substring(0, at) + "***", offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in Email.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Marked(text, m.Index, o)) continue;
            if (EmailAllowed(m.Groups[1].Value, m.Groups[2].Value) || BinaryEmail(m, text) || LicenseText(label)) continue;
            Add("e-mail address", "***@" + m.Groups[2].Value, offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in MacSeparated.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Same(m.Value) || Marked(text, m.Index, o)) continue;
            Add("MAC address", Hide(m.Value, 0), offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in MacDotted.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Same(m.Value) || !Regex.IsMatch(m.Value, "[A-Fa-f]") ||
                Marked(text, m.Index, o)) continue;
            Add("MAC address", Hide(m.Value, 0), offset, m.Value, label, o, results, counts);
        }
        foreach (Match m in MacBare.Matches(text))
        {
            if (!Inside(m, align, step, baseOffset, limit, out offset) || Same(m.Groups[1].Value) || Marked(text, m.Index, o)) continue;
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
$options.Terms = [string[]]$terms.ToArray()
$options.Allow = [string[]]$allowed.ToArray()

# ---- 2b. The files git tracks --------------------------------------------------------------------------------------------
if ($SkipSource) {
    $notes.Add("tracked files were not scanned (-SkipSource)")
} else {
    $sourceOptions = New-Object NebulaScanOptions
    $sourceOptions.Terms = $options.Terms
    $sourceOptions.Allow = $options.Allow
    $sourceOptions.LineMarkers = $true
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

# Nebula Setup carries what it installs as a zip in its PAYLOAD resource: the entries are unpacked to a temporary folder
# and checked as shipped files (2, 3, 3b, 5), and Setup's own bytes are read with the payload's left out, since its
# compressed bytes are arbitrary.
$payloadReader = @'
using System;
using System.IO;
using System.Runtime.InteropServices;
public static class NebulaPayload
{
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
    [DllImport("kernel32", SetLastError = true)] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr FindResourceW(IntPtr module, string name, IntPtr type);
    [DllImport("kernel32", SetLastError = true)] static extern IntPtr LoadResource(IntPtr module, IntPtr info);
    [DllImport("kernel32", SetLastError = true)] static extern IntPtr LockResource(IntPtr data);
    [DllImport("kernel32", SetLastError = true)] static extern uint SizeofResource(IntPtr module, IntPtr info);
    // Writes the PAYLOAD resource to `target`; false when the file has none.
    public static bool Save(string exe, string target)
    {
        IntPtr module = LoadLibraryExW(exe, IntPtr.Zero, 0x00000002 | 0x00000020);  // as a data file, as an image resource
        if (module == IntPtr.Zero) throw new IOException(exe + " could not be opened as a resource file");
        try
        {
            IntPtr info = FindResourceW(module, "PAYLOAD", (IntPtr)10);
            if (info == IntPtr.Zero) return false;
            uint size = SizeofResource(module, info);
            IntPtr data = LockResource(LoadResource(module, info));
            byte[] bytes = new byte[size];
            Marshal.Copy(data, bytes, 0, (int)size);
            File.WriteAllBytes(target, bytes);
            return true;
        }
        finally { FreeLibrary(module); }
    }
    // Where the payload's bytes sit in the file (a resource is stored as it is); -1 when they are not found.
    public static long Locate(string exe, string payload)
    {
        byte[] file = File.ReadAllBytes(exe);
        byte[] p = File.ReadAllBytes(payload);
        if (p.Length == 0 || p.Length > file.Length) return -1;
        int probe = Math.Min(p.Length, 4096);
        int last = file.Length - p.Length;
        for (int at = Array.IndexOf(file, p[0], 0, last + 1); at >= 0; at = at < last ? Array.IndexOf(file, p[0], at + 1, last - at) : -1)
        {
            int i = 1;
            while (i < probe && file[at + i] == p[i]) i++;
            if (i < probe) continue;
            while (i < p.Length && file[at + i] == p[i]) i++;
            if (i == p.Length) return at;
        }
        return -1;
    }
    // A PE file with a CLR header (data directory 14): a .NET assembly.
    public static bool IsDotNet(string path)
    {
        using (var reader = new BinaryReader(File.OpenRead(path)))
        {
            if (reader.BaseStream.Length < 0x40 || reader.ReadUInt16() != 0x5A4D) return false;
            reader.BaseStream.Position = 0x3C;
            long pe = reader.ReadUInt32();
            if (pe + 0x18 + 0x70 + 0x80 > reader.BaseStream.Length) return false;
            reader.BaseStream.Position = pe;
            if (reader.ReadUInt32() != 0x00004550) return false;
            reader.BaseStream.Position = pe + 0x18;
            ushort magic = reader.ReadUInt16();
            long directories = pe + 0x18 + (magic == 0x20B ? 0x70 : 0x60);
            reader.BaseStream.Position = directories + 14 * 8;
            return reader.ReadUInt32() != 0;
        }
    }
}
'@
if (-not ('NebulaPayload' -as [type])) { Add-Type -TypeDefinition $payloadReader }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$payloadFolders = New-Object System.Collections.Generic.List[string]
foreach ($item in @($shipped | Where-Object { $_.Installer -and $_.Full.ToLowerInvariant().EndsWith(".exe") })) {
    $folder = Join-Path ([IO.Path]::GetTempPath()) ("nebula-release-check-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force $folder | Out-Null
    $payloadFolders.Add($folder)
    $zip = Join-Path $folder "payload.zip"
    if (-not [NebulaPayload]::Save($item.Full, $zip)) { $notes.Add("$($item.Label) has no PAYLOAD resource"); continue }
    $at = [NebulaPayload]::Locate($item.Full, $zip)
    if ($at -ge 0) {
        $item | Add-Member -NotePropertyName PayloadAt -NotePropertyValue $at
        $item | Add-Member -NotePropertyName PayloadLength -NotePropertyValue (Get-Item -LiteralPath $zip).Length
    } else {
        $notes.Add("$($item.Label): its payload's bytes were not found in the file, so the file was read with them")
    }
    $unpacked = Join-Path $folder "payload"
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $unpacked)
    $count = 0
    foreach ($entry in Get-ChildItem -LiteralPath $unpacked -Recurse -File) {
        $shipped.Add([pscustomobject]@{ Full = $entry.FullName; Label = "$($item.Label) > " + $entry.FullName.Substring($unpacked.Length + 1).Replace('\', '/'); Installer = $false })
        $count++
    }
    $notes.Add("$($item.Label): the $count files of its payload were checked as shipped files")
}

foreach ($item in $shipped) {
    $extension = [IO.Path]::GetExtension($item.Full).ToLowerInvariant()
    if ($extension -in @(".exe", ".dll")) {
        $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($item.Full)
        if ([string]::IsNullOrWhiteSpace($info.ProductName) -or [string]::IsNullOrWhiteSpace($info.FileVersion) -or
            [string]::IsNullOrWhiteSpace($info.FileDescription)) {
            $failures.Add("$($item.Label): no version information (product, file version and description)")
        }
        # 3b. Native programs only: the .NET builds of 1.0.0 and 1.0.1 were flagged, 1.0.2's native ones were not.
        if ([NebulaPayload]::IsDotNet($item.Full)) { $failures.Add("$($item.Label) is a .NET assembly: Nebula ships native programs only") }
    }
    if ($item.PSObject.Properties['PayloadAt']) {
        $findings = [NebulaReleaseScan]::ScanFile($item.Full, $item.Label, $options, $item.PayloadAt, $item.PayloadLength)
    } else {
        $findings = [NebulaReleaseScan]::ScanFile($item.Full, $item.Label, $options)
    }
    foreach ($finding in $findings) { $failures.Add($finding) }
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

foreach ($folder in $payloadFolders) { Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue }
foreach ($note in $notes) { Write-Output "release_check: note: $note" }
if ($failures.Count -ne 0) {
    Write-Output "release_check: $($failures.Count) failure(s) in $Path"
    $failures | ForEach-Object { Write-Output "  $_" }
    exit 1
}
Write-Output "release_check: $($shipped.Count) files passed (one build, version information, no test names, user paths, e-mail or MAC addresses, signatures, Defender)"
exit 0
