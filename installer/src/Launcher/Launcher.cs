using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            NativeMethods.SetProcessDpiAwarenessContext(new IntPtr(-4));
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                var form = new LauncherForm();
                Application.Run(form);
                return 0;
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
    }

    /// <summary>Play settings and launch, matching the qualified development launcher.</summary>
    internal sealed class LauncherForm : Form
    {
        private readonly string app = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        private readonly string content;
        private readonly ComboBox output = new ComboBox(), aspect = new ComboBox(), internalTarget = new ComboBox();
        private readonly NumericUpDown customWidth = new NumericUpDown(), customHeight = new NumericUpDown();
        private readonly Label outputInfo = new Label(), info = new Label();
        private readonly CheckBox monitor = new CheckBox();
        private readonly Button play = new Button();
        private DisplayPlan plan;
        private bool borderless;

        public LauncherForm()
        {
            // versions\<version>\ -> install root -> content
            string root = Path.GetDirectoryName(Path.GetDirectoryName(app));
            content = Path.Combine(root, "content");
            Text = "Nebula";
            Font = new Font("Segoe UI", 10f);

            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(760, 540);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            AddLabel("Output / window", 20, 20, 300);
            output.Items.Add("Match Display (borderless)");
            output.Items.AddRange(DisplaySettings.Presets);
            Combo(output, 340, 17, true);
            outputInfo.Location = new Point(20, 54);
            outputInfo.Size = new Size(720, 26);
            Controls.Add(outputInfo);
            AddLabel("Gameplay / camera aspect", 20, 92, 300);
            aspect.Items.AddRange(DisplaySettings.Aspects);
            Combo(aspect, 340, 89, false);
            AddLabel("Internal 3D resolution", 20, 132, 300);
            internalTarget.Items.Add("Match Output");
            internalTarget.Items.AddRange(DisplaySettings.Presets);
            internalTarget.Items.Add("Custom");
            Combo(internalTarget, 340, 129, true);
            AddLabel("Custom internal width × height", 20, 172, 300);
            Number(customWidth, 340, 169, 320, 16384, 1920);
            Number(customHeight, 480, 169, 240, 8448, 1080);
            info.Location = new Point(20, 210);
            info.Size = new Size(720, 120);
            Controls.Add(info);
            monitor.Text = "Record diagnostics for this session (off by default; saved to a new folder each time)";
            monitor.Location = new Point(20, 336);
            monitor.Size = new Size(720, 26);
            Controls.Add(monitor);
            AddLabel("Internal rendering changes apply on the next launch. Movies keep their original resolution.", 20, 368, 720);

            var import = Button("Import a GalaxyRecomp save…", 20, 412, 250);
            import.Click += delegate { ImportSave(); };
            var saves = Button("Open save folder", 280, 412, 160);
            saves.Click += delegate { Directory.CreateDirectory(NebulaPaths.Saves); Process.Start("explorer.exe", "\"" + NebulaPaths.Saves + "\""); };
            var sessions = Button("Open diagnostics", 450, 412, 150);
            sessions.Click += delegate { Directory.CreateDirectory(NebulaPaths.Sessions); Process.Start("explorer.exe", "\"" + NebulaPaths.Sessions + "\""); };
            var update = Button("Check for updates", 20, 490, 170);
            update.Click += delegate
            {
                string setup = Path.Combine(root, "Nebula-Setup.exe");
                if (File.Exists(setup)) Process.Start(new ProcessStartInfo(setup, "--update") { WorkingDirectory = root });
            };
            play.Text = "Play";
            play.Location = new Point(620, 486);
            play.Size = new Size(120, 36);
            play.Click += delegate { Play(); };
            Controls.Add(play);
            AcceptButton = play;

            LoadSettings();
            EventHandler changed = delegate { Recalculate(); };
            output.SelectedIndexChanged += changed;
            aspect.TextChanged += changed;
            internalTarget.SelectedIndexChanged += changed;
            customWidth.ValueChanged += changed;
            customHeight.ValueChanged += changed;
            Recalculate();
            UiScale.Apply(this);
        }

        private void AddLabel(string text, int x, int y, int width)
        {
            Controls.Add(new Label { Text = text, Location = new Point(x, y), Size = new Size(width, 26) });
        }

        private void Combo(ComboBox box, int x, int y, bool list)
        {
            box.Location = new Point(x, y);
            box.Width = 400;
            box.DropDownStyle = list ? ComboBoxStyle.DropDownList : ComboBoxStyle.DropDown;
            Controls.Add(box);
        }

        private void Number(NumericUpDown box, int x, int y, int min, int max, int value)
        {
            box.Location = new Point(x, y);
            box.Size = new Size(120, 28);
            box.Minimum = min;
            box.Maximum = max;
            box.Value = value;
            Controls.Add(box);
        }

        private Button Button(string text, int x, int y, int width)
        {
            var button = new Button { Text = text, Location = new Point(x, y), Size = new Size(width, 32) };
            Controls.Add(button);
            return button;
        }

        private void LoadSettings()
        {
            output.SelectedIndex = 0;
            aspect.Text = "16:9";
            internalTarget.SelectedIndex = 0;
            try
            {
                if (!File.Exists(NebulaPaths.SettingsFile)) return;
                var settings = Json.Load(NebulaPaths.SettingsFile);
                int index = output.Items.IndexOf(Json.Str(settings, "output") ?? "");
                if (index >= 0) output.SelectedIndex = index;
                if (Json.Str(settings, "aspect") != null) aspect.Text = Json.Str(settings, "aspect");
                index = internalTarget.Items.IndexOf(Json.Str(settings, "internal") ?? "");
                if (index >= 0) internalTarget.SelectedIndex = index;
                long width = Json.Long(settings, "customWidth"), height = Json.Long(settings, "customHeight");
                if (width >= customWidth.Minimum && width <= customWidth.Maximum) customWidth.Value = width;
                if (height >= customHeight.Minimum && height <= customHeight.Maximum) customHeight.Value = height;
            }
            catch (Exception) { }
        }

        private void SaveSettings()
        {
            Json.Save(NebulaPaths.SettingsFile, new Dictionary<string, object>
            {
                { "output", output.Text }, { "aspect", aspect.Text }, { "internal", internalTarget.Text },
                { "customWidth", (int)customWidth.Value }, { "customHeight", (int)customHeight.Value }
            });
        }

        private void Recalculate()
        {
            customWidth.Enabled = customHeight.Enabled = internalTarget.Text == "Custom";
            try
            {
                int width, height;
                borderless = output.SelectedIndex == 0;
                if (borderless)
                {
                    var screen = Screen.PrimaryScreen.Bounds;
                    width = screen.Width;
                    height = screen.Height;
                }
                else DisplaySettings.OutputPreset(output.Text, aspect.Text, out width, out height);
                outputInfo.Text = string.Format("{0}: {1} × {2} physical pixels.", borderless ? "Borderless display" : "Window", width, height);
                string selection = internalTarget.Text;
                if (selection != "Match Output" && selection != "Custom") selection = DisplaySettings.PresetHeight(selection).ToString();
                plan = DisplaySettings.Resolve(width, height, aspect.Text, selection, (int)customWidth.Value, (int)customHeight.Value);
                info.ForeColor = SystemColors.ControlText;
                info.Text = string.Format(
                    "Active game picture: {0}×{1}, centered at ({2},{3}).\r\nActual 3D scene: {4}×{5} ({6}x internal); frame buffer {7}×{8}.\r\n" +
                    "480 is the original Wii resolution; higher settings render the 3D scene at a genuinely higher resolution (more GPU work).",
                    plan.ContentWidth, plan.ContentHeight, plan.ContentLeft, plan.ContentTop, plan.SceneWidth, plan.SceneHeight,
                    plan.EfbScale, plan.BackingWidth, plan.BackingHeight);
                play.Enabled = true;
            }
            catch (Exception error)
            {
                plan = null;
                info.ForeColor = Color.DarkRed;
                info.Text = error.Message;
                play.Enabled = false;
            }
        }

        private void Play()
        {
            Recalculate();
            if (plan == null) return;
            try
            {
                if (!File.Exists(Path.Combine(content, "game.pak")))
                    throw new FileNotFoundException("The game content is missing. Run Nebula Setup and choose Repair.");
                SaveSettings();
                Directory.CreateDirectory(NebulaPaths.Saves);
                string session = Sessions.Create(monitor.Checked ? "diagnostics" : "launches");
                var start = new ProcessStartInfo(Path.Combine(app, "NebulaRuntime.exe"),
                    ProcessRunner.Quote(content) + " " + ProcessRunner.Quote(Path.Combine(app, "RMGE01_game.dll")));
                start.UseShellExecute = false;
                start.WorkingDirectory = app;
                start.RedirectStandardOutput = true;
                start.RedirectStandardError = true;
                foreach (var name in start.EnvironmentVariables.Keys.Cast<string>().Where(n => n.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)).ToList())
                    start.EnvironmentVariables.Remove(name);
                foreach (var entry in (System.Collections.IList)Json.ParseAny(File.ReadAllText(Path.Combine(app, "runtime-env.json"))))
                {
                    var pair = (Dictionary<string, object>)entry;
                    start.EnvironmentVariables[Json.Str(pair, "name")] = Json.Str(pair, "value");
                }
                var env = start.EnvironmentVariables;
                env["GALAXY_NAND_ROOT"] = FileUtil.FinalPath(NebulaPaths.Saves);
                env["GALAXY_WINDOW_WIDTH"] = plan.OutputWidth.ToString();
                env["GALAXY_WINDOW_HEIGHT"] = plan.OutputHeight.ToString();
                env["GALAXY_FULLSCREEN"] = borderless ? "1" : "0";
                env["GALAXY_EXCLUSIVE_FULLSCREEN"] = "0";
                env["GALAXY_EFB_SCALE"] = plan.EfbScale.ToString();
                env["GALAXY_EXPERIMENTAL_NATIVE_4_3"] = plan.NativeFourThree ? "1" : "0";
                env["GALAXY_EXPERIMENTAL_DYNAMIC_ASPECT"] = "0";
                env["GALAXY_EXPERIMENTAL_ULTRAWIDE_ASPECT"] =
                    plan.NativeFourThree || Math.Abs(plan.AspectRatio - 16.0 / 9.0) < 1e-10 ? "" : plan.Aspect;
                env["GALAXY_NATIVE_THP_VIDEO_DECODE"] = "1";
                env["GALAXY_DUSK_DSP_HLE"] = "0";
                if (monitor.Checked)
                {
                    foreach (var flag in new[] { "GALAXY_TRACE_PRESENT_STATS", "GALAXY_MONITOR_POINTER_LATENCY", "GALAXY_MONITOR_FRAME_TAILS",
                        "GALAXY_MONITOR_DISPLAY_LAYOUT", "GALAXY_TRACE_GX_STALLS", "GALAXY_TRACE_NATIVE_THP_BOUNDARY" })
                        env[flag] = "1";
                    env["GALAXY_TRACE_GX_STALL_US"] = "20000";
                }
                var process = Process.Start(start);
                var sessionInfo = new Dictionary<string, object>
                {
                    { "startUtc", DateTime.UtcNow.ToString("o") }, { "pid", process.Id }, { "monitoring", monitor.Checked },
                    { "content", content }, { "saves", env["GALAXY_NAND_ROOT"] }, { "app", app },
                    { "output", plan.OutputWidth + "x" + plan.OutputHeight }, { "aspect", plan.Aspect },
                    { "efbScale", plan.EfbScale }, { "borderless", borderless },
                    { "install", File.Exists(Path.Combine(app, "install.json")) ? (object)Json.Load(Path.Combine(app, "install.json")) : null }
                };
                Json.Save(Path.Combine(session, "session.json"), sessionInfo);
                Sessions.Capture(process, session, monitor.Checked);
                Close();
            }
            catch (Exception error) { MessageBox.Show(this, error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        private void ImportSave()
        {
            using (var dialog = new FolderBrowserDialog { Description = "Choose the GalaxyRecomp save-data folder to import (it is only read)" })
            {
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                try
                {
                    string message = SaveImport.Import(dialog.SelectedPath);
                    MessageBox.Show(this, message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                catch (Exception error) { MessageBox.Show(this, error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            }
        }
    }

    /// <summary>Per-launch folders under the sessions directory; never reused.</summary>
    internal static class Sessions
    {
        public static string Create(string kind)
        {
            string parent = Path.Combine(NebulaPaths.Sessions, kind);
            Directory.CreateDirectory(parent);
            for (int attempt = 0; attempt < 8; attempt++)
            {
                string path = Path.Combine(parent, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + "-" + Guid.NewGuid().ToString("N"));
                if (!Directory.Exists(path)) { Directory.CreateDirectory(path); return path; }
            }
            throw new IOException("Could not create a new session folder.");
        }

        /// <summary>
        /// Stream the runtime's output to the session folder in a detached
        /// helper thread of a hidden helper process: the launcher window closes
        /// while the game runs. The helper is this launcher started again.
        /// </summary>
        public static void Capture(Process process, string session, bool sampling)
        {
            var stdout = new StreamWriter(Path.Combine(session, "runtime.stdout.log"));
            var stderr = new StreamWriter(Path.Combine(session, "runtime.stderr.log"));
            process.OutputDataReceived += delegate(object s, DataReceivedEventArgs e) { if (e.Data != null) lock (stdout) stdout.WriteLine(e.Data); };
            process.ErrorDataReceived += delegate(object s, DataReceivedEventArgs e) { if (e.Data != null) lock (stderr) stderr.WriteLine(e.Data); };
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            var thread = new Thread(delegate()
            {
                var started = DateTime.UtcNow;
                StreamWriter samples = null;
                if (sampling)
                {
                    samples = new StreamWriter(Path.Combine(session, "process.csv"));
                    samples.WriteLine("elapsedSeconds,cpuSeconds,workingSetBytes");
                }
                while (!process.WaitForExit(1000))
                {
                    if (samples == null) continue;
                    try
                    {
                        process.Refresh();
                        samples.WriteLine(string.Format(System.Globalization.CultureInfo.InvariantCulture, "{0:F3},{1:F6},{2}",
                            (DateTime.UtcNow - started).TotalSeconds, process.TotalProcessorTime.TotalSeconds, process.WorkingSet64));
                        samples.Flush();
                    }
                    catch (InvalidOperationException) { break; }
                }
                process.WaitForExit();
                if (samples != null) samples.Dispose();
                lock (stdout) stdout.Dispose();
                lock (stderr) stderr.Dispose();
                Json.Save(Path.Combine(session, "exit.json"), new Dictionary<string, object>
                {
                    { "exitCode", process.ExitCode }, { "elapsedSeconds", (DateTime.UtcNow - started).TotalSeconds },
                    { "endUtc", DateTime.UtcNow.ToString("o") }
                });
                if (process.ExitCode != 0)
                    MessageBox.Show("The game exited with code " + process.ExitCode + ". Logs: " + session, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                Environment.Exit(0);
            });
            thread.IsBackground = false;
            thread.Start();
        }
    }

    /// <summary>Import a GalaxyRecomp save-data folder, alternate data streams included.</summary>
    internal static class SaveImport
    {
        private const string GameSave = @"title\00010000\524d4745\data\GameData.bin";

        public static string Import(string selected)
        {
            string sourceRoot = FileUtil.FinalPath(Path.GetFullPath(selected));
            if (!File.Exists(Path.Combine(sourceRoot, GameSave)))
            {
                string nested = Path.Combine(sourceRoot, "save-data");
                if (File.Exists(Path.Combine(nested, GameSave))) sourceRoot = FileUtil.FinalPath(nested);
                else throw new InvalidOperationException("This folder is not a GalaxyRecomp save folder (" + GameSave + " was not found).");
            }
            if (Process.GetProcessesByName("NebulaRuntime").Length > 0)
                throw new InvalidOperationException("Close the game before importing a save.");
            string target = NebulaPaths.Saves;
            string staging = target + ".import-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            int count = 0;
            FileUtil.CopyTreeWithStreams(sourceRoot, staging, delegate(string f) { count++; });
            // Every file, directory and stream must match its source before switching.
            var entries = new List<string> { sourceRoot };
            entries.AddRange(Directory.GetDirectories(sourceRoot, "*", SearchOption.AllDirectories));
            entries.AddRange(Directory.GetFiles(sourceRoot, "*", SearchOption.AllDirectories));
            int rootLength = sourceRoot.TrimEnd('\\').Length;
            foreach (var file in entries)
            {
                string relative = file.Length > rootLength ? file.Substring(rootLength + 1) : "";
                var a = FileUtil.StreamDigests(file);
                var b = FileUtil.StreamDigests(Path.Combine(staging, relative));
                if (a.Count != b.Count || a.Any(p => !b.ContainsKey(p.Key) || b[p.Key] != p.Value))
                {
                    FileUtil.DeleteTree(staging);
                    throw new IOException("The imported copy of " + relative + " does not match the original; nothing was changed.");
                }
            }
            string backup = null;
            if (Directory.Exists(target) && Directory.EnumerateFileSystemEntries(target).Any())
            {
                backup = target + "-backup-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss");
                Directory.Move(target, backup);
            }
            else if (Directory.Exists(target)) Directory.Delete(target);
            Directory.Move(staging, target);
            return "Imported " + count + " save files from " + sourceRoot + "." +
                (backup != null ? "\r\nYour previous Nebula saves were kept in " + backup + "." : "");
        }
    }
}
