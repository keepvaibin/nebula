using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;

namespace Nebula
{
    /// <summary>A published release that is newer than the running version.</summary>
    internal sealed class ReleaseInfo
    {
        public string Version, Tag, Notes, SetupUrl, SignatureUrl;
        public long SetupSize;
        public bool Prerelease;
    }

    /// <summary>
    /// Update discovery through the GitHub releases of the Nebula repository.
    /// A downloaded setup runs only when its RSA-SHA256 signature verifies
    /// against the release public key built into this setup.
    /// </summary>
    internal static class Updater
    {
        /// <summary>The releases list (tests point it at a local server).</summary>
        internal static string ReleasesUrl = "https://api.github.com/repos/" + BuildInfo.Repository + "/releases?per_page=30";

        public static ReleaseInfo FindNewer(int timeoutMilliseconds = 30000)
        {
            string json = Downloader.GetString(ReleasesUrl, "application/vnd.github+json", timeoutMilliseconds);
            return SelectNewer(json, BuildInfo.Version);
        }

        // A beta updates to betas only, a release to releases only. A beta's setup has its own asset name, so an
        // updater without this filter, which looks for Nebula-Setup.exe alone, never offers it.
        internal static ReleaseInfo SelectNewer(string json, string installedVersion)
        {
            bool beta = IsBetaVersion(installedVersion);
            string setupName = beta ? "Nebula-Beta-Setup.exe" : "Nebula-Setup.exe";
            ReleaseInfo best = null;
            var list = Json.ParseAny(json) as System.Collections.IList;
            if (list == null) throw new InvalidDataException("The release list is not a list.");
            foreach (var item in list)
            {
                var release = item as Dictionary<string, object>;
                if (release == null || Json.Bool(release, "draft")) continue;
                string tag = Json.Str(release, "tag_name") ?? "";
                string version = tag.StartsWith("v") ? tag.Substring(1) : tag;
                if (IsBetaVersion(version) != beta) continue;
                if (CompareVersions(version, best != null ? best.Version : installedVersion) <= 0) continue;
                string setup = null, signature = null;
                long setupSize = 0;
                foreach (var assetItem in Json.List(release, "assets") ?? new object[0])
                {
                    var asset = assetItem as Dictionary<string, object>;
                    if (asset == null) continue;
                    string name = Json.Str(asset, "name");
                    if (name == setupName)
                    {
                        setup = Json.Str(asset, "browser_download_url");
                        setupSize = Json.Long(asset, "size");
                    }
                    if (name == setupName + ".sig") signature = Json.Str(asset, "browser_download_url");
                }
                if (!IsHttps(setup) || !IsHttps(signature)) continue;
                best = new ReleaseInfo
                {
                    Version = version, Tag = tag, Notes = Json.Str(release, "body") ?? "",
                    SetupUrl = setup, SignatureUrl = signature, SetupSize = setupSize,
                    Prerelease = Json.Bool(release, "prerelease")
                };
            }
            return best;
        }

        // Downloads come over HTTPS only (tests serve 127.0.0.1 over HTTP).
        private static bool IsHttps(string url)
        {
            Uri uri;
            return url != null && Uri.TryCreate(url, UriKind.Absolute, out uri) &&
                   (uri.Scheme == Uri.UriSchemeHttps || (uri.Scheme == Uri.UriSchemeHttp && uri.IsLoopback));
        }

        internal static bool IsBetaVersion(string version)
        {
            return version != null && version.IndexOf("-beta.", StringComparison.OrdinalIgnoreCase) >= 0;
        }

        /// <summary>Download and authenticate a release setup; returns its path.</summary>
        public static string Download(ReleaseInfo release, string directory, Action<long> onBytes, System.Threading.CancellationToken cancel)
        {
            string folder = Path.Combine(directory, "update-" + release.Version);
            string exe = Path.Combine(folder, "Nebula-Setup.exe");
            string sig = exe + ".sig";
            // The signature is fetched afresh each time (a release published again under its version has a new one);
            // the setup resumes a partial download, so a check that fails downloads it once more from the start.
            for (int attempt = 0; ; attempt++)
            {
                if (File.Exists(exe)) File.Delete(exe);
                if (File.Exists(sig)) File.Delete(sig);
                if (attempt > 0 && File.Exists(exe + ".partial")) File.Delete(exe + ".partial");
                Downloader.Fetch(release.SignatureUrl, sig, null, cancel, MaxSignatureBytes);
                Downloader.Fetch(release.SetupUrl, exe, onBytes, cancel, release.SetupSize > 0 ? release.SetupSize : MaxSetupBytes);
                if (VerifySignature(exe, sig))
                {
                    // The version is inside the signed bytes: an older signed setup published under a newer tag (a
                    // downgrade or a replay) is refused like an unsigned one.
                    if (SetupVersion(exe) == release.Version && CompareVersions(release.Version, BuildInfo.Version) > 0) return exe;
                    File.Delete(exe);
                    throw new InvalidDataException(NebulaText.Of("The downloaded setup is not Nebula {0}. It was deleted and not run.", release.Version));
                }
                File.Delete(exe);
                if (attempt == 1)
                    throw new InvalidDataException(NebulaText.Of("The downloaded Nebula {0} setup is not signed by the Nebula release key. It was deleted and not run.", release.Version));
            }
        }

        private const long MaxSignatureBytes = 64 * 1024;
        // A release's setup without a size in the listing: no Nebula setup is larger.
        private const long MaxSetupBytes = 2L << 30;

        /// <summary>The release version a setup carries (its product version, AssemblyInfo.cs); null without one.</summary>
        internal static string SetupVersion(string file)
        {
            try { return FileVersionInfo.GetVersionInfo(file).ProductVersion; }
            catch (Exception) { return null; }
        }

        public static bool VerifySignature(string file, string signatureFile)
        {
            byte[] signature;
            try { signature = Convert.FromBase64String(File.ReadAllText(signatureFile).Trim()); }
            catch (FormatException) { return false; }
            using (var rsa = new RSACryptoServiceProvider())
            {
                rsa.PersistKeyInCsp = false;
                rsa.FromXmlString(BuildInfo.ReleasePublicKeyXml);
                using (var stream = File.OpenRead(file))
                using (var sha = SHA256.Create())
                    return rsa.VerifyHash(sha.ComputeHash(stream), CryptoConfig.MapNameToOID("SHA256"), signature);
            }
        }

        /// <summary>Compare MAJOR.MINOR.PATCH[-preview.N]; a release sorts after its previews.</summary>
        public static int CompareVersions(string a, string b)
        {
            long[] x = Key(a), y = Key(b);
            for (int i = 0; i < x.Length; i++) if (x[i] != y[i]) return x[i].CompareTo(y[i]);
            return 0;
        }

        private static long[] Key(string version)
        {
            var key = new long[5];
            if (string.IsNullOrEmpty(version)) return key;
            string[] parts = version.Split(new[] { '-' }, 2);
            string[] numbers = parts[0].Split('.');
            for (int i = 0; i < 3 && i < numbers.Length; i++) long.TryParse(numbers[i], out key[i]);
            key[3] = parts.Length == 1 ? 1 : 0;
            if (parts.Length == 2)
            {
                int dot = parts[1].LastIndexOf('.');
                if (dot >= 0) long.TryParse(parts[1].Substring(dot + 1), out key[4]);
            }
            return key;
        }
    }
}
