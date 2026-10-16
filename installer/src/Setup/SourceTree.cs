using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Reflection;
using System.Threading;

namespace Nebula.Setup
{
    /// <summary>
    /// The exact Nebula source revision this setup was built from; the game modules are compiled against
    /// it. A release setup carries it (build-release.ps1 embeds it). A setup without a matching copy
    /// downloads GitHub's archive of the commit instead. Either is accepted only when every file matches the
    /// SHA-256 manifest embedded in the setup executable.
    /// </summary>
    internal sealed class SourceTree
    {
        /// <summary>The resource name build-release.ps1 embeds the source zip under.</summary>
        public const string EmbeddedArchiveName = "nebula-source.zip";

        private readonly Dictionary<string, object> manifest;
        private readonly Dictionary<string, object> files;

        public SourceTree(Dictionary<string, object> manifest)
        {
            this.manifest = manifest;
            files = Json.Obj(manifest, "files");
            if (string.IsNullOrEmpty(Commit) || files == null || files.Count == 0)
                throw new InvalidDataException("The setup's source manifest is incomplete.");
            EmbeddedArchive = delegate { return Assembly.GetExecutingAssembly().GetManifestResourceStream(EmbeddedArchiveName); };
            DownloadUrl = "https://codeload.github.com/" + Repository + "/zip/" + Commit;
        }

        public string Commit { get { return Json.Str(manifest, "commit"); } }
        public string Repository { get { return Json.Str(manifest, "repository"); } }
        public string Version { get { return Json.Str(manifest, "version"); } }
        public string TreeSha256 { get { return Json.Str(manifest, "treeSha256"); } }
        private string ShortCommit { get { return Commit.Substring(0, Math.Min(12, Commit.Length)); } }

        /// <summary>Opens the embedded source zip; null in a setup without one (compiled by hand).</summary>
        public Func<Stream> EmbeddedArchive { get; set; }

        /// <summary>GitHub's archive of the commit, for a setup that carries no matching source.</summary>
        public string DownloadUrl { get; set; }

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
        /// Make the verified source available under <paramref name="sourcesDirectory"/>: a copy verified
        /// earlier, else <paramref name="overridePath"/> (--source: a zip or folder, used or rejected), else
        /// the embedded copy, else the download.
        /// </summary>
        public string Ensure(string sourcesDirectory, string overridePath, Action<string, double> progress,
            Action<string> log, CancellationToken cancel)
        {
            string target = Path.Combine(sourcesDirectory, ShortCommit);
            if (Directory.Exists(target))
            {
                try { Verify(target); log("Using the verified source at " + target + "."); return target; }
                catch (InvalidDataException) { FileUtil.DeleteTree(target); }
            }
            Directory.CreateDirectory(sourcesDirectory);
            string staging = target + ".partial";
            if (!string.IsNullOrEmpty(overridePath))
            {
                Unpack(overridePath, staging, progress);
                Verify(staging);
                log("Using the source at " + overridePath + ".");
            }
            else if (!TryEmbedded(staging, progress, log))
            {
                Download(sourcesDirectory, staging, progress, log, cancel);
            }
            FileUtil.MoveDirectory(staging, target, log);
            log("Verified " + files.Count + " source files of revision " + Commit + ".");
            return target;
        }

        private bool TryEmbedded(string staging, Action<string, double> progress, Action<string> log)
        {
            Stream embedded = EmbeddedArchive();
            if (embedded == null) return false;
            try
            {
                progress("Unpacking the Nebula source", 0);
                using (var archive = new ZipArchive(embedded, ZipArchiveMode.Read))
                    Extract(archive, staging);
                Verify(staging);
                log("Using the source embedded in Setup.");
                return true;
            }
            catch (InvalidDataException error) { log("The source embedded in Setup does not match this release (" + error.Message + ")."); }
            catch (IOException error) { log("Could not unpack the source embedded in Setup: " + error.Message); }
            catch (UnauthorizedAccessException error) { log("Could not unpack the source embedded in Setup: " + error.Message); }
            return false;
        }

        private void Download(string sourcesDirectory, string staging, Action<string, double> progress,
            Action<string> log, CancellationToken cancel)
        {
            string zip = Path.Combine(sourcesDirectory, "nebula-" + Commit + ".zip");
            log("Downloading the Nebula source from " + DownloadUrl);
            try
            {
                Downloader.Fetch(DownloadUrl, zip, null, 0,
                    delegate(long bytes) { progress("Downloading the Nebula source (" + FileUtil.FormatBytes(bytes) + ")", 0); },
                    cancel);
                Unpack(zip, staging, progress);
                Verify(staging);
            }
            catch (WebException error) { throw Unavailable(error.Message); }
            catch (InvalidDataException error) { DeleteQuietly(zip); throw Unavailable(error.Message); }
            DeleteQuietly(zip);
        }

        private InvalidOperationException Unavailable(string reason)
        {
            return new InvalidOperationException("Setup could not get the Nebula " + Version + " source (revision " +
                ShortCommit + ") from GitHub: " + reason + "\r\n\r\nCheck the internet connection and run Setup again.");
        }

        // A scanner may still be reading a fresh download; a leftover costs only space.
        private static void DeleteQuietly(string path)
        {
            try { File.Delete(path); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        }

        /// <summary>Copies a source folder, or unpacks a source zip, into an empty <paramref name="staging"/>.</summary>
        private void Unpack(string path, string staging, Action<string, double> progress)
        {
            if (Directory.Exists(path))
            {
                progress("Copying the Nebula source", 0);
                if (Directory.Exists(staging)) FileUtil.DeleteTree(staging);
                foreach (var key in files.Keys)
                {
                    string source = Path.Combine(path, key.Replace('/', '\\'));
                    if (!File.Exists(source)) throw new InvalidDataException("The source is missing " + key + ".");
                    string destination = Toolchain.SafeJoin(staging, key);
                    Directory.CreateDirectory(Path.GetDirectoryName(destination));
                    File.Copy(source, destination);
                }
                return;
            }
            progress("Unpacking the Nebula source", 0);
            using (var archive = ZipFile.OpenRead(path))
                Extract(archive, staging);
        }

        /// <summary>The archive's files below its top folder (git archive --prefix, GitHub's archive) into an empty <paramref name="staging"/>.</summary>
        private void Extract(ZipArchive archive, string staging)
        {
            if (Directory.Exists(staging)) FileUtil.DeleteTree(staging);
            foreach (var entry in archive.Entries)
            {
                string name = entry.FullName.Replace('\\', '/');
                int slash = name.IndexOf('/');
                if (slash < 0 || name.EndsWith("/")) continue;
                string relative = name.Substring(slash + 1);
                if (!files.ContainsKey(relative))
                    throw new InvalidDataException("The source archive contains an unexpected file: " + relative);
                string destination = Toolchain.SafeJoin(staging, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(destination));
                entry.ExtractToFile(destination, false);
            }
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
