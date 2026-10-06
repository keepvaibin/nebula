using System;
using System.IO;

namespace Nebula
{
    /// <summary>
    /// Per-user locations. Everything under <see cref="InstallRoot"/> is
    /// replaceable; everything under <see cref="DataRoot"/> (saves, settings,
    /// caches, sessions) survives update, rollback and uninstall.
    /// </summary>
    internal static class NebulaPaths
    {
        public const string ProductName = "Nebula";

        public static string LocalAppData
        {
            get { return Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData); }
        }

        /// <summary>Default install root: %LOCALAPPDATA%\Programs\Nebula.</summary>
        public static string DefaultInstallRoot
        {
            get { return Path.Combine(LocalAppData, "Programs", ProductName); }
        }

        /// <summary>User data root: %LOCALAPPDATA%\Nebula. The runtime keeps its
        /// shader cache and config.ini here as well.</summary>
        public static string DataRoot
        {
            get { return Path.Combine(LocalAppData, ProductName); }
        }

        public static string Saves { get { return Path.Combine(DataRoot, "saves"); } }
        public static string SettingsFile { get { return Path.Combine(DataRoot, "settings.json"); } }
        public static string Sessions { get { return Path.Combine(DataRoot, "sessions"); } }
        public static string SetupLogs { get { return Path.Combine(DataRoot, "setup-logs"); } }
        public static string ShaderCache { get { return Path.Combine(DataRoot, "shadercache"); } }

        /// <summary>Install root recorded by the last successful install.</summary>
        public static string RegisteredInstallRoot
        {
            get
            {
                using (var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\Nebula"))
                {
                    var value = key == null ? null : key.GetValue("InstallRoot") as string;
                    return string.IsNullOrEmpty(value) ? null : value;
                }
            }
        }
    }

    /// <summary>Layout of one install root.</summary>
    internal sealed class InstallLayout
    {
        private readonly string root;

        public InstallLayout(string root) { this.root = Path.GetFullPath(root); }

        public string Root { get { return root; } }
        public string Versions { get { return Path.Combine(root, "versions"); } }
        public string Current { get { return Path.Combine(root, "current.json"); } }
        public string Content { get { return Path.Combine(root, "content"); } }
        public string GameInputs { get { return Path.Combine(root, "game-inputs"); } }
        public string Toolchains { get { return Path.Combine(root, "toolchain"); } }
        public string Sources { get { return Path.Combine(root, "source"); } }
        public string Staging { get { return Path.Combine(root, "staging"); } }
        public string SetupCopy { get { return Path.Combine(root, "Nebula-Setup.exe"); } }

        public string VersionDir(string version) { return Path.Combine(Versions, version); }
    }
}
