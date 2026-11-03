using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Windows.Forms;

[assembly: System.Reflection.AssemblyTitle("Nebula Setup")]

namespace Nebula.Setup
{
    internal static class Program
    {
        /// <summary>
        /// Nebula-Setup.exe [--update | --apply-update [--launch] | --uninstall | --rollback]
        ///   [--input PATH] [--root PATH] [--desktop-shortcut] [--unattended] [--remove-user-data]
        /// --launch starts the game once the update is installed (the launcher's update offer).
        /// </summary>
        [STAThread]
        private static int Main(string[] args)
        {
            var options = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            for (int i = 0; i < args.Length; i++)
            {
                string arg = args[i];
                if (!arg.StartsWith("--")) continue;
                bool hasValue = i + 1 < args.Length && !args[i + 1].StartsWith("--")
                    && (arg == "--input" || arg == "--root");
                options[arg.Substring(2)] = hasValue ? args[++i] : "";
            }
            NativeMethods.SetProcessDpiAwarenessContext(new IntPtr(-4));
            if (options.ContainsKey("unattended")) return Unattended(options);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new SetupForm(options));
            return 0;
        }

        /// <summary>Scripted runs for testing; progress goes to the setup log.</summary>
        private static int Unattended(Dictionary<string, string> options)
        {
            string root = options.ContainsKey("root") ? options["root"] : (NebulaPaths.RegisteredInstallRoot ?? NebulaPaths.DefaultInstallRoot);
            var layout = new InstallLayout(root);
            try
            {
                if (options.ContainsKey("uninstall")) { Integration.Uninstall(layout, options.ContainsKey("remove-user-data")); return 0; }
                if (options.ContainsKey("rollback")) { Integration.Rollback(layout); return 0; }
                var current = InstalledInfo.Read(layout);
                var request = new InstallRequest
                {
                    Mode = current == null ? InstallMode.Install :
                        (options.ContainsKey("apply-update") ? InstallMode.Update : InstallIntent.ForPackage(current.Version, BuildInfo.Version)),
                    GameInput = options.ContainsKey("input") ? options["input"] : null,
                    InstallRoot = root,
                    DesktopShortcut = options.ContainsKey("desktop-shortcut")
                };
                var engine = new InstallEngine(request, new SilentProgress(), CancellationToken.None);
                engine.Run();
                return 0;
            }
            catch (Exception error)
            {
                try
                {
                    Directory.CreateDirectory(NebulaPaths.SetupLogs);
                    File.AppendAllText(Path.Combine(NebulaPaths.SetupLogs, "unattended-errors.log"),
                        DateTime.UtcNow.ToString("o") + " " + error + Environment.NewLine);
                }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
                return 1;
            }
        }

        private sealed class SilentProgress : IInstallProgress
        {
            public void Report(double overall, string step, double stepFraction) { }
            public void Log(string line) { }
            public bool AskToClose(string message) { return false; }
        }
    }
}
