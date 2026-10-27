using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using Microsoft.Win32;

namespace Nebula.Setup
{
    /// <summary>Files embedded in Nebula-Setup.exe by the release build.</summary>
    internal static class Payload
    {
        private static Stream Open(string name)
        {
            var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
            if (stream == null) throw new FileNotFoundException("This setup is missing its embedded " + name + ".");
            return stream;
        }

        public static string ReadText(string name)
        {
            using (var reader = new StreamReader(Open(name))) return reader.ReadToEnd();
        }

        public static void Extract(string name, string path)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            using (var input = Open(name))
            using (var output = new FileStream(path, FileMode.Create, FileAccess.Write))
                input.CopyTo(output);
        }
    }

    /// <summary>The currently installed version, read from current.json and its install.json.</summary>
    internal sealed class InstalledInfo
    {
        private Dictionary<string, object> install;
        public string Version { get; private set; }
        public string Previous { get; private set; }
        public string Directory { get; private set; }
        public string ModuleKey { get { return Json.Str(install, "moduleKey"); } }
        public string Commit { get { return Json.Str(install, "commit"); } }
        public string ModuleSourceCommit { get { return Json.Str(install, "moduleSourceCommit") ?? Commit; } }
        public string ModuleIsa { get { return Json.Str(install, "moduleIsa") ?? "SSE2"; } }
        /// <summary>InstallEngine.ModuleGeneration of the installed modules; 1 before it was recorded.</summary>
        public long ModuleGeneration { get { return Math.Max(1L, Json.Long(install, "moduleGeneration")); } }

        public static InstalledInfo Read(InstallLayout layout)
        {
            if (!File.Exists(layout.Current)) return null;
            try
            {
                var current = Json.Load(layout.Current);
                var info = new InstalledInfo();
                info.Version = Json.Str(current, "version");
                info.Previous = Json.Str(current, "previous");
                info.Directory = layout.VersionDir(info.Version);
                string installJson = Path.Combine(info.Directory, "install.json");
                if (!File.Exists(installJson)) return null;
                info.install = Json.Load(installJson);
                if (info.Previous != null && !System.IO.Directory.Exists(layout.VersionDir(info.Previous))) info.Previous = null;
                return info;
            }
            catch (Exception) { return null; }
        }

        public bool FilesIntact(params string[] names)
        {
            var files = Json.Obj(install, "files");
            foreach (var name in names)
            {
                string path = Path.Combine(Directory, name);
                if (files == null || !File.Exists(path) || FileUtil.Sha256File(path) != Json.Str(files, name)) return false;
            }
            return true;
        }

        public void CopyFiles(string target, params string[] names)
        {
            foreach (var name in names) File.Copy(Path.Combine(Directory, name), Path.Combine(target, name), true);
        }

        public IEnumerable<string> RuntimeDllNames()
        {
            var files = Json.Obj(install, "files");
            if (files == null) return Enumerable.Empty<string>();
            return files.Keys.Where(n => n.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)
                && !n.StartsWith("RMGE01_", StringComparison.OrdinalIgnoreCase)).ToList();
        }
    }

    internal static class Running
    {
        /// <summary>The game, the launcher and Nebula Settings.</summary>
        public static readonly string[] Programs = { "NebulaRuntime", "Nebula", "NebulaSettings" };

        /// <summary>The first of <paramref name="names"/> running from inside <paramref name="folder"/>, as "Nebula.exe (pid)"; null when none.</summary>
        public static string Find(string folder, params string[] names)
        {
            string prefix = Path.GetFullPath(folder).TrimEnd('\\') + "\\";
            foreach (var name in names)
            {
                foreach (var process in Process.GetProcessesByName(name))
                {
                    using (process)
                    {
                        string path = null;
                        try { path = process.MainModule.FileName; } catch (Exception) { }
                        if (path != null && path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                            return name + ".exe (" + process.Id + ")";
                    }
                }
            }
            return null;
        }

        /// <summary>Fail with an actionable message while Nebula runs from this install.</summary>
        public static void RequireClosed(string root)
        {
            if (Find(root, Programs) != null)
                throw new InvalidOperationException("Nebula is running. Close the game, the Nebula launcher and Nebula Settings, then try again.");
        }
    }

    /// <summary>Start Menu shortcuts, the Add/Remove Programs entry and uninstall.</summary>
    internal static class Integration
    {
        private const string UninstallKey = @"Software\Microsoft\Windows\CurrentVersion\Uninstall\Nebula";

        public static string StartMenuFolder
        {
            get { return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Programs), "Nebula"); }
        }

        /// <summary>
        /// Shortcuts, the install-root key and the Add/Remove Programs entry. A shortcut that cannot be written (Wine's
        /// Mono has no late-bound COM for WScript.Shell) is returned as a failure, not thrown: Nebula runs without it,
        /// and the registry entries still follow.
        /// </summary>
        public static IList<string> Register(InstallLayout layout, string version, bool desktop)
        {
            string app = layout.VersionDir(version);
            var failures = new List<string>();
            Action<string, string, string, string, string> link = (path, target, arguments, directory, description) =>
            {
                try { Shortcut(path, target, arguments, directory, description); }
                catch (Exception error) { failures.Add(Path.GetFileName(path) + ": " + error.Message); }
            };
            Directory.CreateDirectory(StartMenuFolder);
            link(Path.Combine(StartMenuFolder, "Nebula.lnk"), Path.Combine(app, "Nebula.exe"), "", app, "Play Nebula");
            link(Path.Combine(StartMenuFolder, "Nebula Settings.lnk"), Path.Combine(app, "NebulaSettings.exe"), "", app, "Display, graphics, audio and game files");
            link(Path.Combine(StartMenuFolder, "Update Nebula.lnk"), layout.SetupCopy, "--update", layout.Root, "Check for and install Nebula updates");
            link(Path.Combine(StartMenuFolder, "Nebula Setup.lnk"), layout.SetupCopy, "", layout.Root, "Repair, restore or uninstall Nebula");
            string desktopLink = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Nebula.lnk");
            if (desktop || File.Exists(desktopLink))
                link(desktopLink, Path.Combine(app, "Nebula.exe"), "", app, "Play Nebula");

            using (var key = Registry.CurrentUser.CreateSubKey(@"Software\Nebula")) key.SetValue("InstallRoot", layout.Root);
            using (var key = Registry.CurrentUser.CreateSubKey(UninstallKey))
            {
                key.SetValue("DisplayName", "Nebula");
                key.SetValue("DisplayVersion", version);
                key.SetValue("Publisher", "keepvaibin");
                key.SetValue("InstallLocation", layout.Root);
                key.SetValue("DisplayIcon", Path.Combine(app, "Nebula.exe"));
                key.SetValue("UninstallString", "\"" + layout.SetupCopy + "\" --uninstall");
                key.SetValue("ModifyPath", "\"" + layout.SetupCopy + "\"");
                key.SetValue("URLInfoAbout", "https://github.com/" + BuildInfo.Repository);
                key.SetValue("NoRepair", 0, RegistryValueKind.DWord);
            }
            return failures;
        }

        private static void Shortcut(string path, string target, string arguments, string workingDirectory, string description)
        {
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(shellType);
            object link = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { path });
            Type linkType = link.GetType();
            linkType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, link, new object[] { target });
            linkType.InvokeMember("Arguments", BindingFlags.SetProperty, null, link, new object[] { arguments });
            linkType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, link, new object[] { workingDirectory });
            linkType.InvokeMember("Description", BindingFlags.SetProperty, null, link, new object[] { description });
            linkType.InvokeMember("Save", BindingFlags.InvokeMethod, null, link, null);
        }

        /// <summary>Switch back to the retained previous version.</summary>
        public static string Rollback(InstallLayout layout)
        {
            Running.RequireClosed(layout.Root);
            var current = InstalledInfo.Read(layout);
            if (current == null || current.Previous == null) throw new InvalidOperationException("No previous version is available.");
            Json.Save(layout.Current, new Dictionary<string, object> { { "version", current.Previous }, { "previous", current.Version } });
            Register(layout, current.Previous, false);
            return current.Previous;
        }

        /// <summary>Remove the program. Saves, settings and caches stay unless asked.</summary>
        public static void Uninstall(InstallLayout layout, bool removeUserData)
        {
            Running.RequireClosed(layout.Root);
            // Files first: if one is held, the Add/Remove Programs entry and shortcuts are still there to try again.
            string self = Path.GetFullPath(Assembly.GetExecutingAssembly().Location);
            foreach (var entry in Directory.GetFileSystemEntries(layout.Root))
            {
                if (string.Equals(Path.GetFullPath(entry), self, StringComparison.OrdinalIgnoreCase)) continue;
                try
                {
                    if (Directory.Exists(entry)) FileUtil.DeleteTree(entry); else File.Delete(entry);
                }
                catch (Exception error)
                {
                    if (!(error is IOException) && !(error is UnauthorizedAccessException)) throw;
                    throw new IOException("Could not remove " + entry + ": " + error.Message + FileUtil.HeldAdvice, error);
                }
            }
            if (Directory.Exists(StartMenuFolder)) FileUtil.DeleteTree(StartMenuFolder);
            string desktopLink = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Nebula.lnk");
            if (File.Exists(desktopLink)) File.Delete(desktopLink);
            Registry.CurrentUser.DeleteSubKeyTree(UninstallKey, false);
            Registry.CurrentUser.DeleteSubKeyTree(@"Software\Nebula", false);
            if (removeUserData && Directory.Exists(NebulaPaths.DataRoot)) FileUtil.DeleteTree(NebulaPaths.DataRoot);
            // A running program cannot delete its own file. When this is the Setup copy in the install folder, a copy of
            // it in the temporary folder removes it and the emptied folder once this process has ended, as NSIS and Inno
            // Setup uninstallers do.
            if (self.StartsWith(Path.GetFullPath(layout.Root), StringComparison.OrdinalIgnoreCase))
            {
                string folder = Path.Combine(Path.GetTempPath(), RemoverPrefix + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(folder);
                string remover = Path.Combine(folder, Path.GetFileName(self));
                File.Copy(self, remover);
                var start = new ProcessStartInfo(remover, "--finish-uninstall " + Process.GetCurrentProcess().Id + " --root " +
                                                 CommandLine.Quote(layout.Root));
                start.UseShellExecute = false;
                start.WorkingDirectory = folder;
                Process.Start(start).Dispose();
            }
            else if (Directory.Exists(layout.Root) && Directory.GetFileSystemEntries(layout.Root).Length == 0)
                Directory.Delete(layout.Root);
        }

        private const string RemoverPrefix = "Nebula-Uninstall-";

        /// <summary>
        /// The temporary copy's part of an uninstall (--finish-uninstall PID --root ROOT): once Setup PID has ended, its
        /// copy in ROOT and the emptied ROOT are removed. The temporary copies of earlier uninstalls are removed too; this
        /// one stays in the temporary folder, which Windows clears.
        /// </summary>
        public static void FinishUninstall(int setupProcess, string root)
        {
            try
            {
                using (var setup = Process.GetProcessById(setupProcess)) setup.WaitForExit(60000);
            }
            catch (ArgumentException) { } // already ended
            var layout = new InstallLayout(root);
            for (int attempt = 0; attempt < 20 && File.Exists(layout.SetupCopy); attempt++)
            {
                try { File.Delete(layout.SetupCopy); }
                catch (IOException) { System.Threading.Thread.Sleep(500); }
                catch (UnauthorizedAccessException) { System.Threading.Thread.Sleep(500); }
            }
            try
            {
                if (Directory.Exists(layout.Root) && Directory.GetFileSystemEntries(layout.Root).Length == 0) Directory.Delete(layout.Root);
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            string own = Path.GetDirectoryName(Path.GetFullPath(Assembly.GetExecutingAssembly().Location));
            foreach (var folder in Directory.GetDirectories(Path.GetTempPath(), RemoverPrefix + "*"))
            {
                if (string.Equals(Path.GetFullPath(folder), own, StringComparison.OrdinalIgnoreCase)) continue;
                try { FileUtil.DeleteTree(folder); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
        }
    }

    /// <summary>A published release that is newer than this setup.</summary>
    internal sealed class ReleaseInfo
    {
        public string Version, Tag, Notes, SetupUrl, SignatureUrl;
        public bool Prerelease;
    }

    /// <summary>
    /// Update discovery through the GitHub releases of the Nebula repository.
    /// A downloaded setup runs only when its RSA-SHA256 signature verifies
    /// against the release public key built into this setup.
    /// </summary>
    internal static class Updater
    {
        public static ReleaseInfo FindNewer()
        {
            string json = Downloader.GetString("https://api.github.com/repos/" + BuildInfo.Repository + "/releases?per_page=30",
                "application/vnd.github+json");
            return SelectNewer(json, BuildInfo.Version);
        }

        // Betas require an explicit first download. Separate asset names also
        // keep already-installed preview updaters (which lack this filter) from
        // offering a beta as an ordinary update.
        internal static ReleaseInfo SelectNewer(string json, string installedVersion)
        {
            bool beta = IsBetaVersion(installedVersion);
            string setupName = beta ? "Nebula-Beta-Setup.exe" : "Nebula-Setup.exe";
            ReleaseInfo best = null;
            foreach (var item in (System.Collections.IList)Json.ParseAny(json))
            {
                var release = (Dictionary<string, object>)item;
                if (Json.Bool(release, "draft")) continue;
                string tag = Json.Str(release, "tag_name") ?? "";
                string version = tag.StartsWith("v") ? tag.Substring(1) : tag;
                if (IsBetaVersion(version) != beta) continue;
                if (CompareVersions(version, best != null ? best.Version : installedVersion) <= 0) continue;
                string setup = null, signature = null;
                foreach (var assetItem in Json.List(release, "assets") ?? new object[0])
                {
                    var asset = (Dictionary<string, object>)assetItem;
                    string name = Json.Str(asset, "name");
                    if (name == setupName) setup = Json.Str(asset, "browser_download_url");
                    if (name == setupName + ".sig") signature = Json.Str(asset, "browser_download_url");
                }
                if (setup == null || signature == null) continue;
                best = new ReleaseInfo
                {
                    Version = version, Tag = tag, Notes = Json.Str(release, "body") ?? "",
                    SetupUrl = setup, SignatureUrl = signature, Prerelease = Json.Bool(release, "prerelease")
                };
            }
            return best;
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
            if (File.Exists(exe)) File.Delete(exe);
            Downloader.Fetch(release.SignatureUrl, sig, null, 0, null, cancel);
            Downloader.Fetch(release.SetupUrl, exe, null, 0, onBytes, cancel);
            if (!VerifySignature(exe, sig))
            {
                File.Delete(exe);
                throw new InvalidDataException("The downloaded Nebula " + release.Version + " setup is not signed by the Nebula release key. It was deleted and not run.");
            }
            return exe;
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
