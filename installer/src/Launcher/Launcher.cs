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
        private readonly ComboBox antiAliasing = new ComboBox(), aaQuality = new ComboBox(), upscaler = new ComboBox(), textureFilter = new ComboBox(), lighting = new ComboBox();
        private readonly NumericUpDown sharpness = new NumericUpDown();
        private readonly CheckBox mipmaps = new CheckBox();
        private readonly NumericUpDown[] volumes = new NumericUpDown[AudioSettings.Names.Length];
        private readonly Button play = new Button();
        private GraphicsSettings graphics = new GraphicsSettings();
        private AudioSettings audio = new AudioSettings();
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
            ClientSize = new Size(760, 524);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var tabs = new TabControl { Location = new Point(12, 12), Size = new Size(736, 420) };
            var display = new TabPage("Display");
            var image = new TabPage("Graphics");
            var sound = new TabPage("Audio");
            var diagnostics = new TabPage("Diagnostics");
            tabs.TabPages.AddRange(new[] { display, image, sound, diagnostics });
            Controls.Add(tabs);

            AddLabel(display, "Output / window", 12, 16, 300);
            output.Items.Add("Match Display (borderless)");
            output.Items.AddRange(DisplaySettings.Presets);
            Combo(display, output, 320, 13, true);
            outputInfo.Location = new Point(12, 50);
            outputInfo.Size = new Size(700, 26);
            display.Controls.Add(outputInfo);
            AddLabel(display, "Gameplay / camera aspect", 12, 88, 300);
            aspect.Items.AddRange(DisplaySettings.Aspects);
            Combo(display, aspect, 320, 85, false);
            AddLabel(display, "Internal 3D resolution", 12, 128, 300);
            internalTarget.Items.Add("Match Output");
            internalTarget.Items.AddRange(DisplaySettings.Presets);
            internalTarget.Items.Add("Custom");
            Combo(display, internalTarget, 320, 125, true);
            AddLabel(display, "Custom internal width × height", 12, 168, 300);
            Number(display, customWidth, 320, 165, 320, 16384, 1920);
            Number(display, customHeight, 460, 165, 240, 8448, 1080);
            info.Location = new Point(12, 206);
            info.Size = new Size(700, 170);
            display.Controls.Add(info);

            AddLabel(image, "Anti-aliasing", 12, 16, 300);
            antiAliasing.Items.AddRange(GraphicsSettings.AntiAliasingModes);
            Combo(image, antiAliasing, 320, 13, true);
            AddLabel(image, "Anti-aliasing quality", 12, 56, 300);
            aaQuality.Items.AddRange(GraphicsSettings.AntiAliasingQualities);
            Combo(image, aaQuality, 320, 53, true);
            AddLabel(image, "Upscaling", 12, 96, 300);
            upscaler.Items.AddRange(GraphicsSettings.Upscalers);
            Combo(image, upscaler, 320, 93, true);
            AddLabel(image, "Sharpening (0-100)", 12, 136, 300);
            Number(image, sharpness, 320, 133, 0, 100, 20);
            AddLabel(image, "Texture filtering", 12, 176, 300);
            textureFilter.Items.AddRange(GraphicsSettings.TextureFilters);
            Combo(image, textureFilter, 320, 173, true);
            AddLabel(image, "Lighting", 12, 216, 300);
            lighting.Items.AddRange(GraphicsSettings.LightingModes);
            Combo(image, lighting, 320, 213, true);
            mipmaps.Text = "Generate mipmaps for textures that have none";
            mipmaps.Location = new Point(12, 254);
            mipmaps.Size = new Size(600, 28);
            image.Controls.Add(mipmaps);
            AddLabel(image, "These also change during play (F1, or View+Menu on a controller), except mipmaps.", 12, 300, 700);

            for (int i = 0; i < volumes.Length; i++)
            {
                AddLabel(sound, AudioSettings.Names[i] + " volume (%)", 12, 16 + i * 40, 300);
                volumes[i] = new NumericUpDown { Increment = 5 };
                Number(sound, volumes[i], 320, 13 + i * 40, 0, 200, 100);
            }
            AddLabel(sound, "Cutscenes covers in-engine cutscene sounds and the movies. These also change during play.", 12, 16 + volumes.Length * 40, 700);

            AddLabel(diagnostics, "Recording", 12, 16, 300);
            recording.Items.AddRange(new[] { "No recording", "Lightweight recording", "Detailed recording" });
            Combo(diagnostics, recording, 320, 13, true);
            recording.SelectedIndex = 0;
            AddLabel(diagnostics, "Detailed recording may reduce performance. Saves use the normal Nebula folder.", 12, 52, 700);
            var sessions = Button(diagnostics, "Open diagnostics", 12, 92, 180);
            sessions.Click += delegate { Directory.CreateDirectory(NebulaPaths.Sessions); Process.Start("explorer.exe", "\"" + NebulaPaths.Sessions + "\""); };

            var import = Button(this, "Import a GalaxyRecomp save…", 12, 442, 250);
            import.Click += delegate { ImportSave(); };
            var saves = Button(this, "Open save folder", 270, 442, 160);
            saves.Click += delegate { Directory.CreateDirectory(NebulaPaths.Saves); Process.Start("explorer.exe", "\"" + NebulaPaths.Saves + "\""); };
            var chooseContent = Button(this, "Choose game content…", 438, 442, 190);
            chooseContent.Click += delegate { ChooseContent(); };
            var update = Button(this, portable ? "Beta releases" : "Check for updates", 12, 482, 170);
            update.Click += delegate
            {
                if (portable) { Process.Start("https://github.com/" + BuildInfo.Repository + "/releases"); return; }
                string setup = Path.Combine(root, "Nebula-Setup.exe");
                if (File.Exists(setup)) Process.Start(new ProcessStartInfo(setup, "--update") { WorkingDirectory = root });
            };
            play.Text = "Play";
            play.Location = new Point(628, 476);
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

        private static void AddLabel(Control parent, string text, int x, int y, int width)
        {
            parent.Controls.Add(new Label { Text = text, Location = new Point(x, y), Size = new Size(width, 26) });
        }

        private static void Combo(Control parent, ComboBox box, int x, int y, bool list)
        {
            box.Location = new Point(x, y);
            box.Width = 390;
            box.DropDownStyle = list ? ComboBoxStyle.DropDownList : ComboBoxStyle.DropDown;
            parent.Controls.Add(box);
        }

        private static void Number(Control parent, NumericUpDown box, int x, int y, int min, int max, int value)
        {
            box.Location = new Point(x, y);
            box.Size = new Size(120, 28);
            box.Minimum = min;
            box.Maximum = max;
            box.Value = value;
            parent.Controls.Add(box);
        }

        private static Button Button(Control parent, string text, int x, int y, int width)
        {
            var button = new Button { Text = text, Location = new Point(x, y), Size = new Size(width, 32) };
            parent.Controls.Add(button);
            return button;
        }

        private void LoadSettings()
        {
            graphics = GraphicsSettings.Load();
            antiAliasing.SelectedIndex = graphics.AntiAliasing;
            aaQuality.SelectedIndex = graphics.Quality;
            upscaler.SelectedIndex = graphics.Upscaler;
            sharpness.Value = graphics.Sharpness;
            textureFilter.SelectedIndex = graphics.TextureFilter;
            mipmaps.Checked = graphics.GeneratedMipmaps;
            lighting.SelectedIndex = graphics.PerPixelLighting ? 0 : 1;
            audio = AudioSettings.Load();
            for (int i = 0; i < volumes.Length; i++) volumes[i].Value = audio.Percent[i];
            output.SelectedIndex = 0;
            aspect.Text = DisplaySettings.MatchDisplayAspect;
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
                string savedAspect = Json.Str(settings, "aspect");
                // Settings written before "Match display" existed hold the old
                // 16:9 default; they follow the display from now on.
                if (savedAspect != null && !(savedAspect == "16:9" && Json.Long(settings, "settingsVersion") < 2))
                    aspect.Text = savedAspect;
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
                { "settingsVersion", 2 }, { "output", output.Text }, { "aspect", aspect.Text }, { "internal", internalTarget.Text },
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
                var screen = Screen.PrimaryScreen.Bounds;
                string aspectText = DisplaySettings.ResolveAspect(aspect.Text, screen.Width, screen.Height);
                if (borderless)
                {
                    width = screen.Width;
                    height = screen.Height;
                }
                else DisplaySettings.OutputPreset(output.Text, aspectText, out width, out height);
                outputInfo.Text = string.Format("{0}: {1} × {2} physical pixels. Aspect {3}.",
                    borderless ? "Borderless display" : "Window", width, height, aspectText);
                string selection = internalTarget.Text;
                if (selection != "Match Output" && selection != "Custom") selection = DisplaySettings.PresetHeight(selection).ToString();
                plan = DisplaySettings.Resolve(width, height, aspectText, selection, (int)customWidth.Value, (int)customHeight.Value);
                // "Match display" keeps following the window's shape after
                // launch; "Match Output" keeps the internal resolution
                // following its size.
                plan.FollowWindowAspect = string.Equals(aspect.Text.Trim(), DisplaySettings.MatchDisplayAspect,
                    StringComparison.OrdinalIgnoreCase) && !plan.NativeFourThree;
                plan.FollowWindowResolution = selection == "Match Output";
                info.ForeColor = SystemColors.ControlText;
                info.Text = string.Format(
                    "Game picture: {0}×{1}, centered at ({2},{3}).\r\nInternal scale: {6}x. Scene {4}×{5}; frame buffer {7}×{8}.\r\n" +
                    "{9}Choose 480p internal resolution for lower GPU load.",
                    plan.ContentWidth, plan.ContentHeight, plan.ContentLeft, plan.ContentTop, plan.SceneWidth, plan.SceneHeight,
                    plan.EfbScale, plan.BackingWidth, plan.BackingHeight,
                    plan.FollowWindowAspect || plan.FollowWindowResolution
                        ? "Resizing the window during play keeps the " +
                          (plan.FollowWindowAspect && plan.FollowWindowResolution ? "picture shape and resolution" :
                           plan.FollowWindowAspect ? "picture shape" : "internal resolution") + " matched.\r\n"
                        : "");
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
                graphics.AntiAliasing = antiAliasing.SelectedIndex;
                graphics.Quality = aaQuality.SelectedIndex;
                graphics.Upscaler = upscaler.SelectedIndex;
                graphics.Sharpness = (int)sharpness.Value;
                graphics.TextureFilter = textureFilter.SelectedIndex;
                graphics.GeneratedMipmaps = mipmaps.Checked;
                graphics.PerPixelLighting = lighting.SelectedIndex == 0;
                graphics.Save(plan.FollowWindowResolution);
                for (int i = 0; i < volumes.Length; i++) audio.Percent[i] = (int)volumes[i].Value;
                audio.Save();
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
                start.EnvironmentVariables["GALAXY_CRASH_DUMP_DIR"] = session;
                start.EnvironmentVariables["GALAXY_FRAME_TIMELINE_PATH"] = Path.Combine(session, "timeline.bin");
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
        [DllImport("kernel32.dll")] private static extern IntPtr OpenThread(uint access, bool inherit, uint id);
        [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
        [DllImport("kernel32.dll")] private static extern int GetThreadDescription(IntPtr thread, out IntPtr description);
        [DllImport("kernel32.dll")] private static extern IntPtr LocalFree(IntPtr memory);
        private static readonly Dictionary<int, string> threadNames = new Dictionary<int, string>();

        /// <summary>The runtime names its threads (SetThreadDescription); unnamed threads stay empty.</summary>
        private static string ThreadName(int id)
        {
            string name;
            if (threadNames.TryGetValue(id, out name) && name.Length != 0) return name;
            name = "";
            try
            {
                IntPtr handle = OpenThread(0x0800 /* THREAD_QUERY_LIMITED_INFORMATION */, false, (uint)id);
                if (handle != IntPtr.Zero)
                {
                    IntPtr text;
                    if (GetThreadDescription(handle, out text) >= 0 && text != IntPtr.Zero)
                    {
                        name = (Marshal.PtrToStringUni(text) ?? "").Replace(',', ' ');
                        LocalFree(text);
                    }
                    CloseHandle(handle);
                }
            }
            catch (EntryPointNotFoundException) { }
            threadNames[id] = name;
            return name;
        }

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
                    threads.WriteLine("elapsedSeconds,threadId,cpu100ns,threadState,waitReason,priorityLevel,name");
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
                                    "{0:F3},{1},{2},{3},{4},{5},{6}",
                                    elapsed,
                                    t.Id,
                                    t.TotalProcessorTime.Ticks,
                                    threadState,
                                    waitReason,
                                    t.PriorityLevel,
                                    ThreadName(t.Id)));
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
