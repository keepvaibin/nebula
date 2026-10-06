using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Setup
{
    internal static class Program
    {
        /// <summary>
        /// Nebula-Setup.exe [--update | --apply-update | --uninstall | --rollback]
        ///   [--input PATH] [--root PATH] [--source ZIP|DIR] [--jobs N] [--desktop-shortcut]
        ///   [--unattended] [--remove-user-data] [--accept-build-tools-license]
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
                    && (arg == "--input" || arg == "--root" || arg == "--source" || arg == "--jobs");
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
                if (!options.ContainsKey("accept-build-tools-license"))
                    throw new InvalidOperationException("Unattended installs must pass --accept-build-tools-license.");
                var request = new InstallRequest
                {
                    Mode = options.ContainsKey("apply-update") ? InstallMode.Update : (InstalledInfo.Read(layout) != null ? InstallMode.Repair : InstallMode.Install),
                    GameInput = options.ContainsKey("input") ? options["input"] : null,
                    InstallRoot = root,
                    SourceOverride = options.ContainsKey("source") ? options["source"] : null,
                    DesktopShortcut = options.ContainsKey("desktop-shortcut"),
                    CompileJobs = options.ContainsKey("jobs") ? int.Parse(options["jobs"]) : 0
                };
                var engine = new InstallEngine(request, new SilentProgress(), CancellationToken.None);
                engine.Run();
                return 0;
            }
            catch (Exception error)
            {
                Directory.CreateDirectory(NebulaPaths.SetupLogs);
                File.AppendAllText(Path.Combine(NebulaPaths.SetupLogs, "unattended-errors.log"),
                    DateTime.UtcNow.ToString("o") + " " + error + Environment.NewLine);
                return 1;
            }
        }

        private sealed class SilentProgress : IInstallProgress
        {
            public void Report(double overall, string step, double stepFraction) { }
            public void Log(string line) { }
        }
    }
}
