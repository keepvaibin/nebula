using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using Microsoft.Win32;

namespace Nebula.Setup
{
    /// <summary>Files build-native-setup.ps1 embeds in Nebula-Setup.exe.</summary>
    internal static class Payload
    {
        private static Stream Open(string name)
        {
            var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(name);
            if (stream == null) throw new FileNotFoundException("This setup is missing its embedded " + name + ".");
            return stream;
        }

        public static void Extract(string name, string path)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            using (var input = Open(name))
            using (var output = new FileStream(path, FileMode.Create, FileAccess.Write))
                input.CopyTo(output);
        }
    }

    /// <summary>The currently installed version, read from current.json; its folder must hold a readable install.json.</summary>
    internal sealed class InstalledInfo
    {
        public string Version { get; private set; }
        public string Previous { get; private set; }

        public static InstalledInfo Read(InstallLayout layout)
        {
            if (!File.Exists(layout.Current)) return null;
            try
            {
                var current = Json.Load(layout.Current);
                var info = new InstalledInfo();
                info.Version = Json.Str(current, "version");
                info.Previous = Json.Str(current, "previous");
                string installJson = Path.Combine(layout.VersionDir(info.Version), "install.json");
                if (!File.Exists(installJson)) return null;
                Json.Load(installJson);
                if (info.Previous != null && !Directory.Exists(layout.VersionDir(info.Previous))) info.Previous = null;
                return info;
            }
            catch (Exception) { return null; }
        }
    }

    internal static class Running
    {
        /// <summary>The game (recompiled or native), the launcher and Nebula Settings.</summary>
        public static readonly string[] Programs = { "NebulaRuntime", "NebulaNative", "Nebula", "NebulaSettings" };

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
            // Automated tests (NEBULA_DATA_ROOT) install into their own folders: the user's shortcuts and registry stay.
            if (!string.IsNullOrEmpty(Environment.GetEnvironmentVariable("NEBULA_DATA_ROOT"))) return failures;
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
                    string holders = Directory.Exists(entry) ? FileUtil.Holders(Directory.GetFiles(entry, "*", SearchOption.AllDirectories))
                        : FileUtil.Holders(new[] { entry });
                    throw new IOException("Could not remove " + entry + ": " + error.Message +
                        (holders.Length != 0 ? " In use by: " + holders + "." : "") + " Close it and uninstall again.", error);
                }
            }
            if (Directory.Exists(StartMenuFolder)) FileUtil.DeleteTree(StartMenuFolder);
            string desktopLink = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Nebula.lnk");
            if (File.Exists(desktopLink)) File.Delete(desktopLink);
            Registry.CurrentUser.DeleteSubKeyTree(UninstallKey, false);
            Registry.CurrentUser.DeleteSubKeyTree(@"Software\Nebula", false);
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
}
