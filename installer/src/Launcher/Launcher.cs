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

    /// <summary>Play settings and launch.</summary>
    internal sealed class LauncherForm : Form
    {
        private readonly string app = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        private string content;
        private readonly bool portable;
        private string settingsFile { get { return portable ? Path.Combine(app, "portable-settings.json") : NebulaPaths.SettingsFile; } }
        private readonly ComboBox output = new ComboBox(), aspect = new ComboBox(), internalTarget = new ComboBox();
        private readonly NumericUpDown customWidth = new NumericUpDown(), customHeight = new NumericUpDown();
        private readonly Label outputInfo = new Label(), info = new Label();
        private readonly ComboBox recording = new ComboBox();
        private readonly Button play = new Button();
        private DisplayPlan plan;
        private bool borderless;

        public LauncherForm()
        {
            // versions\<version>\ -> install root -> content
            portable = File.Exists(Path.Combine(app, "portable.json"));
            string root = portable ? (NebulaPaths.RegisteredInstallRoot ?? NebulaPaths.DefaultInstallRoot) : Path.GetDirectoryName(Path.GetDirectoryName(app));
            content = Path.Combine(root, "content");
            if (File.Exists(Path.Combine(app, "content", "game.pak"))) content = Path.Combine(app, "content");
            Text = portable ? "Nebula Beta" : "Nebula";
            Font = new Font("Segoe UI", 10f);

            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(760, 564);
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
            AddLabel("Recording", 20, 342, 300);
            recording.Items.AddRange(new[] { "No recording", "Lightweight recording", "Detailed recording" });
            Combo(recording, 340, 339, true);
            recording.SelectedIndex = 0;
            AddLabel("Detailed recording may reduce performance. Saves use the normal Nebula folder.", 20, 378, 720);

            var import = Button("Import a GalaxyRecomp save…", 20, 436, 250);
            import.Click += delegate { ImportSave(); };
            var saves = Button("Open save folder", 280, 436, 160);
            saves.Click += delegate { Directory.CreateDirectory(NebulaPaths.Saves); Process.Start("explorer.exe", "\"" + NebulaPaths.Saves + "\""); };
            var sessions = Button("Open diagnostics", 450, 436, 150);
            sessions.Click += delegate { Directory.CreateDirectory(NebulaPaths.Sessions); Process.Start("explorer.exe", "\"" + NebulaPaths.Sessions + "\""); };
            var chooseContent = Button("Choose game content…", 20, 476, 250);
            chooseContent.Click += delegate { ChooseContent(); };
            var update = Button(portable ? "Beta releases" : "Check for updates", 20, 514, 170);
            update.Click += delegate
            {
                if (portable) { Process.Start("https://github.com/" + BuildInfo.Repository + "/releases"); return; }
                string setup = Path.Combine(root, "Nebula-Setup.exe");
                if (File.Exists(setup)) Process.Start(new ProcessStartInfo(setup, "--update") { WorkingDirectory = root });
            };
            play.Text = "Play";
            play.Location = new Point(620, 510);
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
                string loadPath = File.Exists(settingsFile) ? settingsFile : NebulaPaths.SettingsFile;
                if (!File.Exists(loadPath)) { if(portable) { output.SelectedIndex = 2; internalTarget.SelectedIndex = 1; } return; }
                var settings = Json.Load(loadPath);
                string savedContent = Json.Str(settings, "content");
                if (portable && savedContent != null && File.Exists(Path.Combine(savedContent, "game.pak"))) content = savedContent;
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
            Json.Save(settingsFile, new Dictionary<string, object>
            {
                { "output", output.Text }, { "aspect", aspect.Text }, { "internal", internalTarget.Text },
                { "customWidth", (int)customWidth.Value }, { "customHeight", (int)customHeight.Value }, { "content", content }
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
                    "Game picture: {0}×{1}, centered at ({2},{3}).\r\nInternal scale: {6}x. Scene {4}×{5}; frame buffer {7}×{8}.\r\n" +
                    "Choose 480p internal resolution for lower GPU load.",
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

        private bool ChooseContent()
        {
            using (var dialog = new OpenFileDialog { Title = "Choose game.pak from your Nebula installation", Filter = "Nebula game content|game.pak", CheckFileExists = true })
            {
                if (dialog.ShowDialog(this) != DialogResult.OK) return false;
                content = Path.GetDirectoryName(dialog.FileName);
                SaveSettings();
                return true;
            }
        }

        private void Play()
        {
            Recalculate();
            if (plan == null) return;
            try
            {
                if (!File.Exists(Path.Combine(content, "game.pak")) && !ChooseContent()) return;
                if (Process.GetProcessesByName("NebulaRuntime").Length != 0)
                    throw new InvalidOperationException("Close the running Nebula game before starting another copy.");
                LaunchOptions.ValidatePair(app);
                if (portable) LaunchOptions.PreserveUserData(NebulaPaths.DataRoot, "portable-" + BuildInfo.Version);
                SaveSettings();
                Directory.CreateDirectory(NebulaPaths.Saves);
                var mode = (RecordingMode)recording.SelectedIndex;
                var start = LaunchOptions.Create(app, content, FileUtil.FinalPath(NebulaPaths.Saves), plan, borderless, mode);
                // Normal play has no recording folder, redirected log pipes or sampler.
                if (mode == RecordingMode.None)
                {
                    using (var process = Process.Start(start)) { }
                    Close();
                    return;
                }
                string session = Sessions.Create("diagnostics");
                var launchFiles = new Dictionary<string, object>();
                foreach (string name in new[] { "NebulaRuntime.exe", "RMGE01_game.dll", "RMGE01_home_button.dll",
                    "RMGE01_dsp.dll", "RMGE01_boot_image.bin", "runtime-env.json", "Nebula.exe" })
                    launchFiles[name] = FileUtil.Sha256File(Path.Combine(app, name));
                var launchEnvironment = new Dictionary<string, object>();
                foreach (string name in start.EnvironmentVariables.Keys)
                    if (name.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)) launchEnvironment[name] = start.EnvironmentVariables[name];
                var sessionInfo = new Dictionary<string, object>
                {
                    { "startUtc", DateTime.UtcNow.ToString("o") }, { "pid", null }, { "monitoring", true },
                    { "recording", mode.ToString() }, { "content", content }, { "saves", start.EnvironmentVariables["GALAXY_NAND_ROOT"] },
                    { "app", app }, { "output", plan.OutputWidth + "x" + plan.OutputHeight }, { "aspect", plan.Aspect },
                    { "efbScale", plan.EfbScale }, { "borderless", borderless }, { "detailedRendererTimings", mode == RecordingMode.Detailed },
                    { "launchFileSha256", launchFiles }, { "environment", launchEnvironment }
                };
                Json.Save(Path.Combine(session, "session.json"), sessionInfo);
                var game = Process.Start(start);
                // Start draining redirected output before any further file work.
                Sessions.Capture(game, session, true);
                sessionInfo["pid"] = game.Id;
                try { Json.Save(Path.Combine(session, "session.json"), sessionInfo); }
                catch (IOException) { } // The pre-launch identity and live reader remain.
                catch (UnauthorizedAccessException) { }
                Close();
            }
            catch (Exception error) { MessageBox.Show(this, error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        private void ImportSave()
        {
            using (var dialog = new FolderBrowserDialog { Description = "Choose the GalaxyRecomp save-data folder" })
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
        /// Stream the runtime's output on a foreground worker thread. The launcher
        /// window closes while its recording worker stays alive until game exit.
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
                StreamWriter threads = null;
                if (sampling)
                {
                    samples = new StreamWriter(Path.Combine(session, "process.csv"));
                    samples.WriteLine("elapsedSeconds,cpuSeconds,workingSetBytes");
                    // Per-thread CPU, alongside the process-wide row. `process.csv`
                    // cannot say which thread is spending the time, and that is the
                    // open question: the process runs at a flat ~1.3 cores of 32
                    // while producing 44 frames/s instead of 60, so the CPU seconds
                    // are going somewhere the frame rate does not reflect.
                    //
                    // Do NOT read the regime gap here as "1.41x CPU per produced
                    // frame". That figure is ~96 % a rate effect -- the denominator
                    // (frame rate) moves 35 % while the numerator (CPU rate) moves
                    // 3.6 % -- and it cannot distinguish executing from waiting,
                    // since any slow pipeline raises CPU-per-frame as
                    // 1/frame_rate. The honest signal is the CPU RATE. See
                    // AgentWork/agent-23/25-cpu-per-frame-is-a-rate-effect.md.
                    //
                    // The column set matches tools/perf_session.ps1 so
                    // session_report.ps1's existing threads.csv parser reads it
                    // unchanged.
                    threads = new StreamWriter(Path.Combine(session, "threads.csv"));
                    threads.WriteLine("elapsedSeconds,threadId,cpu100ns,threadState,waitReason,priorityLevel");
                }
                while (!process.WaitForExit(1000))
                {
                    if (samples == null) continue;
                    try
                    {
                        process.Refresh();
                        var elapsed = (DateTime.UtcNow - started).TotalSeconds;
                        samples.WriteLine(string.Format(System.Globalization.CultureInfo.InvariantCulture, "{0:F3},{1:F6},{2}",
                            elapsed, process.TotalProcessorTime.TotalSeconds, process.WorkingSet64));
                        samples.Flush();
                        // A thread can exit between enumerating the collection and
                        // reading its properties, so one bad thread must not lose
                        // the whole sample. Threads that have never run report a
                        // zero TotalProcessorTime, which is a real value here and
                        // not an error.
                        foreach (System.Diagnostics.ProcessThread t in process.Threads)
                        {
                            try
                            {
                                var threadState = t.ThreadState;
                                string waitReason = threadState == System.Diagnostics.ThreadState.Wait ? t.WaitReason.ToString() : "";
                                threads.WriteLine(string.Format(
                                    System.Globalization.CultureInfo.InvariantCulture,
                                    "{0:F3},{1},{2},{3},{4},{5}",
                                    elapsed,
                                    t.Id,
                                    t.TotalProcessorTime.Ticks,
                                    threadState,
                                    waitReason,
                                    t.PriorityLevel));
                            }
                            catch (Exception) { }
                        }
                        threads.Flush();
                    }
                    catch (InvalidOperationException) { break; }
                }
                process.WaitForExit();
                if (samples != null) samples.Dispose();
                if (threads != null) threads.Dispose();
                lock (stdout) stdout.Dispose();
                lock (stderr) stderr.Dispose();
                Json.Save(Path.Combine(session, "exit.json"), new Dictionary<string, object>
                {
                    { "exitCode", process.ExitCode }, { "elapsedSeconds", (DateTime.UtcNow - started).TotalSeconds },
                    { "endUtc", DateTime.UtcNow.ToString("o") }
                });
                string result = "Recording saved to " + session;
                try
                {
                    System.IO.Compression.ZipFile.CreateFromDirectory(session, session + ".zip", System.IO.Compression.CompressionLevel.Optimal, false);
                    result = "Recording saved to " + session + ".zip";
                }
                catch (Exception) { /* Logs remain available if ZIP creation fails. */ }
                if (process.ExitCode != 0) result = "The game exited with code " + process.ExitCode + ".\r\n" + result;
                MessageBox.Show(result, "Nebula recording", MessageBoxButtons.OK, process.ExitCode == 0 ? MessageBoxIcon.Information : MessageBoxIcon.Warning);
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
