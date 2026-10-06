using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Threading;

namespace Nebula.Setup
{
    /// <summary>
    /// The exact Nebula source revision this setup was built from. It is
    /// downloaded from GitHub and accepted only when every file matches the
    /// SHA-256 manifest embedded in the setup executable.
    /// </summary>
    internal sealed class SourceTree
    {
        private readonly Dictionary<string, object> manifest;
        private readonly Dictionary<string, object> files;

        public SourceTree(Dictionary<string, object> manifest)
        {
            this.manifest = manifest;
            files = Json.Obj(manifest, "files");
            if (string.IsNullOrEmpty(Commit) || files == null || files.Count == 0)
                throw new InvalidDataException("The setup's source manifest is incomplete.");
        }

        public string Commit { get { return Json.Str(manifest, "commit"); } }
        public string Repository { get { return Json.Str(manifest, "repository"); } }
        public string TreeSha256 { get { return Json.Str(manifest, "treeSha256"); } }

        public string ArchiveUrl
        {
            get { return "https://codeload.github.com/" + Repository + "/zip/" + Commit; }
        }

        /// <summary>SHA-256 over a subset of the manifest, used for compatibility keys.</summary>
        public string SubsetSha256(params string[] prefixes)
        {
            var keys = new List<string>(files.Keys);
            keys.Sort(StringComparer.Ordinal);
            var text = new System.Text.StringBuilder();
            foreach (var key in keys)
            {
                foreach (var prefix in prefixes)
                {
                    if (key.StartsWith(prefix, StringComparison.Ordinal))
                    {
                        text.Append(key).Append('\0').Append(files[key]).Append('\n');
                        break;
                    }
                }
            }
            return FileUtil.Sha256Text(text.ToString());
        }

        /// <summary>
        /// Make the verified source available under <paramref name="sourcesDirectory"/>.
        /// <paramref name="overridePath"/> (a zip or folder) replaces the download for
        /// offline and pre-release testing; it is verified the same way.
        /// </summary>
        public string Ensure(string sourcesDirectory, string overridePath, Action<string, double> progress,
            Action<string> log, CancellationToken cancel)
        {
            string target = Path.Combine(sourcesDirectory, Commit.Substring(0, Math.Min(12, Commit.Length)));
            if (Directory.Exists(target))
            {
                try { Verify(target); log("Using the verified source at " + target + "."); return target; }
                catch (InvalidDataException) { FileUtil.DeleteTree(target); }
            }
            string staging = target + ".partial";
            if (Directory.Exists(staging)) FileUtil.DeleteTree(staging);
            if (!string.IsNullOrEmpty(overridePath) && Directory.Exists(overridePath))
            {
                progress("Copying the Nebula source", 0);
                foreach (var key in files.Keys)
                {
                    string destination = Toolchain.SafeJoin(staging, key);
                    Directory.CreateDirectory(Path.GetDirectoryName(destination));
                    File.Copy(Path.Combine(overridePath, key.Replace('/', '\\')), destination);
                }
            }
            else
            {
                string zip = overridePath;
                if (string.IsNullOrEmpty(zip))
                {
                    zip = Path.Combine(sourcesDirectory, "nebula-" + Commit + ".zip");
                    Downloader.Fetch(ArchiveUrl, zip, null, 0,
                        delegate(long bytes) { progress("Downloading the Nebula source (" + FileUtil.FormatBytes(bytes) + ")", 0); },
                        cancel);
                }
                progress("Unpacking the Nebula source", 0);
                using (var archive = ZipFile.OpenRead(zip))
                {
                    foreach (var entry in archive.Entries)
                    {
                        string name = entry.FullName.Replace('\\', '/');
                        int slash = name.IndexOf('/');
                        if (slash < 0 || name.EndsWith("/")) continue;
                        string relative = name.Substring(slash + 1);
                        if (!files.ContainsKey(relative))
                            throw new InvalidDataException("The downloaded source contains an unexpected file: " + relative);
                        string destination = Toolchain.SafeJoin(staging, relative);
                        Directory.CreateDirectory(Path.GetDirectoryName(destination));
                        entry.ExtractToFile(destination, false);
                    }
                }
                if (string.IsNullOrEmpty(overridePath)) File.Delete(zip);
            }
            Verify(staging);
            Directory.Move(staging, target);
            log("Verified " + files.Count + " source files of revision " + Commit + ".");
            return target;
        }

        private void Verify(string root)
        {
            foreach (var pair in files)
            {
                string path = Path.Combine(root, pair.Key.Replace('/', '\\'));
                if (!File.Exists(path)) throw new InvalidDataException("The source is missing " + pair.Key + ".");
                if (FileUtil.Sha256File(path) != Convert.ToString(pair.Value))
                    throw new InvalidDataException("The source file " + pair.Key + " does not match this release.");
            }
            int count = Directory.GetFiles(root, "*", SearchOption.AllDirectories).Length;
            if (count != files.Count) throw new InvalidDataException("The source folder contains files that are not part of this release.");
        }
    }
}
