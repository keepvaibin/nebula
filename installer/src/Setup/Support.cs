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
        /// <summary>Fail with an actionable message while Nebula runs from this install.</summary>
        public static void RequireClosed(string root)
        {
            string prefix = Path.GetFullPath(root).TrimEnd('\\') + "\\";
            foreach (var name in new[] { "NebulaRuntime", "Nebula" })
            {
                foreach (var process in Process.GetProcessesByName(name))
                {
                    string path = null;
                    try { path = process.MainModule.FileName; } catch (Exception) { }
                    if (path != null && path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException("Nebula is running. Close the game and the Nebula launcher, then try again.");
                }
            }
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

        public static void Register(InstallLayout layout, string version, bool desktop)
        {
            string app = layout.VersionDir(version);
            Directory.CreateDirectory(StartMenuFolder);
            Shortcut(Path.Combine(StartMenuFolder, "Nebula.lnk"), Path.Combine(app, "Nebula.exe"), "", app, "Play Nebula");
            Shortcut(Path.Combine(StartMenuFolder, "Update Nebula.lnk"), layout.SetupCopy, "--update", layout.Root, "Check for and install Nebula updates");
            Shortcut(Path.Combine(StartMenuFolder, "Nebula Setup.lnk"), layout.SetupCopy, "", layout.Root, "Repair, restore or uninstall Nebula");
            string desktopLink = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Nebula.lnk");
            if (desktop || File.Exists(desktopLink))
                Shortcut(desktopLink, Path.Combine(app, "Nebula.exe"), "", app, "Play Nebula");

            using (var key = Registry.CurrentUser.CreateSubKey(@"Software\Nebula")) key.SetValue("InstallRoot", layout.Root);
            using (var key = Registry.CurrentUser.CreateSubKey(UninstallKey))
            {
                key.SetValue("DisplayName", "Nebula (development preview)");
                key.SetValue("DisplayVersion", version);
                key.SetValue("Publisher", "Nebula contributors");
                key.SetValue("InstallLocation", layout.Root);
                key.SetValue("DisplayIcon", Path.Combine(app, "Nebula.exe"));
                key.SetValue("UninstallString", "\"" + layout.SetupCopy + "\" --uninstall");
                key.SetValue("ModifyPath", "\"" + layout.SetupCopy + "\"");
                key.SetValue("URLInfoAbout", "https://github.com/" + BuildInfo.Repository);
                key.SetValue("NoRepair", 0, RegistryValueKind.DWord);
            }
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
            if (Directory.Exists(StartMenuFolder)) FileUtil.DeleteTree(StartMenuFolder);
            string desktopLink = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Nebula.lnk");
            if (File.Exists(desktopLink)) File.Delete(desktopLink);
            Registry.CurrentUser.DeleteSubKeyTree(UninstallKey, false);
            Registry.CurrentUser.DeleteSubKeyTree(@"Software\Nebula", false);
            string self = Path.GetFullPath(Assembly.GetExecutingAssembly().Location);
            foreach (var entry in Directory.GetFileSystemEntries(layout.Root))
            {
                if (string.Equals(Path.GetFullPath(entry), self, StringComparison.OrdinalIgnoreCase)) continue;
                if (Directory.Exists(entry)) FileUtil.DeleteTree(entry); else File.Delete(entry);
            }
            if (removeUserData && Directory.Exists(NebulaPaths.DataRoot)) FileUtil.DeleteTree(NebulaPaths.DataRoot);
            // The running setup copy deletes itself and the folder after exit.
            if (self.StartsWith(Path.GetFullPath(layout.Root), StringComparison.OrdinalIgnoreCase))
            {
                string command = "/c ping 127.0.0.1 -n 3 > nul & del /f /q \"" + self + "\" & rmdir \"" + layout.Root + "\"";
                var start = new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "cmd.exe"), command);
                start.CreateNoWindow = true;
                start.UseShellExecute = false;
                Process.Start(start);
            }
            else if (Directory.Exists(layout.Root) && Directory.GetFileSystemEntries(layout.Root).Length == 0)
                Directory.Delete(layout.Root);
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
            ReleaseInfo best = null;
            foreach (var item in (System.Collections.IList)Json.ParseAny(json))
            {
                var release = (Dictionary<string, object>)item;
                if (Json.Bool(release, "draft")) continue;
                string tag = Json.Str(release, "tag_name") ?? "";
                string version = tag.StartsWith("v") ? tag.Substring(1) : tag;
                if (CompareVersions(version, best != null ? best.Version : BuildInfo.Version) <= 0) continue;
                string setup = null, signature = null;
                foreach (var assetItem in Json.List(release, "assets") ?? new object[0])
                {
                    var asset = (Dictionary<string, object>)assetItem;
                    string name = Json.Str(asset, "name");
                    if (name == "Nebula-Setup.exe") setup = Json.Str(asset, "browser_download_url");
                    if (name == "Nebula-Setup.exe.sig") signature = Json.Str(asset, "browser_download_url");
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
