using System;
using System.IO;

namespace Nebula
{
    /// <summary>
    /// Per-user locations. Everything under an install root (<see cref="InstallLayout"/>) is
    /// replaceable; everything under <see cref="DataRoot"/> (saves, settings,
    /// caches, sessions) survives update, rollback and uninstall.
    /// </summary>
    internal static class NebulaPaths
    {
        public const string ProductName = "Nebula";

        /// <summary>%LOCALAPPDATA%: the variable, as the game reads it (a sandboxed or test run names its own
        /// folder), else the user's folder.</summary>
        public static string LocalAppData
        {
            get
            {
                string named = Environment.GetEnvironmentVariable("LOCALAPPDATA");
                return !string.IsNullOrEmpty(named) && Path.IsPathRooted(named) ? named : UserLocalAppData;
            }
        }

        private static string UserLocalAppData
        {
            get { return Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData); }
        }

        /// <summary>%LOCALAPPDATA% names another folder than the user's own: a sandboxed or test run.</summary>
        public static bool LocalAppDataRedirected
        {
            get
            {
                try
                {
                    return !string.Equals(Path.GetFullPath(LocalAppData).TrimEnd('\\', '/'),
                        Path.GetFullPath(UserLocalAppData).TrimEnd('\\', '/'), StringComparison.OrdinalIgnoreCase);
                }
                catch (ArgumentException) { return true; }
                catch (NotSupportedException) { return true; }
                catch (IOException) { return true; }
            }
        }

        /// <summary>Default install root: %LOCALAPPDATA%\Programs\Nebula.</summary>
        public static string DefaultInstallRoot
        {
            get { return Path.Combine(LocalAppData, "Programs", ProductName); }
        }

        /// <summary>User data root: %LOCALAPPDATA%\Nebula, or a portable copy's data folder. The runtime keeps its
        /// shader cache and config.ini here as well.</summary>
        public static string DataRoot
        {
            get { return portableRoot != null ? PortableData(portableRoot) : Path.Combine(LocalAppData, ProductName); }
        }

        /// <summary>The file that makes an install folder a portable copy (Setup's portable choice): the game, the launcher
        /// and Setup then keep everything in its data folder, and nothing is registered with Windows. The game checks for
        /// it too (platform_paths.h).</summary>
        public const string PortableMarker = "portable.txt";

        private static string portableRoot = FindPortableRoot();

        /// <summary>The portable copy this program belongs to (its folder), or null.</summary>
        public static string PortableRoot { get { return portableRoot; } }

        public static bool IsPortableRoot(string root)
        {
            try { return !string.IsNullOrEmpty(root) && File.Exists(Path.Combine(root, PortableMarker)); }
            catch (ArgumentException) { return false; }
            catch (NotSupportedException) { return false; }
        }

        /// <summary>A portable copy's saves, settings and caches.</summary>
        public static string PortableData(string root) { return Path.Combine(Path.GetFullPath(root), "data"); }

        /// <summary>From here on, the data of the portable copy in <paramref name="root"/> (Setup working on one).</summary>
        public static void UsePortableRoot(string root) { portableRoot = Path.GetFullPath(root); }

        // The program's folder is the copy's (its Setup and its launcher) or versions\<version> inside it.
        private static string FindPortableRoot()
        {
            try
            {
                string here = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\', '/');
                string above = Path.GetDirectoryName(Path.GetDirectoryName(here));
                foreach (string folder in new[] { here, above })
                    if (IsPortableRoot(folder)) return Path.GetFullPath(folder);
            }
            catch (Exception) { }
            return null;
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
        /// <summary>Content of another format, kept for the version that reads it or to cook from.</summary>
        public string KeptContent(long format) { return Path.Combine(root, KeptContentPrefix + format); }
        public const string KeptContentPrefix = "content-format";
        /// <summary>The compiler and the Nebula source of installs that compiled the game; an install removes them.</summary>
        public string Toolchains { get { return Path.Combine(root, "toolchain"); } }
        public string Sources { get { return Path.Combine(root, "source"); } }
        public string Staging { get { return Path.Combine(root, "staging"); } }
        public string SetupCopy { get { return Path.Combine(root, "Nebula-Setup.exe"); } }

        public string VersionDir(string version) { return Path.Combine(Versions, version); }
    }
}
