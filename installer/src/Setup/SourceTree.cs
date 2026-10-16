using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Threading;

namespace Nebula.Setup
{
    /// <summary>
    /// The exact Nebula source revision this setup was built from. It comes from
    /// the source zip beside the setup, the release's copy of that zip or GitHub's
    /// archive of the commit, and is accepted only when every file matches the
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
            SetupDirectory = AppDomain.CurrentDomain.BaseDirectory;
            DownloadUrls = new[] { ReleaseArchiveUrl, ArchiveUrl };
        }

        public string Commit { get { return Json.Str(manifest, "commit"); } }
        public string Repository { get { return Json.Str(manifest, "repository"); } }
        public string Version { get { return Json.Str(manifest, "version"); } }
        public string TreeSha256 { get { return Json.Str(manifest, "treeSha256"); } }
        private string ShortCommit { get { return Commit.Substring(0, Math.Min(12, Commit.Length)); } }

        /// <summary>The zip build-release.ps1 writes beside Nebula-Setup.exe; the release carries it too.</summary>
        public string ArchiveName { get { return "nebula-source-" + ShortCommit + ".zip"; } }

        public string ArchiveUrl
        {
            get { return "https://codeload.github.com/" + Repository + "/zip/" + Commit; }
        }

        public string ReleaseArchiveUrl
        {
            get { return "https://github.com/" + Repository + "/releases/download/v" + Version + "/" + ArchiveName; }
        }

        /// <summary>Where <see cref="ArchiveName"/> is looked for before downloading: the setup's folder.</summary>
        public string SetupDirectory { get; set; }

        /// <summary>Tried in order when no verified source is at hand.</summary>
        public string[] DownloadUrls { get; set; }

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
        /// <paramref name="overridePath"/> (a zip or folder) replaces every other source;
        /// otherwise <see cref="ArchiveName"/> beside the setup is used when it verifies,
        /// and then each of <see cref="DownloadUrls"/> in turn.
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
            string staging = target + ".partial";
            if (!string.IsNullOrEmpty(overridePath))
            {
                // An explicit --source is used or rejected, never replaced by a download.
                Unpack(overridePath, staging, progress);
                Verify(staging);
            }
            else if (!TryArchiveBesideSetup(staging, progress, log))
            {
                Download(sourcesDirectory, staging, progress, log, cancel);
            }
            FileUtil.MoveDirectory(staging, target, log);
            log("Verified " + files.Count + " source files of revision " + Commit + ".");
            return target;
        }

        private bool TryArchiveBesideSetup(string staging, Action<string, double> progress, Action<string> log)
        {
            if (string.IsNullOrEmpty(SetupDirectory)) return false;
            string zip = Path.Combine(SetupDirectory, ArchiveName);
            if (!File.Exists(zip)) return false;
            try
            {
                Unpack(zip, staging, progress);
                Verify(staging);
                log("Using the source archive " + zip + ".");
                return true;
            }
            catch (InvalidDataException error) { log(zip + " is not this release's source (" + error.Message + ")."); }
            catch (IOException error) { log("Could not read " + zip + ": " + error.Message); }
            catch (UnauthorizedAccessException error) { log("Could not read " + zip + ": " + error.Message); }
            return false;
        }

        private void Download(string sourcesDirectory, string staging, Action<string, double> progress,
            Action<string> log, CancellationToken cancel)
        {
            Directory.CreateDirectory(sourcesDirectory);
            string lastFailure = null;
            for (int i = 0; i < DownloadUrls.Length; i++)
            {
                // One file per address: a partial transfer resumes only from the address it came from.
                string zip = Path.Combine(sourcesDirectory, "nebula-" + Commit + "-" + i + ".zip");
                try
                {
                    log("Downloading the Nebula source from " + DownloadUrls[i]);
                    Downloader.Fetch(DownloadUrls[i], zip, null, 0,
                        delegate(long bytes) { progress("Downloading the Nebula source (" + FileUtil.FormatBytes(bytes) + ")", 0); },
                        cancel);
                    Unpack(zip, staging, progress);
                    Verify(staging);
                    File.Delete(zip);
                    return;
                }
                catch (System.Net.WebException error) { lastFailure = error.Message; log("Not available: " + error.Message); }
                catch (InvalidDataException error) { lastFailure = error.Message; log("Rejected: " + error.Message); }
                catch (IOException error) { lastFailure = error.Message; log("Failed: " + error.Message); }
                cancel.ThrowIfCancellationRequested();
                try { File.Delete(zip); } catch (IOException) { }
            }
            throw new InvalidOperationException(
                "Setup could not get the Nebula " + Version + " source (revision " + ShortCommit + ") from GitHub" +
                (lastFailure != null ? ": " + lastFailure : ".") + "\r\n\r\n" +
                "Check the internet connection and run Setup again. Without internet access, download " + ArchiveName +
                " from the Nebula " + Version + " release and put it next to Nebula-Setup.exe.");
        }

        /// <summary>Copies a source folder, or unpacks a source zip, into an empty <paramref name="staging"/>.</summary>
        private void Unpack(string path, string staging, Action<string, double> progress)
        {
            if (Directory.Exists(staging)) FileUtil.DeleteTree(staging);
            if (Directory.Exists(path))
            {
                progress("Copying the Nebula source", 0);
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
            {
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
