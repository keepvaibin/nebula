using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

#if NEBULA_SETTINGS_APP
[assembly: System.Reflection.AssemblyTitle("Nebula settings")]
#else
[assembly: System.Reflection.AssemblyTitle("Nebula")]
#endif

namespace Nebula.Launcher
{
    internal static class Program
    {
        // Nebula.exe starts the game; the first run (and --settings) shows the
        // settings first. NebulaSettings.exe is the same program built with
        // NEBULA_SETTINGS_APP and always opens the settings.
        [STAThread]
        private static int Main(string[] args)
        {
            NativeMethods.SetProcessDpiAwarenessContext(new IntPtr(-4));
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            try
            {
                var session = new LaunchSession();
                if (!OpensSettings(args, session))
                {
                    string error = session.TryLaunch();
                    if (error == null) return 0;
                    MessageBox.Show(error, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
                Application.Run(new SettingsForm(session));
                return 0;
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        internal static bool OpensSettings(string[] args, LaunchSession session)
        {
#if NEBULA_SETTINGS_APP
            return true;
#else
            return args.Any(a => string.Equals(a, "--settings", StringComparison.OrdinalIgnoreCase)) ||
                !session.SetupComplete;
#endif
        }
    }

    /// <summary>Where the game is and how it was last started (settings.json).</summary>
    internal sealed class LaunchSession
    {
        public readonly string App = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');
        public readonly bool Portable;
        public readonly string Root;
        public string Content;
        public bool SetupComplete;
        public RecordingMode Recording = RecordingMode.None;

        public string SettingsFile { get { return Portable ? Path.Combine(App, "portable-settings.json") : NebulaPaths.SettingsFile; } }

        public LaunchSession()
        {
            // versions\<version>\ -> install root -> content
            Portable = File.Exists(Path.Combine(App, "portable.json"));
            Root = Portable ? (NebulaPaths.RegisteredInstallRoot ?? NebulaPaths.DefaultInstallRoot) : Path.GetDirectoryName(Path.GetDirectoryName(App));
            Content = Path.Combine(Root, "content");
            if (File.Exists(Path.Combine(App, "content", "game.pak"))) Content = Path.Combine(App, "content");
            try
            {
                if (!File.Exists(SettingsFile)) return;
                var settings = Json.Load(SettingsFile);
                string savedContent = Json.Str(settings, "content");
                if (savedContent != null && File.Exists(Path.Combine(savedContent, "game.pak"))) Content = savedContent;
                SetupComplete = Json.Long(settings, "settingsVersion") >= 3;
                // A recording applies to the next launch only (settings version 4).
                long recording = Json.Long(settings, "recording");
                if (Json.Long(settings, "settingsVersion") >= 4 && recording >= 0 && recording <= 2)
                    Recording = (RecordingMode)recording;
            }
            catch (Exception) { }
        }

        public void Save()
        {
            Json.Save(SettingsFile, new Dictionary<string, object>
            {
                { "settingsVersion", 4 }, { "content", Content }, { "recording", (int)Recording }
            });
            SetupComplete = true;
        }

        /// <summary>Starts the game; returns an error message instead of throwing.</summary>
        public string TryLaunch()
        {
            try
            {
                if (!File.Exists(Path.Combine(Content, "game.pak")))
                    return "Nebula could not find the game content (game.pak). Choose it in Nebula settings.";
                if (RuntimeLeaseHeld() && !EndRunningGame())
                    return "Nebula is already running.";
                LaunchOptions.ValidatePair(App);
                if (Portable) LaunchOptions.PreserveUserData(NebulaPaths.DataRoot, "portable-" + BuildInfo.Version);
                Directory.CreateDirectory(NebulaPaths.Saves);
                var start = LaunchOptions.Create(App, Content, FileUtil.FinalPath(NebulaPaths.Saves), Recording);
                if (Recording == RecordingMode.None)
                {
                    using (Process.Start(start)) { }
                    return null;
                }
                var mode = Recording;
                Recording = RecordingMode.None;
                try { Save(); } catch (Exception) { }
                Recording = mode;
                StartRecorded(start);
                return null;
            }
            catch (Exception error) { return error.Message; }
        }

        // The runtime holds a named mutex while it runs (runtime_instance.cpp). Automated sessions use private
        // lease names (GALAXY_INSTANCE_LEASE), so only the player's lease counts, not every NebulaRuntime.exe process.
        private static bool RuntimeLeaseHeld()
        {
            Mutex lease;
            if (!Mutex.TryOpenExisting(@"Local\Nebula.RMGE01.NativeRuntime", out lease)) return false;
            using (lease)
            {
                bool acquired;
                try { acquired = lease.WaitOne(0); }
                catch (AbandonedMutexException) { acquired = true; }
                if (acquired) lease.ReleaseMutex();
                return !acquired;
            }
        }

        // A game that stopped responding can keep the lease after its window is gone. Offers to end this
        // install's runtime, then waits for the lease.
        private bool EndRunningGame()
        {
            string exe = Path.Combine(App, "NebulaRuntime.exe");
            var running = new List<Process>();
            foreach (var process in Process.GetProcessesByName("NebulaRuntime"))
            {
                try
                {
                    if (string.Equals(process.MainModule.FileName, exe, StringComparison.OrdinalIgnoreCase)) { running.Add(process); continue; }
                }
                catch (Exception) { }
                process.Dispose();
            }
            if (running.Count == 0) return false;
            try
            {
                if (MessageBox.Show("Nebula is already running. End it and start again?", "Nebula",
                                    MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return false;
                foreach (var process in running)
                {
                    try { process.Kill(); process.WaitForExit(5000); } catch (Exception) { }
                }
                for (int i = 0; i < 50 && RuntimeLeaseHeld(); i++) Thread.Sleep(100);
                return !RuntimeLeaseHeld();
            }
            finally
            {
                foreach (var process in running) process.Dispose();
            }
        }

        private void StartRecorded(ProcessStartInfo start)
        {
            string session = Sessions.Create("diagnostics");
            start.EnvironmentVariables["GALAXY_CRASH_DUMP_DIR"] = session;
            start.EnvironmentVariables["GALAXY_FRAME_TIMELINE_PATH"] = Path.Combine(session, "timeline.bin");
            var launchFiles = new Dictionary<string, object>();
            foreach (string name in new[] { "NebulaRuntime.exe", "RMGE01_game.dll", "RMGE01_home_button.dll",
                "RMGE01_dsp.dll", "RMGE01_boot_image.bin", "runtime-env.json", "Nebula.exe" })
            {
                string path = Path.Combine(App, name);
                if (File.Exists(path)) launchFiles[name] = FileUtil.Sha256File(path);
            }
            var launchEnvironment = new Dictionary<string, object>();
            foreach (string name in start.EnvironmentVariables.Keys)
                if (name.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)) launchEnvironment[name] = start.EnvironmentVariables[name];
            var graphics = GraphicsSettings.Load();
            var sessionInfo = new Dictionary<string, object>
            {
                { "startUtc", DateTime.UtcNow.ToString("o") }, { "pid", null }, { "monitoring", true },
                { "recording", Recording.ToString() }, { "content", Content }, { "saves", start.EnvironmentVariables["GALAXY_NAND_ROOT"] },
                { "app", App }, { "displayMode", graphics.DisplayMode }, { "window", graphics.WindowWidth + "x" + graphics.WindowHeight },
                { "aspect", graphics.Aspect }, { "efbScale", graphics.MatchOutput ? 0 : graphics.EfbScale },
                { "detailedRendererTimings", Recording == RecordingMode.Detailed },
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
        }
    }

    /// <summary>
    /// Nebula settings: the same choices as the in-game menu (F1, or View+Menu on a
    /// controller), saved to the same files.
    /// </summary>
    internal sealed class SettingsForm : Form
    {
        private readonly LaunchSession session;
        private readonly List<MonitorEntry> monitors = DisplayInfo.Enumerate();
        private GraphicsSettings graphics = GraphicsSettings.Load();
        private AudioSettings audio = AudioSettings.Load();
        private readonly ControlsSettings controls = ControlsSettings.Load();
        private bool loading;
        private readonly ComboBox inputSource = new ComboBox(), editScheme = new ComboBox(), motion = new ComboBox(),
            vibration = new ComboBox();
        private readonly ListView bindingList = new ListView();
        private readonly NumericUpDown pointerSpeed = new NumericUpDown(), stickDeadzone = new NumericUpDown(),
            pointerDeadzone = new NumericUpDown(), shakeSensitivity = new NumericUpDown(), gyroSensitivity = new NumericUpDown(),
            pointerHide = new NumericUpDown();
        private readonly CheckBox invertPointer = new CheckBox(), gyroPointer = new CheckBox(), vibrationOn = new CheckBox();
        private readonly Label captureLabel = new Label();
        private int captureTarget = -1;
        private DateTime captureDeadline;
        private PadCaptureReader padReader;
        private PadCaptureReader.Sample captureBaseline;
        private readonly System.Windows.Forms.Timer captureTimer = new System.Windows.Forms.Timer { Interval = 15 };
        private CaptureFilter captureFilter;

        private readonly ComboBox monitor = new ComboBox(), mode = new ComboBox(), resolution = new ComboBox(), refresh = new ComboBox();
        private readonly ComboBox aspect = new ComboBox(), internalScale = new ComboBox(), frameRate = new ComboBox();
        private readonly CheckBox vsync = new CheckBox();
        private readonly ComboBox antiAliasing = new ComboBox(), aaQuality = new ComboBox(), upscaler = new ComboBox(),
            textureFilter = new ComboBox(), lighting = new ComboBox();
        private readonly NumericUpDown sharpness = new NumericUpDown();
        private readonly CheckBox mipmaps = new CheckBox(), hudOutput = new CheckBox();
        private readonly TrackBar[] volumes = new TrackBar[AudioSettings.Names.Length];
        private readonly Label[] volumeValues = new Label[AudioSettings.Names.Length];
        private readonly ComboBox recording = new ComboBox();
        private readonly Label contentLabel = new Label();
        private readonly List<Size> resolutionChoices = new List<Size>();
        // The windowed size being edited; kept while borderless is selected.
        private Size windowSize;
        private readonly List<int> refreshChoices = new List<int>();
        private readonly List<int> frameRateChoices = new List<int>();

        public SettingsForm(LaunchSession session)
        {
            this.session = session;
            bool firstRun = !session.SetupComplete;
#if NEBULA_SETTINGS_APP
            Text = "Nebula settings";
#else
            Text = firstRun ? "Welcome to Nebula" : "Nebula settings";
#endif
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }
            Font = new Font("Segoe UI", 10f);
            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(760, 560);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var intro = new Label
            {
                Location = new Point(12, 10), Size = new Size(736, 40),
                Text = "In game: F1, View + Menu (Xbox) or Minus + Plus (Switch) opens the settings."
            };
            Controls.Add(intro);

            var tabs = new TabControl { Location = new Point(12, 54), Size = new Size(736, 440) };
            var display = new TabPage("Display");
            var image = new TabPage("Graphics");
            var sound = new TabPage("Audio");
            var input = new TabPage("Controls");
            var advanced = new TabPage("Advanced");
            tabs.TabPages.AddRange(new[] { display, image, input, sound, advanced });
            Controls.Add(tabs);

            int y = 16;
            Row(display, "Monitor", monitor, ref y);
            Row(display, "Display mode", mode, ref y);
            Row(display, "Resolution", resolution, ref y);
            Row(display, "Refresh rate", refresh, ref y);
            Row(display, "Aspect ratio", aspect, ref y);
            Row(display, "Internal resolution", internalScale, ref y);
            Row(display, "Frame rate", frameRate, ref y);
            vsync.Text = "Vertical sync";
            vsync.Location = new Point(12, y);
            vsync.Size = new Size(600, 28);
            display.Controls.Add(vsync);
            for (int i = 0; i < monitors.Count; i++) monitor.Items.Add(monitors[i].Label(i));
            mode.Items.AddRange(GraphicsSettings.DisplayModes);
            aspect.Items.AddRange(GraphicsSettings.Aspects);

            y = 16;
            Row(image, "Anti-aliasing", antiAliasing, ref y);
            Row(image, "Anti-aliasing quality", aaQuality, ref y);
            Row(image, "Upscaling", upscaler, ref y);
            Label(image, "Sharpening (0-100)", 12, y + 3);
            sharpness.Location = new Point(320, y);
            sharpness.Size = new Size(120, 28);
            sharpness.Minimum = 0;
            sharpness.Maximum = 100;
            sharpness.Increment = 10;
            image.Controls.Add(sharpness);
            y += 40;
            Row(image, "Texture filtering", textureFilter, ref y);
            Row(image, "Lighting", lighting, ref y);
            mipmaps.Text = "Generated mipmaps";
            mipmaps.Location = new Point(12, y);
            mipmaps.Size = new Size(600, 28);
            image.Controls.Add(mipmaps);
            y += 32;
            hudOutput.Text = "HUD at display resolution";
            hudOutput.Location = new Point(12, y);
            hudOutput.Size = new Size(600, 28);
            image.Controls.Add(hudOutput);
            antiAliasing.Items.AddRange(GraphicsSettings.AntiAliasingModes);
            aaQuality.Items.AddRange(GraphicsSettings.AntiAliasingQualities);
            upscaler.Items.AddRange(GraphicsSettings.Upscalers);
            textureFilter.Items.AddRange(GraphicsSettings.TextureFilters);
            lighting.Items.AddRange(GraphicsSettings.LightingModes);

            for (int i = 0; i < volumes.Length; i++)
            {
                int row = 16 + i * 52;
                Label(sound, AudioSettings.Names[i], 12, row + 6);
                var bar = new TrackBar { Location = new Point(200, row), Size = new Size(420, 45), Minimum = 0, Maximum = 200, TickFrequency = 25, SmallChange = 5, LargeChange = 25 };
                var value = new Label { Location = new Point(630, row + 6), Size = new Size(80, 26) };
                int index = i;
                bar.ValueChanged += delegate { volumeValues[index].Text = volumes[index].Value + "%"; };
                volumes[i] = bar;
                volumeValues[i] = value;
                sound.Controls.Add(bar);
                sound.Controls.Add(value);
            }

            BuildControlsTab(input);

            y = 16;
            Label(advanced, "Game content", 12, y + 3);
            contentLabel.Location = new Point(200, y + 3);
            contentLabel.Size = new Size(380, 26);
            contentLabel.AutoEllipsis = true;
            advanced.Controls.Add(contentLabel);
            Button(advanced, "Change…", 590, y, 120).Click += delegate { ChooseContent(); };
            y += 44;
            Label(advanced, "Recording (next launch)", 12, y + 3);
            recording.Items.AddRange(new[] { "No recording", "Lightweight recording", "Detailed recording" });
            Combo(advanced, recording, 320, y);
            y += 44;
            Button(advanced, "Import saves…", 12, y, 250).Click += delegate { ImportSave(); };
            Button(advanced, "Open save folder", 270, y, 170).Click += delegate
            {
                Directory.CreateDirectory(NebulaPaths.Saves);
                Process.Start("explorer.exe", "\"" + NebulaPaths.Saves + "\"");
            };
            Button(advanced, "Open diagnostics", 448, y, 170).Click += delegate
            {
                Directory.CreateDirectory(NebulaPaths.Sessions);
                Process.Start("explorer.exe", "\"" + NebulaPaths.Sessions + "\"");
            };
            y += 44;
            Button(advanced, session.Portable ? "Beta releases" : "Check for updates", 12, y, 200).Click += delegate
            {
                if (session.Portable) { Process.Start("https://github.com/" + BuildInfo.Repository + "/releases"); return; }
                string setup = Path.Combine(session.Root, "Nebula-Setup.exe");
                if (File.Exists(setup)) Process.Start(new ProcessStartInfo(setup, "--update") { WorkingDirectory = session.Root });
            };

            var play = new Button { Text = "Save and play", Location = new Point(608, 508), Size = new Size(140, 38) };
            play.Click += delegate { if (Commit()) Play(); };
            Controls.Add(play);
            AcceptButton = play;
            var save = new Button { Text = "Save", Location = new Point(496, 508), Size = new Size(104, 38) };
            save.Click += delegate { if (Commit()) Close(); };
            Controls.Add(save);
#if !NEBULA_SETTINGS_APP
            save.Visible = !firstRun;
#endif

            LoadValues();
            monitor.SelectedIndexChanged += delegate { if (!loading) { graphics.FullscreenWidth = graphics.FullscreenHeight = graphics.FullscreenRefresh = 0; RefreshDisplayChoices(); } };
            mode.SelectedIndexChanged += delegate { if (!loading) RefreshDisplayChoices(); };
            aspect.SelectedIndexChanged += delegate { if (!loading) RefreshDisplayChoices(); };
            resolution.SelectedIndexChanged += delegate
            {
                if (loading) return;
                if (mode.SelectedIndex == 0 && resolution.SelectedIndex >= 0 && resolution.SelectedIndex < resolutionChoices.Count)
                    windowSize = resolutionChoices[resolution.SelectedIndex];
                RefreshRefreshChoices();
            };
            refresh.SelectedIndexChanged += delegate { if (!loading) RefreshFrameRateChoices(); };
            UiScale.Apply(this);
        }

        // ---- Controls -----------------------------------------------------------
        private int Scheme { get { return Math.Max(0, editScheme.SelectedIndex); } }

        private void BuildControlsTab(TabPage page)
        {
            int y = 12;
            SmallLabel(page, "Input", 12, y + 3, 180);
            Combo(page, inputSource, 200, y);
            inputSource.Width = 300;
            inputSource.Items.AddRange(new object[] { "Auto (keyboard, mouse and controllers)", "Keyboard and mouse only", "Controller only" });
            if (controls.InputMode == ControlsSettings.ModeRealWiimote) { inputSource.Items.Add("Wii Remote"); inputSource.Enabled = false; }
            y += 36;
            SmallLabel(page, "Edit controls for", 12, y + 3, 180);
            Combo(page, editScheme, 200, y);
            editScheme.Width = 300;
            editScheme.Items.AddRange(ControlsSettings.SchemeLabels);
            vibrationOn.Text = "Vibration";
            vibrationOn.Location = new Point(516, y + 1);
            vibrationOn.Size = new Size(96, 24);
            page.Controls.Add(vibrationOn);
            Combo(page, vibration, 616, y);
            vibration.Width = 100;
            vibration.Items.AddRange(ControlsSettings.RumbleLabels);
            y += 40;

            bindingList.Location = new Point(12, y);
            bindingList.Size = new Size(440, 434 - y - 8);
            bindingList.View = View.Details;
            bindingList.FullRowSelect = true;
            bindingList.MultiSelect = false;
            bindingList.HideSelection = false;
            bindingList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            bindingList.Columns.Add("Wii input", 170);
            bindingList.Columns.Add("Bound to", 226);
            for (int t = 0; t < ControlsSettings.TargetCount; t++)
                bindingList.Items.Add(new ListViewItem(new[] { ControlsSettings.TargetLabels[t], "" }));
            bindingList.DoubleClick += delegate { StartCapture(); };
            page.Controls.Add(bindingList);

            int x = 466;
            Button(page, "Add…", x, y, 80).Click += delegate { StartCapture(); };
            Button(page, "Clear", x + 84, y, 80).Click += delegate
            {
                int target = SelectedTarget();
                if (target < 0) return;
                controls.Bindings[Scheme][target] = new InputSource[0];
                RefreshBindings();
            };
            Button(page, "Defaults", x + 168, y, 80).Click += delegate
            {
                EndCapture();
                controls.Bindings[Scheme] = ControlsSettings.DefaultTable(Scheme);
                controls.Tuning[Scheme] = ControlsSettings.DefaultTuning(Scheme);
                LoadTuning();
                RefreshBindings();
            };
            y += 36;
            captureLabel.Location = new Point(x, y);
            captureLabel.Size = new Size(250, 30);
            page.Controls.Add(captureLabel);
            y += 30;

            TuningRow(page, "Pointer speed", pointerSpeed, x, ref y, 0.2m, 6.0m, 0.2m, 1);
            TuningRow(page, "Stick deadzone %", stickDeadzone, x, ref y, 0m, 50m, 2m, 0);
            TuningRow(page, "Pointer stick deadzone %", pointerDeadzone, x, ref y, 0m, 50m, 2m, 0);
            TuningRow(page, "Hide still pointer, s", pointerHide, x, ref y, 0m, 30m, 1m, 0);
            invertPointer.Text = "Invert pointer Y";
            invertPointer.Location = new Point(x, y);
            invertPointer.Size = new Size(250, 24);
            page.Controls.Add(invertPointer);
            y += 26;
            SmallLabel(page, "Motion", x, y + 3, 88);
            motion.Location = new Point(x + 92, y);
            motion.Width = 158;
            motion.DropDownStyle = ComboBoxStyle.DropDownList;
            motion.Items.AddRange(ControlsSettings.MotionLabels);
            page.Controls.Add(motion);
            y += 27;
            TuningRow(page, "Shake sensitivity", shakeSensitivity, x, ref y, 1m, 5m, 1m, 0);
            gyroPointer.Text = "Gyro pointer (right stick: camera)";
            gyroPointer.Location = new Point(x, y);
            gyroPointer.Size = new Size(250, 24);
            page.Controls.Add(gyroPointer);
            y += 26;
            TuningRow(page, "Gyro sensitivity", gyroSensitivity, x, ref y, 0.2m, 5.0m, 0.1m, 1);

            pointerSpeed.ValueChanged += delegate { if (!loading) controls.Tuning[Scheme].PointerSpeed = (float)pointerSpeed.Value; };
            stickDeadzone.ValueChanged += delegate { if (!loading) controls.Tuning[Scheme].StickDeadzone = (float)stickDeadzone.Value / 100f; };
            pointerDeadzone.ValueChanged += delegate { if (!loading) controls.Tuning[Scheme].PointerDeadzone = (float)pointerDeadzone.Value / 100f; };
            shakeSensitivity.ValueChanged += delegate { if (!loading) controls.Tuning[Scheme].ShakeSensitivity = (int)shakeSensitivity.Value; };
            invertPointer.CheckedChanged += delegate { if (!loading) controls.Tuning[Scheme].InvertPointerY = invertPointer.Checked; };
            gyroPointer.CheckedChanged += delegate { if (!loading) controls.Tuning[Scheme].GyroPointer = gyroPointer.Checked; };
            gyroSensitivity.ValueChanged += delegate { if (!loading) controls.Tuning[Scheme].GyroSensitivity = (float)gyroSensitivity.Value; };
            pointerHide.ValueChanged += delegate { if (!loading) controls.Tuning[Scheme].PointerHideSeconds = (float)pointerHide.Value; };
            motion.SelectedIndexChanged += delegate { if (!loading) controls.Tuning[Scheme].Motion = Math.Max(0, motion.SelectedIndex); };
            vibration.SelectedIndexChanged += delegate { if (!loading) controls.Tuning[Scheme].Rumble = ControlsSettings.RumbleSteps[Math.Max(0, vibration.SelectedIndex)]; };
            vibrationOn.CheckedChanged += delegate { if (!loading) controls.Tuning[Scheme].Vibration = vibrationOn.Checked; vibration.Enabled = vibrationOn.Enabled && vibrationOn.Checked; };
            editScheme.SelectedIndexChanged += delegate { EndCapture(); LoadTuning(); RefreshBindings(); };

            inputSource.SelectedIndex = Math.Min(controls.InputMode, inputSource.Items.Count - 1);
            editScheme.SelectedIndex = controls.InputMode == ControlsSettings.ModeController ? ControlsSettings.Xbox : ControlsSettings.Keyboard;
            captureTimer.Tick += delegate { PollCapture(); };
            FormClosed += delegate { EndCapture(); };
        }

        private static void SmallLabel(Control parent, string text, int x, int y, int width)
        {
            parent.Controls.Add(new Label { Text = text, Location = new Point(x, y), Size = new Size(width, 24) });
        }

        private static void TuningRow(Control parent, string label, NumericUpDown box, int x, ref int y,
            decimal min, decimal max, decimal step, int decimals)
        {
            parent.Controls.Add(new Label { Text = label, Location = new Point(x, y + 3), Size = new Size(170, 24) });
            box.Location = new Point(x + 172, y);
            box.Size = new Size(78, 26);
            box.Minimum = min;
            box.Maximum = max;
            box.Increment = step;
            box.DecimalPlaces = decimals;
            parent.Controls.Add(box);
            y += 27;
        }

        private static decimal Clamp(NumericUpDown box, double value)
        {
            return Math.Max(box.Minimum, Math.Min(box.Maximum, (decimal)Math.Round(value, box.DecimalPlaces)));
        }

        private void LoadTuning()
        {
            loading = true;
            var t = controls.Tuning[Scheme];
            bool pad = Scheme != ControlsSettings.Keyboard, imu = ControlsSettings.HasMotion(Scheme);
            pointerSpeed.Value = Clamp(pointerSpeed, t.PointerSpeed);
            stickDeadzone.Value = Clamp(stickDeadzone, t.StickDeadzone * 100.0);
            pointerDeadzone.Value = Clamp(pointerDeadzone, t.PointerDeadzone * 100.0);
            invertPointer.Checked = t.InvertPointerY;
            motion.SelectedIndex = Math.Max(0, Math.Min(2, t.Motion));
            shakeSensitivity.Value = Clamp(shakeSensitivity, t.ShakeSensitivity);
            gyroPointer.Checked = t.GyroPointer;
            gyroSensitivity.Value = Clamp(gyroSensitivity, t.GyroSensitivity);
            pointerHide.Value = Clamp(pointerHide, t.PointerHideSeconds);
            vibration.SelectedIndex = Math.Max(0, Array.IndexOf(ControlsSettings.RumbleSteps, ControlsSettings.SnapRumble(t.Rumble)));
            vibrationOn.Checked = t.Vibration;
            stickDeadzone.Enabled = pointerDeadzone.Enabled = pointerHide.Enabled = vibrationOn.Enabled = pad;
            vibration.Enabled = pad && t.Vibration;
            motion.Enabled = shakeSensitivity.Enabled = gyroPointer.Enabled = gyroSensitivity.Enabled = imu;
            loading = false;
        }

        private void RefreshBindings()
        {
            for (int t = 0; t < ControlsSettings.TargetCount; t++)
                bindingList.Items[t].SubItems[1].Text = captureTarget == t
                    ? "Press an input…" : ControlsSettings.FormatBindings(Scheme, controls.Bindings[Scheme][t]);
        }

        private int SelectedTarget()
        {
            return bindingList.SelectedIndices.Count == 1 ? bindingList.SelectedIndices[0] : -1;
        }

        private void StartCapture()
        {
            int target = SelectedTarget();
            if (target < 0) { captureLabel.Text = "Select a Wii input first."; return; }
            EndCapture();
            captureTarget = target;
            captureDeadline = DateTime.UtcNow.AddSeconds(8);
            captureBaseline = null;
            if (Scheme == ControlsSettings.Keyboard)
            {
                captureLabel.Text = "Press a key or mouse button. Esc cancels.";
            }
            else
            {
                captureLabel.Text = "Press a button, push a stick or pull a trigger. Esc cancels.";
                padReader = new PadCaptureReader(Scheme != ControlsSettings.Xbox);
            }
            captureFilter = new CaptureFilter(this);
            Application.AddMessageFilter(captureFilter);
            captureTimer.Start();
            RefreshBindings();
        }

        private void EndCapture()
        {
            captureTimer.Stop();
            if (captureFilter != null) { Application.RemoveMessageFilter(captureFilter); captureFilter = null; }
            if (padReader != null) { padReader.Dispose(); padReader = null; }
            if (captureTarget >= 0) { captureTarget = -1; RefreshBindings(); }
        }

        private void FinishCapture(InputSource source)
        {
            int target = captureTarget;
            int scheme = Scheme;
            EndCapture();
            if (target < 0) return;
            if (!ControlsSettings.Allowed(scheme, target, source))
            {
                captureLabel.Text = ControlsSettings.FormatSource(scheme, source) + " cannot drive " + ControlsSettings.TargetLabels[target] + ".";
                return;
            }
            controls.Bindings[scheme][target] = ControlsSettings.AddBinding(controls.Bindings[scheme][target], source);
            captureLabel.Text = "";
            RefreshBindings();
        }

        private void PollCapture()
        {
            if (captureTarget < 0) return;
            if (DateTime.UtcNow > captureDeadline) { EndCapture(); captureLabel.Text = ""; return; }
            if (padReader == null) return;
            var sample = padReader.Read(Scheme);
            if (!sample.Connected) { captureLabel.Text = "No " + ControlsSettings.SchemeLabels[Scheme] + " found. Esc cancels."; return; }
            if (captureBaseline == null) { captureBaseline = sample; return; }
            InputSource source;
            if (PadCaptureReader.Detect(captureBaseline, sample, out source)) FinishCapture(source);
            else captureBaseline = sample;
        }

        [DllImport("user32.dll")] private static extern uint MapVirtualKeyW(uint code, uint mapType);

        // Window messages while a binding waits for input. Keys and mouse buttons bind (keyboard scheme);
        // Esc cancels. Nothing else reacts.
        private sealed class CaptureFilter : IMessageFilter
        {
            private readonly SettingsForm form;
            public CaptureFilter(SettingsForm form) { this.form = form; }
            public bool PreFilterMessage(ref Message m)
            {
                const int WM_KEYDOWN = 0x100, WM_SYSKEYDOWN = 0x104, WM_LBUTTONDOWN = 0x201, WM_RBUTTONDOWN = 0x204,
                    WM_MBUTTONDOWN = 0x207, WM_XBUTTONDOWN = 0x20B, WM_KEYUP = 0x101, WM_SYSKEYUP = 0x105;
                int msg = m.Msg;
                if (msg == WM_KEYUP || msg == WM_SYSKEYUP) return true;
                bool keyboardScheme = form.Scheme == ControlsSettings.Keyboard;
                if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN)
                {
                    int vk = (int)(m.WParam.ToInt64() & 0xFF);
                    long lp = m.LParam.ToInt64();
                    if (vk == 0x1B) { form.EndCapture(); form.captureLabel.Text = ""; return true; }
                    if (!keyboardScheme) return true;
                    if (vk == 0x10) vk = (int)MapVirtualKeyW((uint)((lp >> 16) & 0xFF), 3) == 0xA1 ? 0xA1 : 0xA0;
                    else if (vk == 0x11) vk = (lp & (1L << 24)) != 0 ? 0xA3 : 0xA2;
                    form.FinishCapture(new InputSource(SourceKind.Key, vk));
                    return true;
                }
                if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_XBUTTONDOWN)
                {
                    if (!keyboardScheme) return false;
                    int button = msg == WM_LBUTTONDOWN ? 0 : msg == WM_RBUTTONDOWN ? 1 : msg == WM_MBUTTONDOWN ? 2
                        : (((m.WParam.ToInt64() >> 16) & 0xFFFF) == 1 ? 3 : 4);
                    form.FinishCapture(new InputSource(SourceKind.Mouse, button));
                    return true;
                }
                return false;
            }
        }

        private static void Label(Control parent, string text, int x, int y)
        {
            parent.Controls.Add(new Label { Text = text, Location = new Point(x, y), Size = new Size(300, 26) });
        }

        private static void Combo(Control parent, ComboBox box, int x, int y)
        {
            box.Location = new Point(x, y);
            box.Width = 390;
            box.DropDownStyle = ComboBoxStyle.DropDownList;
            parent.Controls.Add(box);
        }

        private static void Row(Control parent, string label, ComboBox box, ref int y)
        {
            Label(parent, label, 12, y + 3);
            Combo(parent, box, 320, y);
            y += 40;
        }

        private static Button Button(Control parent, string text, int x, int y, int width)
        {
            var button = new Button { Text = text, Location = new Point(x, y), Size = new Size(width, 32) };
            parent.Controls.Add(button);
            return button;
        }

        private MonitorEntry SelectedMonitor
        {
            get { return monitor.SelectedIndex >= 0 && monitor.SelectedIndex < monitors.Count ? monitors[monitor.SelectedIndex] : DisplayInfo.Find(monitors, ""); }
        }

        private void LoadValues()
        {
            loading = true;
            var current = DisplayInfo.Find(monitors, graphics.Monitor);
            monitor.SelectedIndex = Math.Max(0, monitors.IndexOf(current));
            mode.SelectedIndex = Math.Max(0, Math.Min(graphics.DisplayMode, GraphicsSettings.DisplayModes.Length - 1));
            aspect.SelectedIndex = Math.Max(0, Math.Min(graphics.Aspect, GraphicsSettings.Aspects.Length - 1));
            vsync.Checked = graphics.VSync;
            antiAliasing.SelectedIndex = graphics.AntiAliasing;
            aaQuality.SelectedIndex = graphics.Quality;
            upscaler.SelectedIndex = graphics.Upscaler;
            sharpness.Value = graphics.Sharpness;
            textureFilter.SelectedIndex = graphics.TextureFilter;
            lighting.SelectedIndex = graphics.PerPixelLighting ? 0 : 1;
            mipmaps.Checked = graphics.GeneratedMipmaps;
            hudOutput.Checked = graphics.HudOutputResolution;
            for (int i = 0; i < volumes.Length; i++)
            {
                volumes[i].Value = Math.Max(0, Math.Min(200, audio.Percent[i]));
                volumeValues[i].Text = volumes[i].Value + "%";
            }
            recording.SelectedIndex = (int)session.Recording;
            contentLabel.Text = session.Content;
            windowSize = new Size(graphics.WindowWidth, graphics.WindowHeight);
            loading = false;
            RefreshDisplayChoices();
        }

        // Resolution: the desktop (borderless), or window sizes that fit the
        // monitor's work area. With a fixed aspect the window sizes have that
        // shape, and the selection moves to the smallest one at least as tall as
        // the current window.
        private void RefreshDisplayChoices()
        {
            loading = true;
            var screen = SelectedMonitor;
            resolution.Items.Clear();
            resolutionChoices.Clear();
            int selected = 0;
            if (mode.SelectedIndex == 1)
            {
                resolutionChoices.Add(new Size(screen.Width, screen.Height));
                resolution.Items.Add(string.Format("{0} × {1} (desktop)", screen.Width, screen.Height));
            }
            else
            {
                var frame = SystemInformation.FrameBorderSize;
                int maxW = screen.WorkArea.Width - 2 * frame.Width - 16;
                int maxH = screen.WorkArea.Height - SystemInformation.CaptionHeight - 2 * frame.Height - 16;
                if (aspect.SelectedIndex > 0)
                {
                    resolutionChoices.AddRange(GraphicsSettings.AspectWindowSizes(aspect.SelectedIndex, maxW, maxH));
                    if (resolutionChoices.Count == 0)
                        resolutionChoices.AddRange(GraphicsSettings.AspectWindowSizes(aspect.SelectedIndex, int.MaxValue, int.MaxValue).Take(1));
                    selected = GraphicsSettings.SnapWindowSize(resolutionChoices, windowSize);
                }
                else
                {
                    var sizes = new List<Size> { new Size(854, 480), new Size(1024, 768), new Size(1280, 720), new Size(1280, 800),
                        new Size(1366, 768), new Size(1440, 900), new Size(1600, 900), new Size(1680, 1050), new Size(1920, 1080),
                        new Size(1920, 1200), new Size(2560, 1080), new Size(2560, 1440), new Size(2560, 1600), new Size(3440, 1440),
                        new Size(3840, 1080), new Size(3840, 2160), new Size(5120, 1440), new Size(5120, 2160), windowSize };
                    sizes.AddRange(screen.Resolutions());
                    foreach (var size in sizes.Distinct().Where(s => s.Width <= maxW && s.Height <= maxH).OrderBy(s => (long)s.Width * s.Height))
                        resolutionChoices.Add(size);
                    if (resolutionChoices.Count == 0) resolutionChoices.Add(new Size(854, 480));
                    long want = (long)windowSize.Width * windowSize.Height;
                    for (int i = 0; i < resolutionChoices.Count; i++)
                        if ((long)resolutionChoices[i].Width * resolutionChoices[i].Height <= want) selected = i;
                    int exact = resolutionChoices.IndexOf(windowSize);
                    if (exact >= 0) selected = exact;
                }
                foreach (var s in resolutionChoices)
                    resolution.Items.Add(string.Format("{0} × {1}", s.Width, s.Height));
                windowSize = resolutionChoices[selected];
            }
            resolution.SelectedIndex = Math.Min(selected, resolution.Items.Count - 1);
            loading = false;
            RefreshRefreshChoices();
        }

        private void RefreshRefreshChoices()
        {
            loading = true;
            var screen = SelectedMonitor;
            refresh.Items.Clear();
            refreshChoices.Clear();
            refresh.Items.Add(string.Format("Desktop ({0} Hz)", screen.Refresh));
            refresh.SelectedIndex = 0;
            refresh.Enabled = false;
            resolution.Enabled = mode.SelectedIndex != 1;
            RefreshScaleChoices();
            RefreshFrameRateChoices();
            loading = false;
        }

        private void RefreshFrameRateChoices()
        {
            int want = frameRate.SelectedIndex >= 0 && frameRate.SelectedIndex < frameRateChoices.Count
                ? frameRateChoices[frameRate.SelectedIndex] : graphics.FrameRate;
            frameRateChoices.Clear();
            frameRateChoices.AddRange(GraphicsSettings.FrameRates);
            frameRate.Items.Clear();
            int selected = 0;
            for (int i = 0; i < frameRateChoices.Count; i++)
            {
                frameRate.Items.Add(frameRateChoices[i] + " FPS");
                if (frameRateChoices[i] <= want) selected = i;
            }
            frameRate.SelectedIndex = selected;
        }

        // Internal resolution: matches the output, or 1x up to the scale that fills the selected display
        // (the runtime applies the same limit).
        private void RefreshScaleChoices()
        {
            var screen = SelectedMonitor;
            int cap = DisplayInfo.ScaleCovering(screen.Width, screen.Height);
            int previous = internalScale.SelectedIndex;
            bool hadItems = internalScale.Items.Count > 0;
            internalScale.Items.Clear();
            internalScale.Items.Add("Match output");
            for (int s = 1; s <= cap; s++)
                internalScale.Items.Add(string.Format("{0}x ({1} × {2})", s, 640 * s, 528 * s));
            int want = hadItems ? previous : (graphics.MatchOutput ? 0 : Math.Min(graphics.EfbScale, cap));
            internalScale.SelectedIndex = Math.Max(0, Math.Min(want, internalScale.Items.Count - 1));
        }

        private bool Commit()
        {
            try
            {
                var screen = SelectedMonitor;
                graphics.Monitor = screen != null ? screen.Device : "";
                graphics.DisplayMode = Math.Max(0, mode.SelectedIndex);
                if (resolution.SelectedIndex >= 0 && resolution.SelectedIndex < resolutionChoices.Count)
                {
                    var size = resolutionChoices[resolution.SelectedIndex];
                    if (graphics.DisplayMode == 0) { graphics.WindowWidth = size.Width; graphics.WindowHeight = size.Height; }
                }
                graphics.Aspect = Math.Max(0, aspect.SelectedIndex);
                graphics.MatchOutput = internalScale.SelectedIndex <= 0;
                if (!graphics.MatchOutput) graphics.EfbScale = internalScale.SelectedIndex;
                graphics.VSync = vsync.Checked;
                if (frameRate.SelectedIndex >= 0 && frameRate.SelectedIndex < frameRateChoices.Count)
                    graphics.FrameRate = frameRateChoices[frameRate.SelectedIndex];
                graphics.AntiAliasing = antiAliasing.SelectedIndex;
                graphics.Quality = aaQuality.SelectedIndex;
                graphics.Upscaler = upscaler.SelectedIndex;
                graphics.Sharpness = (int)sharpness.Value;
                graphics.TextureFilter = textureFilter.SelectedIndex;
                graphics.PerPixelLighting = lighting.SelectedIndex == 0;
                graphics.GeneratedMipmaps = mipmaps.Checked;
                graphics.HudOutputResolution = hudOutput.Checked;
                graphics.Save();
                for (int i = 0; i < volumes.Length; i++) audio.Percent[i] = volumes[i].Value;
                audio.Save();
                EndCapture();
                if (controls.InputMode != ControlsSettings.ModeRealWiimote) controls.InputMode = Math.Max(0, inputSource.SelectedIndex);
                controls.Save();
                session.Recording = (RecordingMode)Math.Max(0, recording.SelectedIndex);
                session.Save();
                return true;
            }
            catch (Exception error)
            {
                MessageBox.Show(this, "Settings could not be saved: " + error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
        }

        private void Play()
        {
            if (!File.Exists(Path.Combine(session.Content, "game.pak")) && !ChooseContent()) return;
            string error = session.TryLaunch();
            if (error != null)
            {
                MessageBox.Show(this, error, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            // A recording keeps this process alive, through its capture thread, until the game exits.
            Close();
        }

        private bool ChooseContent()
        {
            using (var dialog = new OpenFileDialog { Title = "Choose game.pak from your Nebula installation", Filter = "Nebula game content|game.pak", CheckFileExists = true })
            {
                if (dialog.ShowDialog(this) != DialogResult.OK) return false;
                session.Content = Path.GetDirectoryName(dialog.FileName);
                contentLabel.Text = session.Content;
                return true;
            }
        }

        private void ImportSave()
        {
            using (var dialog = new FolderBrowserDialog { Description = "Choose a saves folder from an earlier installation" })
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
        /// Streams the runtime's output on a foreground worker thread. The launcher window can close while its
        /// recording worker stays alive until game exit.
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
                    // Per-thread CPU, alongside the process-wide row. process.csv cannot say which thread spends the time, and
                    // the process-wide total does not show whether CPU rises when the frame rate falls. CPU rate per second is
                    // the comparable signal. CPU per produced frame mostly reflects the frame rate (it grows as 1/frame_rate for
                    // any slow pipeline), and it cannot tell executing from waiting.
                    //
                    // The column set matches tools/perf_session.ps1, so session_report.ps1's threads.csv parser reads it unchanged.
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
                        // A thread can exit between enumerating the collection and reading its properties, so one bad thread must
                        // not lose the whole sample. A thread that has never run reports a zero TotalProcessorTime. That is a real
                        // value, not an error.
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
                else throw new InvalidOperationException("This folder does not contain Super Mario Galaxy saves (" + GameSave + " was not found).");
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
