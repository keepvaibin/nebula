using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
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
            try
            {
                // The in-game menu runs this: a restart once the game has exited (--record: a diagnostics run).
                int restartAfter = 0;
                bool restarting = args.Length > 1 && string.Equals(args[0], "--restart-after", StringComparison.OrdinalIgnoreCase) &&
                                  int.TryParse(args[1], out restartAfter);
                // The update check, first: it runs behind everything below and is taken before the game starts
                // (UpdateService). A restart from the game's menu offers nothing.
                LaunchSession session = restarting ? null : new LaunchSession();
                if (session != null && UpdateCheck.Enabled(session)) UpdateService.Begin();
                // The settings window's displays and GPUs, listed behind the window's own start (StartLists).
                bool openMod = !restarting && args.Length > 1 && string.Equals(args[0], "--open-mod", StringComparison.OrdinalIgnoreCase);
                if (session != null && (openMod || OpensSettings(args, session))) StartLists.Begin();
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                // A .luma opened from Explorer: the Mods page, with it being imported.
                if (openMod)
                {
                    var importing = new SettingsForm(session, args[1]);
                    Application.Run(importing);
                    if (!importing.UpdateStarted) UpdateService.Finish();
                    return 0;
                }
                if (restarting)
                {
                    WaitForExit(restartAfter);
                    var restart = new LaunchSession();
                    if (args.Length > 2 && string.Equals(args[2], "--record", StringComparison.OrdinalIgnoreCase))
                        restart.Recording = RecordingMode.Lightweight;
                    string restartError = restart.TryLaunch();
                    if (restartError == null) return 0;
                    MessageBox.Show(restartError, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return 1;
                }
                if (!OpensSettings(args, session))
                {
                    if (UpdateCheck.OfferBeforeLaunch(session)) return 0;
                    string error = session.TryLaunch();
                    if (error == null)
                    {
                        // The game is up; a check still running ends here, behind it, and is kept for the next start.
                        UpdateService.Finish();
                        return 0;
                    }
                    MessageBox.Show(error, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                }
                var settings = new SettingsForm(session);
                Application.Run(settings);
                // A check still running is kept for the next start, unless an update's Setup waits for this to close.
                if (!settings.UpdateStarted) UpdateService.Finish();
                return 0;
            }
            catch (Exception error)
            {
                MessageBox.Show(error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        private static void WaitForExit(int processId)
        {
            try
            {
                using (var process = Process.GetProcessById(processId))
                    process.WaitForExit(60000);
            }
            catch (ArgumentException) { }       // already exited
            catch (InvalidOperationException) { }
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

    /// <summary>
    /// The displays (DisplayConfig) and GPUs (DXGI) the settings window lists, begun on a thread of their own as the
    /// process starts, so the window's start (visual styles, its controls, the settings files) overlaps them instead of
    /// waiting for them on the UI thread. The first window takes what was begun; any later one lists afresh (a display
    /// or GPU may have changed).
    /// </summary>
    internal static class StartLists
    {
        private static readonly object gate = new object();
        private static Task<List<MonitorEntry>> monitors;
        private static Task<List<GpuAdapter>> gpus;

        public static void Begin()
        {
            lock (gate)
            {
                if (monitors != null) return;
                monitors = Task.Factory.StartNew(() => DisplayInfo.Enumerate(), CancellationToken.None,
                    TaskCreationOptions.LongRunning, TaskScheduler.Default);
                gpus = Task.Factory.StartNew(() => GpuAdapters.List(), CancellationToken.None,
                    TaskCreationOptions.LongRunning, TaskScheduler.Default);
            }
        }

        public static List<MonitorEntry> TakeMonitors()
        {
            Task<List<MonitorEntry>> begun;
            lock (gate) { begun = monitors; monitors = null; }
            if (begun != null)
            {
                try { return begun.Result; }
                catch (AggregateException) { }
            }
            return DisplayInfo.Enumerate();
        }

        /// <summary>The GPUs, an empty list when DXGI cannot list them.</summary>
        public static List<GpuAdapter> TakeGpus()
        {
            Task<List<GpuAdapter>> begun;
            lock (gate) { begun = gpus; gpus = null; }
            List<GpuAdapter> listed = null;
            bool taken = false;
            if (begun != null)
            {
                try { listed = begun.Result; taken = true; }
                catch (AggregateException) { }
            }
            if (!taken) listed = GpuAdapters.List();
            return listed ?? new List<GpuAdapter>();
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
        public bool AskAboutUpdates = true;  // a newer release is offered at each start (UpdateCheck)
        public string SkippedUpdate;         // the release Skip this version passed over; only newer ones are offered

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
                if (settings.ContainsKey("askAboutUpdates")) AskAboutUpdates = Json.Bool(settings, "askAboutUpdates");
                SkippedUpdate = Json.Str(settings, "skippedUpdate");
            }
            catch (Exception) { }
        }

        public void Save()
        {
            Json.Save(SettingsFile, new Dictionary<string, object>
            {
                { "settingsVersion", 4 }, { "content", Content }, { "recording", (int)Recording },
                { "askAboutUpdates", AskAboutUpdates }, { "skippedUpdate", SkippedUpdate }
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
                if (!GameDisc.IsCurrentVersion(Path.Combine(Content, "game.pak")))
                    return NebulaText.Of("The game content is from another version of Nebula. Run Nebula Setup to read your game again.");
                if (RuntimeLeaseHeld() && !EndRunningGame())
                    return NebulaText.Of("Nebula is already running.");
                LaunchOptions.ValidatePair(App);
                if (Portable) LaunchOptions.PreserveUserData(NebulaPaths.DataRoot, "portable-" + BuildInfo.Version);
                Directory.CreateDirectory(NebulaPaths.Saves);
                string saves = FileUtil.FinalPath(NebulaPaths.Saves);
                var start = LaunchOptions.Create(App, Content, saves, Recording);
                if (Recording == RecordingMode.None)
                {
                    using (Process.Start(start)) { }
                    return null;
                }
                var mode = Recording;
                Recording = RecordingMode.None;
                try { Save(); } catch (Exception) { }
                Recording = mode;
                StartRecorded(start, saves);
                return null;
            }
            catch (Exception error) { return error.Message; }
        }

        private static readonly string[] Releases = { "RMGE01", "RMGP01", "RMGJ01" };

        // The game's lease names (host_main.cpp hold_runtime_lease: Local\Nebula.<release>.NativeRuntime). Tests set their own
        // namespace, so a game open in the same session is not the game the test starts or holds.
        internal static string LeaseNamespace = @"Local\Nebula.";
        internal static string LeaseName(string release) { return LeaseNamespace + release + ".NativeRuntime"; }

        // Waits up to `milliseconds` for the game of every release to end: its lease is free (or abandoned) once the
        // process is gone.
        internal static bool WaitForRuntimeLease(int milliseconds)
        {
            var timer = Stopwatch.StartNew();
            foreach (string release in Releases)
            {
                Mutex lease;
                if (!Mutex.TryOpenExisting(LeaseName(release), out lease)) continue;
                using (lease)
                {
                    bool acquired;
                    try { acquired = lease.WaitOne((int)Math.Max(0, milliseconds - timer.ElapsedMilliseconds)); }
                    catch (AbandonedMutexException) { acquired = true; }
                    if (!acquired) return false;
                    lease.ReleaseMutex();
                }
            }
            return true;
        }

        // The game's lease: a named mutex it holds while it runs, one per release (host_main.cpp hold_runtime_lease).
        internal static bool RuntimeLeaseHeld()
        {
            foreach (string release in Releases)
            {
                Mutex lease;
                if (!Mutex.TryOpenExisting(LeaseName(release), out lease)) continue;
                using (lease)
                {
                    bool acquired;
                    try { acquired = lease.WaitOne(0); }
                    catch (AbandonedMutexException) { acquired = true; }
                    if (acquired) lease.ReleaseMutex();
                    if (!acquired) return true;
                }
            }
            return false;
        }

        // A game that stopped responding can keep the lease after its window is gone. Offers to end this
        // install's game, then waits for the lease.
        private bool EndRunningGame()
        {
            string exe = Path.Combine(App, "NebulaNative.exe");
            var running = new List<Process>();
            foreach (var process in Process.GetProcessesByName("NebulaNative"))
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
                if (MessageBox.Show(NebulaText.Of("Nebula is already running. End it and start again?"), "Nebula",
                                    MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return false;
                foreach (var process in running)
                {
                    try { process.Kill(); process.WaitForExit(5000); } catch (Exception) { }
                }
                return WaitForRuntimeLease(5000);
            }
            finally
            {
                foreach (var process in running) process.Dispose();
            }
        }

        private void StartRecorded(ProcessStartInfo start, string saves)
        {
            string session = Sessions.Create("diagnostics");
            // --diagnostics 1 records the frame timeline; it is written into the session at exit.
            start.Arguments += " --timeline " + ProcessRunner.Quote(Path.Combine(session, "timeline.bin"));
            var launchFiles = new Dictionary<string, object>();
            foreach (string name in new[] { "NebulaNative.exe", "smg_game.dll", "Nebula.exe" })
            {
                string path = Path.Combine(App, name);
                if (File.Exists(path)) launchFiles[name] = FileUtil.Sha256File(path);
            }
            var graphics = GraphicsSettings.Load();
            var sessionInfo = new Dictionary<string, object>
            {
                { "startUtc", DateTime.UtcNow.ToString("o") }, { "pid", null }, { "monitoring", true },
                { "recording", Recording.ToString() }, { "content", Content }, { "saves", saves },
                { "app", App }, { "displayMode", graphics.DisplayMode }, { "window", graphics.WindowWidth + "x" + graphics.WindowHeight },
                { "aspect", GraphicsSettings.AspectNames[graphics.Aspect] },
                { "detailedRendererTimings", Recording == RecordingMode.Detailed },
                { "launchFileSha256", launchFiles }, { "arguments", start.Arguments }
            };
            Json.Save(Path.Combine(session, "session.json"), sessionInfo);
            var game = Process.Start(start);
            // Start draining redirected output before any further file work.
            Sessions.Capture(game, session, true);
            sessionInfo["pid"] = game.Id;
            try { Json.Save(Path.Combine(session, "session.json"), sessionInfo); }
            catch (IOException) { } // session.json keeps its pre-launch contents; the capture goes on.
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
        // A newer release, in the bottom row beside Save (WatchForUpdate); its prompt opens only when pressed.
        private readonly Button updateBanner = new Button();
        private ReleaseInfo pendingUpdate;
        /// <summary>True once an update's Setup was started from this window (it closes for Setup to replace it).</summary>
        public bool UpdateStarted { get; private set; }
        private readonly List<MonitorEntry> monitors = StartLists.TakeMonitors();
        private GraphicsSettings graphics = GraphicsSettings.Load();
        private AudioSettings audio = AudioSettings.Load();
        private GeneralSettings general = GeneralSettings.Load();
        private readonly ComboBox textSpeed = new ComboBox(), languageBox = new ComboBox();
        // The rows the game's menu has and the launcher had not (settings_table.tsv, windows column).
        private readonly CheckBox hdr = new CheckBox(), curvedSurfaces = new CheckBox(), textureDump = new CheckBox(),
            discordStatus = new CheckBox(), coStar = new CheckBox();
        private readonly NumericUpDown paperWhite = new NumericUpDown(), bloom = new NumericUpDown();
        private readonly Label hdrHint = new Label();
        private readonly ComboBox occlusionMode = new ComboBox(), texturePacks = new ComboBox(), soundOutput = new ComboBox(),
            buttonIcons = new ComboBox(), menuTextSize = new ComboBox(), focusLoss = new ComboBox();
        private readonly List<int> menuTextSizes = new List<int>();
        // The installed disc's languages (GeneralSettings.Offered), as the Language row lists them.
        private List<int> offeredLanguages = new List<int> { 0 };
        private readonly ControlsSettings controls = ControlsSettings.Load();
        private bool loading;
        // The player chose the upscaler, its quality, the interpolation or the frame rate. Without that, a save
        // while the game runs leaves them as the game has them: a GPU chosen for the next start falls back from
        // what it does not run (RefreshGpuOptions), and the running game, still on its own GPU, would take that.
        private bool frameRowsChosen;
        private bool gpuRefresh;

        // The upscaler the list shows (its config number).
        private int ShownUpscaler
        {
            get
            {
                int index = upscaler.SelectedIndex;
                return index >= 0 && index < upscalerChoices.Count ? upscalerChoices[index] : graphics.Upscaler;
            }
        }

        // The quality row's modes (config numbers) in list order: the shown upscaler's.
        private int[] qualityOrder = GraphicsSettings.QualityOrder;
        // The 3D resolution row's label: Internal resolution, or Upscaler quality while an upscaler runs.
        private readonly Label resolutionLabel = new Label();

        // The quality row's list and choice for the selected upscaler: its own mode, where the 3D resolution is set
        // while it runs. With upscaling off the row is hidden (its list FSR 1's, which the hidden row keeps).
        private void ShowUpscalerQuality()
        {
            int shown = ShownUpscaler;
            qualityOrder = shown == 3 ? GraphicsSettings.DlssQualityOrder : GraphicsSettings.QualityOrder;
            int value = GraphicsSettings.TemporalUpscaler(shown) ? graphics.TemporalQuality : graphics.SpatialQuality;
            if (shown == 3 && value == 5) value = 1;  // as DLSS runs it
            dlssQuality.Items.Clear();
            foreach (int mode in qualityOrder) dlssQuality.Items.Add(GraphicsSettings.QualityName(shown, mode));
            dlssQuality.SelectedIndex = Math.Max(0, Array.IndexOf(qualityOrder, value));
            RefreshImageRows();
        }

        // Keeps the quality row's choice for the upscaler it was shown for.
        private void KeepQuality(int shown)
        {
            int index = dlssQuality.SelectedIndex;
            if (shown == 0 || index < 0 || index >= qualityOrder.Length) return;
            if (GraphicsSettings.TemporalUpscaler(shown)) graphics.TemporalQuality = qualityOrder[index];
            else graphics.SpatialQuality = qualityOrder[index];
        }

        // The HUD row's own choice while a fixed one is shown.
        private bool hudChosen;

        // One row sets the 3D resolution: the internal resolution with upscaling off, the upscaler's mode with it on.
        // A temporal upscaler antialiases itself (no anti-aliasing or MSAA under it); ray tracing replaces MSAA,
        // ambient occlusion and the lighting modes. The HUD is at the display's resolution where the 3D resolution
        // is fixed (an internal resolution or a temporal upscaler). As the game's menu has them.
        private void RefreshImageRows()
        {
            int shown = ShownUpscaler;
            bool temporal = GraphicsSettings.TemporalUpscaler(shown);
            bool rt = rayTracingOffered && rayTracing.Checked;
            internalResolution.Visible = shown == 0;
            dlssQuality.Visible = shown != 0;
            resolutionLabel.Text = shown == 0 ? NebulaText.Of("Internal resolution") : NebulaText.Of("Upscaler quality");
            antiAliasing.Enabled = aaQuality.Enabled = !temporal;
            msaa.Enabled = !temporal && !rt;
            occlusionMode.Enabled = lighting.Enabled = !rt;
            bool exact = shown == 0 && internalResolution.SelectedIndex > 0;
            bool fixedHud = temporal || exact;
            if (fixedHud && hudOutput.Enabled) hudChosen = hudOutput.Checked;
            if (!fixedHud && !hudOutput.Enabled) hudOutput.Checked = hudChosen;
            hudOutput.Enabled = !fixedHud;
            if (fixedHud) hudOutput.Checked = true;
        }
        private readonly ComboBox inputSource = new ComboBox(), editScheme = new ComboBox(), motion = new ComboBox(),
            vibration = new ComboBox();
        private readonly ListView bindingList = new ListView();
        // The Performance page (BuildPerformancePage): the benchmark and its recommendation.
        internal readonly Button benchmarkButton = new Button();
        internal readonly Label benchmarkStatus = new Label();
        internal readonly Label benchmarkVerdict = new Label();
        internal readonly Label benchmarkTitle = new Label();
        internal readonly ListView benchmarkRows = new ListView();
        internal readonly Button benchmarkApply = new Button();
        private Process benchmarkRun;
        private BenchmarkResult benchmarkResult;
        private readonly NumericUpDown pointerSpeed = new NumericUpDown(), stickDeadzone = new NumericUpDown(),
            pointerDeadzone = new NumericUpDown(), shakeSensitivity = new NumericUpDown(), gyroSensitivity = new NumericUpDown(),
            pointerHide = new NumericUpDown();
        private readonly CheckBox invertPointer = new CheckBox(), gyroPointer = new CheckBox(), vibrationOn = new CheckBox();
        private readonly Label captureLabel = new Label();
        private int captureTarget = -1;
        internal SettingsPadNavigator padNavigator;
        private DateTime captureDeadline;
        private PadCaptureReader padReader;
        private PadCaptureReader.Sample captureBaseline;
        private readonly System.Windows.Forms.Timer captureTimer = new System.Windows.Forms.Timer { Interval = 15 };
        private CaptureFilter captureFilter;

        private readonly ComboBox monitor = new ComboBox(), mode = new ComboBox(), resolution = new ComboBox(), refresh = new ComboBox();
        private readonly ComboBox aspect = new ComboBox(), frameRate = new ComboBox();
        private readonly CheckBox vsync = new CheckBox();
        private readonly CheckBox askUpdates = new CheckBox();
        private readonly ComboBox antiAliasing = new ComboBox(), aaQuality = new ComboBox(), msaa = new ComboBox(),
            upscaler = new ComboBox(), internalResolution = new ComboBox(),
            dlssQuality = new ComboBox(), textureFilter = new ComboBox(), lighting = new ComboBox(),
            interpolation = new ComboBox(), rtQuality = new ComboBox();
        // The frame rate row's label: TARGET with interpolation, LIMIT without.
        private readonly Label frameRateLabel = new Label();
        private readonly List<int> interpolationChoices = new List<int>();
        private readonly NumericUpDown sharpness = new NumericUpDown();
        private readonly CheckBox mipmaps = new CheckBox(), hudOutput = new CheckBox(),
            rayTracing = new CheckBox();
        // The GPUs the game can run on, the automatic choice first; the Graphics card row shows with two or more.
        private readonly List<GpuAdapter> gpus = StartLists.TakeGpus();
        private readonly ComboBox gpuBox = new ComboBox();
        // Ray tracing and DLSS follow the GPU the game will run on (RefreshGpuOptions).
        private bool rayTracingOffered;
        private TabPage displayPage;
        // Under the Monitor row while the GPU the game will run on is not the one the display is connected to
        // (GpuAdapters.DisplayNotice); the rows below it move down by displayNoticeShift.
        private readonly Label displayNotice = new Label();
        private int displayNoticeShift;
        private readonly TrackBar[] volumes = new TrackBar[AudioSettings.Names.Length];
        private readonly Label[] volumeValues = new Label[AudioSettings.Names.Length];
        private readonly ComboBox recording = new ComboBox();
        private readonly Label contentLabel = new Label();
        private readonly List<Size> resolutionChoices = new List<Size>();
        // The windowed size being edited; kept while borderless is selected.
        private Size windowSize;
        private readonly List<int> refreshChoices = new List<int>();
        private readonly List<int> frameRateChoices = new List<int>();
        private List<int> upscalerChoices;
        // The upscaler shown before the current change (DLSS download, quality list).
        private int upscalerShown;

        // The selected GPU's id; empty for Automatic (the gpu key).
        private string SelectedGpuId
        {
            get
            {
                if (gpus.Count < 2) return graphics.Gpu;
                int index = gpuBox.SelectedIndex;
                return index >= 1 && index <= gpus.Count ? gpus[index - 1].Id : "";
            }
        }

        public SettingsForm(LaunchSession session, string importMod = null)
        {
            this.session = session;
            offeredLanguages = GeneralSettings.Offered(Path.Combine(session.Content, "game.pak"));
            SaveArchive.LooseTitle = SaveArchive.TitleOf(GameDisc.GameId(Path.Combine(session.Content, "game.pak")));
            upscalerChoices = GraphicsSettings.UpscalerChoices(gpus, graphics.Gpu);
            rayTracingOffered = GraphicsSettings.RayTracingAdapter(gpus, graphics.Gpu);
            bool firstRun = !session.SetupComplete;
#if NEBULA_SETTINGS_APP
            Text = NebulaText.Of("Nebula settings");
#else
            Text = firstRun ? NebulaText.Of("Welcome to Nebula") : NebulaText.Of("Nebula settings");
#endif
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }
            Font = new Font("Segoe UI", 10f);
            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(760, 680);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            var intro = new Label
            {
                Location = new Point(12, 10), Size = new Size(736, 40),
                Text = NebulaText.Of("In game: F1, View + Menu (Xbox) or Minus + Plus (Switch) opens the settings.")
            };
            Controls.Add(intro);

            var tabs = new TabControl { Location = new Point(12, 54), Size = new Size(736, 560), SizeMode = TabSizeMode.Fixed };
            var display = new TabPage(NebulaText.Of("Display"));
            var image = new TabPage(NebulaText.Of("Graphics"));
            var sound = new TabPage(NebulaText.Of("Audio"));
            var input = new TabPage(NebulaText.Of("Controls"));
            var generalPage = new TabPage(NebulaText.Of("General"));
            var advanced = new TabPage(NebulaText.Of("Advanced"));
            var pages = new List<TabPage> { display, image, input, sound, generalPage };
            if (ModsPage.Available(session.App))
            {
                // The game's mods (ModsPage). Added with the others: TabPages.Insert is ignored before the control
                // has a window.
                var modsTab = new TabPage(NebulaText.Of("Mods"));
                pages.Add(modsTab);
                var mods = new ModsPage(modsTab, session.App, session.Content);
                if (importMod != null)
                {
                    Shown += delegate
                    {
                        tabs.SelectedTab = modsTab;
                        mods.Import(new[] { importMod });
                    };
                }
            }
            var performance = new TabPage(NebulaText.Of("Performance"));
            pages.Add(performance);
            pages.Add(advanced);
            tabs.TabPages.AddRange(pages.ToArray());
            BuildPerformancePage(performance);
            Controls.Add(tabs);

            int y = 16;
            displayPage = display;
            if (gpus.Count >= 2)
            {
                Row(display, NebulaText.Of("Graphics card"), gpuBox, ref y);
                gpuBox.Items.Add(NebulaText.Of("Automatic ({0})", gpus[0].Name));
                foreach (var gpu in gpus) gpuBox.Items.Add(GpuAdapters.Label(gpus, gpu));
            }
            Row(display, NebulaText.Of("Monitor"), monitor, ref y);
            displayNotice.Location = new Point(12, y - 8);
            displayNotice.Size = new Size(700, 26);
            displayNotice.ForeColor = SystemColors.GrayText;
            displayNotice.Visible = false;
            display.Controls.Add(displayNotice);
            Row(display, NebulaText.Of("Display mode"), mode, ref y);
            Row(display, NebulaText.Of("Resolution"), resolution, ref y);
            Row(display, NebulaText.Of("Refresh rate"), refresh, ref y);
            Row(display, NebulaText.Of("Aspect ratio"), aspect, ref y);
            vsync.Text = NebulaText.Of("Vertical sync");
            vsync.Location = new Point(12, y);
            vsync.Size = new Size(600, 28);
            display.Controls.Add(vsync);
            y += 32;
            hudOutput.Text = NebulaText.Of("HUD at display resolution");
            hudOutput.Location = new Point(12, y);
            hudOutput.Size = new Size(600, 28);
            display.Controls.Add(hudOutput);
            y += 40;
            // HDR: only while Windows shows the chosen display in HDR (the game's rule); paper white with it on.
            hdr.Text = NebulaText.Of("HDR");
            hdr.Location = new Point(12, y);
            hdr.Size = new Size(300, 28);
            display.Controls.Add(hdr);
            hdrHint.Location = new Point(320, y + 4);
            hdrHint.Size = new Size(390, 36);
            hdrHint.ForeColor = SystemColors.GrayText;
            hdrHint.Text = NebulaText.Of("Turn on HDR for this display in the Windows settings to use it.");
            display.Controls.Add(hdrHint);
            y += 40;
            Label(display, NebulaText.Of("Paper white"), 12, y + 3);
            paperWhite.Location = new Point(320, y);
            paperWhite.Size = new Size(120, 28);
            paperWhite.Minimum = GraphicsSettings.PaperWhiteMin;
            paperWhite.Maximum = GraphicsSettings.PaperWhiteMax;
            paperWhite.Increment = 10;
            display.Controls.Add(paperWhite);
            hdr.CheckedChanged += delegate { paperWhite.Enabled = hdr.Enabled && hdr.Checked; };
            for (int i = 0; i < monitors.Count; i++) monitor.Items.Add(monitors[i].Label(i));
            mode.Items.AddRange(GraphicsSettings.DisplayModes);
            aspect.Items.AddRange(GraphicsSettings.Aspects);

            y = 16;
            Row(image, NebulaText.Of("Upscaling"), upscaler, ref y);
            // One row sets the 3D resolution, in one place: the internal resolution with upscaling off, the upscaler's
            // mode with it on (RefreshImageRows shows one).
            resolutionLabel.Location = new Point(12, y + 3);
            resolutionLabel.Size = new Size(300, 26);
            image.Controls.Add(resolutionLabel);
            Combo(image, internalResolution, 320, y);
            Combo(image, dlssQuality, 320, y);
            y += 40;
            internalResolution.Items.AddRange(GraphicsSettings.InternalResolutions);
            Label(image, NebulaText.Of("Sharpening (0-100)"), 12, y + 3);
            sharpness.Location = new Point(320, y);
            sharpness.Size = new Size(120, 28);
            sharpness.Minimum = 0;
            sharpness.Maximum = 100;
            sharpness.Increment = 10;
            image.Controls.Add(sharpness);
            y += 40;
            Row(image, NebulaText.Of("Anti-aliasing"), antiAliasing, ref y);
            Row(image, NebulaText.Of("Anti-aliasing quality"), aaQuality, ref y);
            Row(image, "MSAA", msaa, ref y);
            Row(image, NebulaText.Of("Interpolation"), interpolation, ref y);
            frameRateLabel.Location = new Point(12, y + 3);
            frameRateLabel.Size = new Size(300, 26);
            image.Controls.Add(frameRateLabel);
            Combo(image, frameRate, 320, y);
            y += 40;
            Row(image, NebulaText.Of("Texture filtering"), textureFilter, ref y);
            Row(image, NebulaText.Of("Lighting"), lighting, ref y);
            Row(image, NebulaText.Of("Ambient occlusion"), occlusionMode, ref y);
            occlusionMode.Items.AddRange(GraphicsSettings.OcclusionModes);
            Label(image, NebulaText.Of("Bloom") + " (%)", 12, y + 3);
            bloom.Location = new Point(320, y);
            bloom.Size = new Size(120, 28);
            bloom.Minimum = 0;
            bloom.Maximum = GraphicsSettings.BloomMax;
            bloom.Increment = 10;
            image.Controls.Add(bloom);
            y += 40;
            Row(image, NebulaText.Of("Texture packs"), texturePacks, ref y);
            texturePacks.Width = 190;
            texturePacks.Items.AddRange(GraphicsSettings.TexturePackModes);
            textureDump.Text = NebulaText.Of("Dump textures");
            textureDump.Location = new Point(520, y - 40 + 1);
            textureDump.Size = new Size(190, 26);
            image.Controls.Add(textureDump);
            mipmaps.Text = NebulaText.Of("Generated mipmaps");
            mipmaps.Location = new Point(12, y);
            mipmaps.Size = new Size(300, 28);
            image.Controls.Add(mipmaps);
            curvedSurfaces.Text = NebulaText.Of("Curved surfaces");
            curvedSurfaces.Location = new Point(320, y);
            curvedSurfaces.Size = new Size(300, 28);
            image.Controls.Add(curvedSurfaces);
            y += 32;
            rayTracing.Text = NebulaText.Of("Ray tracing");
            rayTracing.Location = new Point(12, y);
            rayTracing.Size = new Size(300, 28);
            rayTracing.Visible = rayTracingOffered;
            image.Controls.Add(rayTracing);
            Combo(image, rtQuality, 320, y);
            rtQuality.Items.AddRange(GraphicsSettings.RtQualities);
            rtQuality.Visible = rayTracingOffered;
            rtQuality.Enabled = false;
            rayTracing.CheckedChanged += delegate { rtQuality.Enabled = rayTracing.Checked; RefreshImageRows(); };
            antiAliasing.Items.AddRange(GraphicsSettings.AntiAliasingModes);
            aaQuality.Items.AddRange(GraphicsSettings.AntiAliasingQualities);
            msaa.Items.AddRange(GraphicsSettings.MsaaModes);
            foreach (int value in upscalerChoices) upscaler.Items.Add(GraphicsSettings.UpscalerName(value));
            textureFilter.Items.AddRange(GraphicsSettings.TextureFilters);
            lighting.Items.AddRange(GraphicsSettings.LightingModes);
            FillInterpolationChoices();
            image.AutoScroll = true;

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

            int outputRow = 16 + volumes.Length * 52;
            Label(sound, NebulaText.Of("Sound output"), 12, outputRow + 3);
            Combo(sound, soundOutput, 200, outputRow);
            soundOutput.Width = 220;
            soundOutput.Items.AddRange(AudioSettings.SoundOutputNames);

            BuildControlsTab(input);

            y = 16;
            Row(generalPage, NebulaText.Of("Text speed"), textSpeed, ref y);
            textSpeed.Items.AddRange(GeneralSettings.TextSpeeds);
            Row(generalPage, NebulaText.Of("Language"), languageBox, ref y);
            foreach (int language in offeredLanguages) languageBox.Items.Add(GeneralSettings.LanguageNames[language]);
            Row(generalPage, NebulaText.Of("Button icons"), buttonIcons, ref y);
            buttonIcons.Items.AddRange(GeneralSettings.ButtonIconNames);
            Row(generalPage, NebulaText.Of("Menu text size"), menuTextSize, ref y);
            Row(generalPage, NebulaText.Of("On losing focus"), focusLoss, ref y);
            focusLoss.Items.AddRange(GeneralSettings.FocusLossNames);
            discordStatus.Text = NebulaText.Of("Discord status");
            discordStatus.Location = new Point(12, y);
            discordStatus.Size = new Size(600, 28);
            generalPage.Controls.Add(discordStatus);

            y = 16;
            Label(advanced, NebulaText.Of("Game content"), 12, y + 3);
            Button(advanced, NebulaText.Of("Change…"), 590, y, 120).Click += delegate { ChooseContent(); };
            contentLabel.Location = new Point(12, y + 36);
            contentLabel.Size = new Size(700, 26);
            contentLabel.AutoEllipsis = true;
            contentLabel.ForeColor = SystemColors.GrayText;
            advanced.Controls.Add(contentLabel);
            y += 76;
            Label(advanced, NebulaText.Of("Recording (next launch)"), 12, y + 3);
            recording.Items.AddRange(new[] { NebulaText.Of("No recording"), NebulaText.Of("Lightweight recording"), NebulaText.Of("Detailed recording") });
            Combo(advanced, recording, 320, y);
            y += 44;
            Button(advanced, NebulaText.Of("Export saves…"), 12, y, 170).Click += delegate { ExportSaves(); };
            var importSaves = Button(advanced, NebulaText.Of("Import saves…"), 190, y, 170);
            var importMenu = new ContextMenuStrip();
            importMenu.Items.Add(NebulaText.Of("From a zip file…"), null, delegate { ImportSaves(true); });
            importMenu.Items.Add(NebulaText.Of("From a folder…"), null, delegate { ImportSaves(false); });
            importSaves.Click += delegate { importMenu.Show(importSaves, new Point(0, importSaves.Height)); };
            Button(advanced, NebulaText.Of("Open save folder"), 368, y, 170).Click += delegate
            {
                Directory.CreateDirectory(NebulaPaths.Saves);
                Process.Start("explorer.exe", "\"" + NebulaPaths.Saves + "\"");
            };
            Button(advanced, NebulaText.Of("Open diagnostics"), 546, y, 170).Click += delegate
            {
                Directory.CreateDirectory(NebulaPaths.Sessions);
                Process.Start("explorer.exe", "\"" + NebulaPaths.Sessions + "\"");
            };
            y += 44;
            Button(advanced, session.Portable ? NebulaText.Of("Beta releases") : NebulaText.Of("Check for updates"), 12, y, 200).Click += delegate
            {
                if (session.Portable) { Process.Start("https://github.com/" + BuildInfo.Repository + "/releases"); return; }
                string setup = Path.Combine(session.Root, "Nebula-Setup.exe");
                if (File.Exists(setup)) Process.Start(new ProcessStartInfo(setup, "--update") { WorkingDirectory = session.Root });
            };
            askUpdates.Text = NebulaText.Of("Check for updates at start");
            askUpdates.Location = new Point(222, y + 6);
            askUpdates.Size = new Size(360, 24);
            askUpdates.Checked = session.AskAboutUpdates;
            askUpdates.Visible = !session.Portable;
            advanced.Controls.Add(askUpdates);

            var play = new Button { Text = NebulaText.Of("Save and play"), Location = new Point(608, 628), Size = new Size(140, 38) };
            play.Click += delegate { if (Commit()) Play(); };
            Controls.Add(play);
            AcceptButton = play;
            var save = new Button { Text = NebulaText.Of("Save"), Location = new Point(496, 628), Size = new Size(104, 38) };
            save.Click += delegate { if (Commit()) Close(); };
            Controls.Add(save);
#if !NEBULA_SETTINGS_APP
            save.Visible = !firstRun;
#endif

            updateBanner.Location = new Point(12, 628);
            updateBanner.Size = new Size(470, 38);
            updateBanner.AutoEllipsis = true;
            updateBanner.TextAlign = ContentAlignment.MiddleLeft;
            updateBanner.Visible = false;
            updateBanner.Click += delegate { OpenUpdate(); };
            Controls.Add(updateBanner);

            LoadValues();
            // The last benchmark's recommendation, named as the rows (filled above) name their values.
            ShowBenchmark(Benchmark.Read(0, Benchmark.ResultPath), false);
            Shown += delegate { WatchForUpdate(); };
            // A controller reaches every row (SettingsPadNavigator); A on a Wii input waits for its new button, and the
            // pad does nothing else while it waits.
            padNavigator = new SettingsPadNavigator(this, tabs, () => captureTarget >= 0, c => { if (c == bindingList) StartCapture(); });
            monitor.SelectedIndexChanged += delegate
            {
                if (loading) return;
                FillInterpolationChoices();
                RefreshDisplayChoices();
                RefreshDisplayNotice();
                RefreshHdrRow();
            };
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
            gpuBox.SelectedIndexChanged += delegate { if (!loading) RefreshGpuOptions(); };
            dlssQuality.SelectedIndexChanged += delegate { if (!loading && !gpuRefresh) frameRowsChosen = true; };
            frameRate.SelectedIndexChanged += delegate { if (!loading && !gpuRefresh) frameRowsChosen = true; };
            internalResolution.SelectedIndexChanged += delegate { if (!loading) RefreshImageRows(); };
            // Choosing DLSS or XeSS before it is downloaded opens the download; closing it without
            // installing goes back to the previous choice.
            upscalerShown = upscaler.SelectedIndex;
            upscaler.SelectedIndexChanged += delegate
            {
                if (loading) return;
                frameRowsChosen = true;
                int index = upscaler.SelectedIndex;
                int chosen = index >= 0 ? upscalerChoices[index] : 0;
                VendorPackage package = chosen == 3 ? DlssPackage.Package : chosen == 4 ? XessPackage.Package : null;
                if (package != null && !package.Installed())
                {
                    bool installed;
                    using (var dialog = new VendorInstallDialog(package, false)) installed = dialog.ShowDialog(this) == DialogResult.OK;
                    loading = true;
                    if (installed) upscaler.Items[index] = GraphicsSettings.UpscalerName(chosen);
                    else upscaler.SelectedIndex = upscalerShown;
                    loading = false;
                }
                // The quality list follows the upscaler: its choice for the one shown before is kept.
                if (upscalerShown >= 0 && upscalerShown < upscalerChoices.Count) KeepQuality(upscalerChoices[upscalerShown]);
                upscalerShown = upscaler.SelectedIndex;
                ShowUpscalerQuality();
                // DLSS Frame Generation runs with DLSS only, FSR 3 Frame Generation with FSR 3 or XeSS.
                int now = ShownUpscaler;
                if ((SelectedInterpolation == 2 && now != 3) || (SelectedInterpolation == 3 && now != 2 && now != 4))
                {
                    loading = true;
                    interpolation.SelectedIndex = interpolationChoices.IndexOf(1);
                    loading = false;
                }
                RefreshFrameRateChoices();
            };
            interpolation.SelectedIndexChanged += delegate
            {
                if (loading) return;
                frameRowsChosen = true;
                // Frame generation selects its upscaler (DLSS's download when it is not installed yet): DLSS for
                // NVIDIA's, FSR 3 for AMD's unless XeSS runs.
                int now = ShownUpscaler;
                if (SelectedInterpolation == 2 && now != 3)
                    upscaler.SelectedIndex = upscalerChoices.IndexOf(3);
                else if (SelectedInterpolation == 3 && now != 2 && now != 4)
                    upscaler.SelectedIndex = upscalerChoices.IndexOf(2);
                RefreshFrameRateChoices();
            };
            UiScale.Apply(this);
            // Tab headers are not scaled with the form.
            // Every page's tab in one row: narrower than 98 px only when there are too many for the width.
            using (var g = CreateGraphics())
                tabs.ItemSize = new Size(Math.Min((int)(98 * g.DpiX / 96f), (tabs.Width - 4) / Math.Max(1, tabs.TabCount)), (int)(28 * g.DpiY / 96f));
            // After scaling: the notice moves the rows below it by its measured height.
            RefreshDisplayNotice();
        }

        // The offer of a newer release: at once from the answer already in (this start's check, else the last one), and
        // when this start's check answers while the window is open. It shows the banner and takes no focus.
        // The Performance page: what the benchmark does, its button and progress, then its verdict and the recommended
        // settings that differ from the current ones (the last result, when a run finished earlier), with Apply.
        private void BuildPerformancePage(TabPage page)
        {
            page.Controls.Add(new Label
            {
                Location = new Point(12, 16), Size = new Size(700, 66),
                Text = NebulaText.Of("Runs real stages to measure this device, then recommends settings. It takes a few minutes. Your saves and settings are not changed.")
            });
            benchmarkButton.Text = NebulaText.Of("Benchmark");
            benchmarkButton.Location = new Point(12, 88);
            benchmarkButton.Size = new Size(180, 34);
            benchmarkButton.Click += delegate { StartBenchmark(); };
            page.Controls.Add(benchmarkButton);
            benchmarkStatus.Location = new Point(204, 95);
            benchmarkStatus.Size = new Size(508, 24);
            benchmarkStatus.AutoEllipsis = true;
            page.Controls.Add(benchmarkStatus);
            benchmarkVerdict.Location = new Point(12, 134);
            benchmarkVerdict.Size = new Size(700, 46);
            page.Controls.Add(benchmarkVerdict);
            benchmarkTitle.Text = NebulaText.Of("Recommended settings");
            benchmarkTitle.Location = new Point(12, 186);
            benchmarkTitle.Size = new Size(700, 24);
            benchmarkTitle.Font = new Font(Font, FontStyle.Bold);
            page.Controls.Add(benchmarkTitle);
            benchmarkRows.View = View.Details;
            benchmarkRows.FullRowSelect = true;
            benchmarkRows.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            benchmarkRows.Location = new Point(12, 214);
            benchmarkRows.Size = new Size(700, 220);
            benchmarkRows.Columns.Add("", 300);
            benchmarkRows.Columns.Add(NebulaText.Of("Current"), 190);
            benchmarkRows.Columns.Add(NebulaText.Of("Recommended"), 190);
            page.Controls.Add(benchmarkRows);
            benchmarkApply.Text = NebulaText.Of("Apply");
            benchmarkApply.Location = new Point(12, 444);
            benchmarkApply.Size = new Size(140, 34);
            benchmarkApply.Click += delegate { ApplyBenchmark(); };
            page.Controls.Add(benchmarkApply);
        }

        // The game started for a benchmark (perf/bench's contract); its steps in the status, its result when it exits.
        // Refused, like a game start, while the game runs.
        private void StartBenchmark()
        {
            if (benchmarkRun != null) return;
            if (LaunchSession.RuntimeLeaseHeld())
            {
                benchmarkStatus.Text = NebulaText.Of("Nebula is already running.");
                return;
            }
            string file = Benchmark.ResultPath;
            Process run;
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(file));
                if (File.Exists(file)) File.Delete(file);
                run = new Process { StartInfo = Benchmark.Create(session.App, session.Content, file), EnableRaisingEvents = true };
                run.OutputDataReceived += (sender, e) =>
                {
                    int step, steps;
                    if (Benchmark.Progress(e.Data, out step, out steps))
                        Post(() => { if (benchmarkRun != null) benchmarkStatus.Text = NebulaText.Of("Benchmark: step {0} of {1}", Math.Max(1, step), steps); });
                };
                run.ErrorDataReceived += delegate { };
                run.Exited += delegate { Post(() => FinishBenchmark(run)); };
                run.Start();
                run.BeginOutputReadLine();
                run.BeginErrorReadLine();
            }
            catch (Exception error)
            {
                benchmarkStatus.Text = error.Message;
                return;
            }
            benchmarkRun = run;
            benchmarkButton.Enabled = false;
            benchmarkStatus.Text = NebulaText.Of("Benchmark");
            ShowBenchmark(null, false);
        }

        private void Post(Action action)
        {
            try
            {
                if (!IsDisposed && IsHandleCreated) BeginInvoke(action);
            }
            catch (InvalidOperationException) { }   // the window closed meanwhile
        }

        private void FinishBenchmark(Process run)
        {
            if (run != benchmarkRun) return;
            int code;
            try { code = run.ExitCode; }
            catch (InvalidOperationException) { code = -1; }
            run.Dispose();
            benchmarkRun = null;
            benchmarkButton.Enabled = true;
            benchmarkStatus.Text = "";
            ShowBenchmark(Benchmark.Read(code, Benchmark.ResultPath), true);
        }

        internal void ShowBenchmark(BenchmarkResult result, bool fresh)
        {
            benchmarkResult = result;
            var changes = result != null && result.Complete ? result.Changes : new List<BenchmarkSetting>();
            benchmarkVerdict.Text = result != null ? result.VerdictText : fresh ? NebulaText.Of("The benchmark did not finish.") : "";
            benchmarkRows.Items.Clear();
            foreach (var change in changes)
                benchmarkRows.Items.Add(new ListViewItem(new[] { BenchmarkLabel(change.Key), BenchmarkValue(change.Key, change.Current), BenchmarkValue(change.Key, change.Recommended) }));
            benchmarkTitle.Visible = benchmarkRows.Visible = benchmarkApply.Visible = changes.Count > 0;
            benchmarkApply.Enabled = true;
        }

        private static string BenchmarkLabel(string key)
        {
            switch (key)
            {
                case "internal_height": return NebulaText.Of("Internal resolution");
                case "msaa_samples": return "MSAA";
                case "ambient_occlusion": return NebulaText.Of("Ambient occlusion");
                case "enhanced_lighting": return NebulaText.Of("Lighting");
                case "aa": return NebulaText.Of("Anti-aliasing");
                case "smaa_quality": return NebulaText.Of("Anti-aliasing quality");
                case "bloom": return NebulaText.Of("Bloom");
                default: return key;
            }
        }

        // A value as this window's own rows name it.
        private string BenchmarkValue(string key, int value)
        {
            switch (key)
            {
                case "internal_height":
                {
                    int index = Array.IndexOf(GraphicsSettings.InternalHeights, value);
                    return index >= 0 && index < internalResolution.Items.Count ? internalResolution.Items[index].ToString() : value + "p";
                }
                case "msaa_samples":
                {
                    int index = MsaaIndex(value);
                    return index >= 0 && index < msaa.Items.Count ? msaa.Items[index].ToString() : value + "x";
                }
                case "ambient_occlusion": return NebulaText.Of(value != 0 ? "On" : "Off");
                case "enhanced_lighting":
                    // Off: the lighting this window has, or per pixel in place of enhanced (what Apply sets).
                    return value != 0 ? lighting.Items[0].ToString() : lighting.Items[lighting.SelectedIndex > 0 ? lighting.SelectedIndex : 1].ToString();
                case "aa": return value >= 0 && value < antiAliasing.Items.Count ? antiAliasing.Items[value].ToString() : value.ToString(CultureInfo.InvariantCulture);
                case "smaa_quality": return value >= 0 && value < aaQuality.Items.Count ? aaQuality.Items[value].ToString() : value.ToString(CultureInfo.InvariantCulture);
                case "bloom": return value.ToString(CultureInfo.InvariantCulture) + "%";
                default: return value.ToString(CultureInfo.InvariantCulture);
            }
        }

        private static int MsaaIndex(int samples)
        {
            switch (samples) { case 1: return 0; case 2: return 1; case 4: return 2; case 8: return 3; default: return -1; }
        }

        // Apply: the recommended values on this window's rows (so Save keeps them) and in config.ini at once, through
        // GraphicsSettings (only those keys change there).
        internal void ApplyBenchmark()
        {
            if (benchmarkResult == null || !benchmarkResult.Complete) return;
            var saved = GraphicsSettings.Load();
            foreach (var change in benchmarkResult.Changes)
            {
                int value = change.Recommended;
                switch (change.Key)
                {
                    case "internal_height":
                    {
                        int index = Array.IndexOf(GraphicsSettings.InternalHeights, value);
                        if (index < 0) break;
                        internalResolution.SelectedIndex = index;
                        saved.InternalHeight = value;
                        break;
                    }
                    case "msaa_samples":
                    {
                        int index = MsaaIndex(value);
                        if (index < 0 || index >= msaa.Items.Count) break;
                        msaa.SelectedIndex = index;
                        saved.Msaa = index;
                        break;
                    }
                    case "ambient_occlusion":
                        // On keeps the kind chosen (SSAO or GTAO), the saved one when it was off.
                        occlusionMode.SelectedIndex = value == 0 ? 0 : occlusionMode.SelectedIndex > 0 ? occlusionMode.SelectedIndex : saved.AmbientOcclusionSsao ? 1 : 2;
                        saved.AmbientOcclusion = value != 0;
                        break;
                    case "enhanced_lighting":
                        lighting.SelectedIndex = value != 0 ? 0 : lighting.SelectedIndex == 0 ? 1 : lighting.SelectedIndex;
                        // The row's choice: enhanced and per pixel both light per pixel.
                        saved.EnhancedLighting = value != 0;
                        saved.PerPixelLighting = lighting.SelectedIndex != 2;
                        break;
                    case "aa":
                        if (value < 0 || value >= antiAliasing.Items.Count) break;
                        antiAliasing.SelectedIndex = value;
                        saved.AntiAliasing = value;
                        break;
                    case "smaa_quality":
                        if (value < 0 || value >= aaQuality.Items.Count) break;
                        aaQuality.SelectedIndex = value;
                        saved.Quality = value;
                        break;
                    case "bloom":
                        bloom.Value = Math.Max(bloom.Minimum, Math.Min(bloom.Maximum, value));
                        saved.Bloom = (int)bloom.Value;
                        break;
                }
            }
            RefreshImageRows();
            try { saved.Save(); }
            catch (Exception error)
            {
                MessageBox.Show(this, NebulaText.Of("Settings could not be saved: {0}", error.Message), Text, MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            benchmarkApply.Enabled = false;
        }

        private void WatchForUpdate()
        {
            if (!UpdateCheck.Enabled(session)) return;
            ShowUpdate(UpdateCheck.Pending(session));
            var check = UpdateService.Running;
            if (check == null || check.IsCompleted) return;
            check.ContinueWith(delegate
            {
                try
                {
                    if (!IsDisposed && IsHandleCreated)
                        BeginInvoke((Action)delegate { if (!IsDisposed) ShowUpdate(UpdateCheck.Pending(session)); });
                }
                catch (InvalidOperationException) { }   // the window closed meanwhile
            });
        }

        private void ShowUpdate(ReleaseInfo release)
        {
            pendingUpdate = release;
            if (release != null) updateBanner.Text = NebulaText.Of("Nebula {0} is available", release.Version) + "  ·  " + NebulaText.Of("Update");
            updateBanner.Visible = release != null;
        }

        // The prompt, from the banner. An update started keeps the edits made here and closes the window, so Setup
        // can replace the launcher; Skip this version hides the banner.
        private void OpenUpdate()
        {
            if (pendingUpdate == null) return;
            if (!UpdateCheck.Prompt(session, pendingUpdate, this, false))
            {
                ShowUpdate(UpdateCheck.Pending(session));
                return;
            }
            UpdateStarted = true;
            Commit();
            Close();
        }

        private int Scheme { get { return Math.Max(0, editScheme.SelectedIndex); } }

        private void BuildControlsTab(TabPage page)
        {
            int y = 12;
            SmallLabel(page, NebulaText.Of("Input"), 12, y + 3, 180);
            Combo(page, inputSource, 200, y);
            inputSource.Width = 300;
            inputSource.Items.AddRange(new object[] { NebulaText.Of("Auto (keyboard, mouse and controllers)"), NebulaText.Of("Keyboard and mouse only"), NebulaText.Of("Controller only") });
            if (controls.InputMode == ControlsSettings.ModeRealWiimote) { inputSource.Items.Add(NebulaText.Of("Wii Remote")); inputSource.Enabled = false; }
            y += 36;
            SmallLabel(page, NebulaText.Of("Edit controls for"), 12, y + 3, 180);
            Combo(page, editScheme, 200, y);
            editScheme.Width = 300;
            editScheme.Items.AddRange(ControlsSettings.SchemeLabels);
            vibrationOn.Text = NebulaText.Of("Vibration");
            vibrationOn.Location = new Point(516, y + 1);
            vibrationOn.Size = new Size(96, 24);
            page.Controls.Add(vibrationOn);
            Combo(page, vibration, 616, y);
            vibration.Width = 100;
            vibration.Items.AddRange(ControlsSettings.RumbleLabels);
            y += 40;

            bindingList.Location = new Point(12, y);
            bindingList.Size = new Size(440, 518 - y);
            bindingList.View = View.Details;
            bindingList.FullRowSelect = true;
            bindingList.MultiSelect = false;
            bindingList.HideSelection = false;
            bindingList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            bindingList.Columns.Add(NebulaText.Of("Wii input"), 170);
            bindingList.Columns.Add(NebulaText.Of("Bound to"), 226);
            bindingList.ClientSizeChanged += delegate
            {
                int width = bindingList.ClientSize.Width - 1;
                bindingList.Columns[0].Width = width * 2 / 5;
                bindingList.Columns[1].Width = width - bindingList.Columns[0].Width;
            };
            for (int t = 0; t < ControlsSettings.TargetCount; t++)
                bindingList.Items.Add(new ListViewItem(new[] { ControlsSettings.TargetLabels[t], "" }));
            bindingList.DoubleClick += delegate { StartCapture(); };
            page.Controls.Add(bindingList);

            int x = 466;
            Button(page, NebulaText.Of("Add…"), x, y, 80).Click += delegate { StartCapture(); };
            Button(page, NebulaText.Of("Clear"), x + 84, y, 80).Click += delegate
            {
                int target = SelectedTarget();
                if (target < 0) return;
                controls.Bindings[Scheme][target] = new InputSource[0];
                RefreshBindings();
            };
            Button(page, NebulaText.Of("Defaults"), x + 168, y, 80).Click += delegate
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

            TuningRow(page, NebulaText.Of("Pointer speed"), pointerSpeed, x, ref y, 0.2m, 6.0m, 0.2m, 1);
            TuningRow(page, NebulaText.Of("Stick deadzone %"), stickDeadzone, x, ref y, 0m, 50m, 2m, 0);
            TuningRow(page, NebulaText.Of("Pointer stick deadzone %"), pointerDeadzone, x, ref y, 0m, 50m, 2m, 0);
            TuningRow(page, NebulaText.Of("Hide idle pointer (s)"), pointerHide, x, ref y, 0m, 30m, 1m, 0);
            invertPointer.Text = NebulaText.Of("Invert pointer Y");
            invertPointer.Location = new Point(x, y);
            invertPointer.Size = new Size(250, 24);
            page.Controls.Add(invertPointer);
            y += 26;
            SmallLabel(page, NebulaText.Of("Motion"), x, y + 3, 88);
            motion.Location = new Point(x + 92, y);
            motion.Width = 158;
            motion.DropDownStyle = ComboBoxStyle.DropDownList;
            motion.Items.AddRange(ControlsSettings.MotionLabels);
            page.Controls.Add(motion);
            y += 27;
            TuningRow(page, NebulaText.Of("Shake sensitivity"), shakeSensitivity, x, ref y, 1m, 5m, 1m, 0);
            gyroPointer.Text = NebulaText.Of("Gyro pointer (right stick: camera)");
            gyroPointer.Location = new Point(x, y);
            gyroPointer.Size = new Size(250, 24);
            page.Controls.Add(gyroPointer);
            y += 26;
            TuningRow(page, NebulaText.Of("Gyro sensitivity"), gyroSensitivity, x, ref y, 0.2m, 5.0m, 0.1m, 1);
            // Not per scheme: a second controller's pointer for a second player.
            coStar.Text = NebulaText.Of("Co-Star");
            coStar.Location = new Point(x, y + 8);
            coStar.Size = new Size(250, 24);
            page.Controls.Add(coStar);

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
                    ? NebulaText.Of("Press an input…") : ControlsSettings.FormatBindings(Scheme, controls.Bindings[Scheme][t]);
        }

        private int SelectedTarget()
        {
            return bindingList.SelectedIndices.Count == 1 ? bindingList.SelectedIndices[0] : -1;
        }

        private void StartCapture()
        {
            int target = SelectedTarget();
            if (target < 0) { captureLabel.Text = NebulaText.Of("Select a Wii input first."); return; }
            EndCapture();
            captureTarget = target;
            captureDeadline = DateTime.UtcNow.AddSeconds(8);
            captureBaseline = null;
            if (Scheme == ControlsSettings.Keyboard)
            {
                captureLabel.Text = NebulaText.Of("Press a key or mouse button. Esc cancels.");
            }
            else
            {
                captureLabel.Text = NebulaText.Of("Press a button, push a stick or pull a trigger. Esc cancels.");
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
                captureLabel.Text = NebulaText.Of("{0} cannot drive {1}.", ControlsSettings.FormatSource(scheme, source), ControlsSettings.TargetLabels[target]);
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
            if (!sample.Connected) { captureLabel.Text = NebulaText.Of("No {0} found. Esc cancels.", ControlsSettings.SchemeLabels[Scheme]); return; }
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
            if (gpus.Count >= 2)
            {
                // A chosen GPU that is not present shows as Automatic, which is what the game then uses.
                int chosen = gpus.FindIndex(g => g.Id == graphics.Gpu);
                gpuBox.SelectedIndex = chosen >= 0 ? chosen + 1 : 0;
            }
            var current = DisplayInfo.Find(monitors, graphics.Monitor);
            monitor.SelectedIndex = Math.Max(0, monitors.IndexOf(current));
            mode.SelectedIndex = Math.Max(0, Math.Min(graphics.DisplayMode, GraphicsSettings.DisplayModes.Length - 1));
            aspect.SelectedIndex = Math.Max(0, Math.Min(graphics.Aspect, GraphicsSettings.Aspects.Length - 1));
            vsync.Checked = graphics.VSync;
            antiAliasing.SelectedIndex = graphics.AntiAliasing;
            aaQuality.SelectedIndex = graphics.Quality;
            msaa.SelectedIndex = graphics.Msaa;
            upscaler.SelectedIndex = upscalerChoices.IndexOf(GraphicsSettings.OfferedUpscaler(upscalerChoices, graphics.Upscaler));
            internalResolution.SelectedIndex = Math.Max(0, Array.IndexOf(GraphicsSettings.InternalHeights, graphics.InternalHeight));
            hudOutput.Checked = hudChosen = graphics.HudOutputResolution;
            ShowUpscalerQuality();
            FillInterpolationChoices();
            int interpolationMode = graphics.FrameGeneration == 2 && interpolationChoices.Contains(3) ? 3
                : graphics.FrameGeneration != 0 && interpolationChoices.Contains(2) ? 2
                : graphics.FrameRate != 0 && graphics.FrameRate <= 61 ? 0 : 1;
            interpolation.SelectedIndex = Math.Max(0, interpolationChoices.IndexOf(interpolationMode));
            sharpness.Value = graphics.Sharpness;
            textureFilter.SelectedIndex = graphics.TextureFilter;
            lighting.SelectedIndex = graphics.EnhancedLighting ? 0 : graphics.PerPixelLighting ? 1 : 2;
            mipmaps.Checked = graphics.GeneratedMipmaps;
            occlusionMode.SelectedIndex = graphics.OcclusionMode;
            bloom.Value = Math.Max(0, Math.Min(GraphicsSettings.BloomMax, graphics.Bloom));
            curvedSurfaces.Checked = graphics.CurvedSurfaces;
            texturePacks.SelectedIndex = graphics.TexturePackMode;
            textureDump.Checked = graphics.TextureDump;
            hdr.Checked = graphics.Hdr;
            paperWhite.Value = Math.Max(GraphicsSettings.PaperWhiteMin, Math.Min(GraphicsSettings.PaperWhiteMax, graphics.PaperWhite));
            soundOutput.SelectedIndex = Math.Max(0, Math.Min(audio.SoundOutput, AudioSettings.SoundOutputNames.Length - 1));
            buttonIcons.SelectedIndex = Math.Max(0, Math.Min(general.ButtonIcons, GeneralSettings.ButtonIconNames.Length - 1));
            FillMenuTextSizes(general.MenuTextSize);
            focusLoss.SelectedIndex = Math.Max(0, Math.Min(general.FocusLoss, GeneralSettings.FocusLossNames.Length - 1));
            discordStatus.Checked = general.DiscordStatus;
            coStar.Checked = controls.CoStar;
            rayTracing.Checked = graphics.RayTracing;
            rtQuality.SelectedIndex = Math.Max(0, Array.IndexOf(GraphicsSettings.RtQualityOrder, graphics.RtQuality));
            rtQuality.Enabled = graphics.RayTracing;
            RefreshImageRows();
            for (int i = 0; i < volumes.Length; i++)
            {
                volumes[i].Value = Math.Max(0, Math.Min(200, audio.Percent[i]));
                volumeValues[i].Text = volumes[i].Value + "%";
            }
            textSpeed.SelectedIndex = Math.Max(0, Math.Min(general.TextSpeed - 1, GeneralSettings.TextSpeeds.Length - 1));
            languageBox.SelectedIndex = Math.Max(0, offeredLanguages.IndexOf(general.Language));
            recording.SelectedIndex = (int)session.Recording;
            contentLabel.Text = session.Content;
            windowSize = new Size(graphics.WindowWidth, graphics.WindowHeight);
            loading = false;
            RefreshDisplayChoices();
            RefreshHdrRow();
        }

        // The game's menu's text sizes (Auto, 80 % to 200 %), with a size of the file's that is not a step kept in order.
        private void FillMenuTextSizes(int current)
        {
            menuTextSizes.Clear();
            menuTextSizes.AddRange(GeneralSettings.MenuTextSizes);
            if (!menuTextSizes.Contains(current)) { menuTextSizes.Add(current); menuTextSizes.Sort(); }
            menuTextSize.Items.Clear();
            foreach (int size in menuTextSizes) menuTextSize.Items.Add(size == 0 ? NebulaText.Of("Auto") : size + "%");
            menuTextSize.SelectedIndex = menuTextSizes.IndexOf(current);
        }

        // HDR is offered while Windows shows the chosen display in HDR; otherwise the row stays as saved, greyed, with
        // the hint (the game ignores the setting on an SDR display).
        private void RefreshHdrRow()
        {
            var screen = SelectedMonitor;
            bool offered = screen != null && screen.Hdr;
            hdr.Enabled = offered;
            paperWhite.Enabled = offered && hdr.Checked;
            hdrHint.Visible = !offered;
        }

        private void RefreshDisplayNotice()
        {
            var screen = SelectedMonitor;
            string text = screen != null ? GpuAdapters.DisplayNotice(gpus, SelectedGpuId, screen.Device) : null;
            int shift = 0;
            if (text != null)
            {
                displayNotice.Height = TextRenderer.MeasureText(text, displayNotice.Font, new Size(displayNotice.Width, 0),
                                                                TextFormatFlags.WordBreak).Height;
                shift = displayNotice.Height;
            }
            displayNotice.Text = text ?? "";
            displayNotice.Visible = text != null;
            if (shift == displayNoticeShift) return;
            foreach (Control control in displayPage.Controls)
                if (control != displayNotice && control.Top >= displayNotice.Top) control.Top += shift - displayNoticeShift;
            displayNoticeShift = shift;
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
                resolution.Items.Add(NebulaText.Of("{0} × {1} (desktop)", screen.Width, screen.Height));
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
            refresh.Items.Add(NebulaText.Of("Desktop ({0} Hz)", screen.Refresh));
            refresh.SelectedIndex = 0;
            refresh.Enabled = false;
            resolution.Enabled = mode.SelectedIndex != 1;
            RefreshFrameRateChoices();
            loading = false;
        }

        private int SelectedInterpolation
        {
            get
            {
                int index = interpolation.SelectedIndex;
                return index >= 0 && index < interpolationChoices.Count ? interpolationChoices[index] : 0;
            }
        }

        // Another GPU: DLSS, DLSS Frame Generation and ray tracing follow what it runs. A choice it does not
        // run falls back (DLSS to FSR 1, Frame Generation to Native); the others are kept. While the game runs
        // the fallbacks wait for its next start (Commit).
        private void RefreshGpuOptions()
        {
            gpuRefresh = true;
            try { ShowGpuOptions(); }
            finally { gpuRefresh = false; }
        }

        private void ShowGpuOptions()
        {
            string gpu = SelectedGpuId;
            int shownUpscaler = ShownUpscaler;
            KeepQuality(shownUpscaler);

            loading = true;
            upscalerChoices = GraphicsSettings.UpscalerChoices(gpus, gpu);
            upscaler.Items.Clear();
            foreach (int value in upscalerChoices) upscaler.Items.Add(GraphicsSettings.UpscalerName(value));
            upscaler.SelectedIndex = upscalerChoices.IndexOf(GraphicsSettings.OfferedUpscaler(upscalerChoices, shownUpscaler));
            upscalerShown = upscaler.SelectedIndex;
            rayTracingOffered = GraphicsSettings.RayTracingAdapter(gpus, gpu);
            rayTracing.Visible = rtQuality.Visible = rayTracingOffered;
            loading = false;

            FillInterpolationChoices();
            ShowUpscalerQuality();
            RefreshFrameRateChoices();
            RefreshDisplayNotice();
        }

        // The interpolation modes this GPU, display and upscaler list offer (InterpolationModes): DLSS Frame
        // Generation where DLSS is, AMD's where the display shows its 120. The selection stays where it is offered,
        // else goes to Native (Off stays Off).
        private void FillInterpolationChoices()
        {
            int shown = interpolation.Items.Count != 0 ? SelectedInterpolation : -1;
            int refreshHz = SelectedMonitor != null ? SelectedMonitor.Refresh : 0;
            bool wasLoading = loading;
            loading = true;
            interpolationChoices.Clear();
            interpolationChoices.Add(0);
            interpolationChoices.Add(1);
            if (upscalerChoices.Contains(3)) interpolationChoices.Add(2);
            if (refreshHz == 0 || refreshHz >= 119) interpolationChoices.Add(3);
            interpolation.Items.Clear();
            foreach (int value in interpolationChoices) interpolation.Items.Add(GraphicsSettings.InterpolationModes[value]);
            if (shown >= 0)
            {
                bool frameGenerationKept = (shown != 2 || ShownUpscaler == 3) && (shown != 3 || ShownUpscaler == 2 || ShownUpscaler == 4);
                interpolation.SelectedIndex = interpolationChoices.Contains(shown) && frameGenerationKept
                    ? interpolationChoices.IndexOf(shown) : interpolationChoices.IndexOf(1);
            }
            loading = wasLoading;
        }

        // The frame rate row for the interpolation chosen: its limit (30 or 60) without one, else its
        // target (the rates below the display's and the display's own).
        private void RefreshFrameRateChoices()
        {
            int want = frameRate.SelectedIndex >= 0 && frameRate.SelectedIndex < frameRateChoices.Count
                ? frameRateChoices[frameRate.SelectedIndex] : graphics.FrameRate;
            int interpolationMode = SelectedInterpolation;
            int refreshHz = SelectedMonitor != null ? SelectedMonitor.Refresh : 0;
            bool dlss = ShownUpscaler == 3;
            frameRateChoices.Clear();
            frameRateChoices.AddRange(GraphicsSettings.InterpolationTargets(interpolationMode, refreshHz, dlss));
            frameRateLabel.Text = interpolationMode == 0 ? NebulaText.Of("Frame rate limit") : NebulaText.Of("Target frame rate");
            frameRate.Items.Clear();
            int wanted = want == 0 ? (refreshHz != 0 ? refreshHz : 1000) : want;
            int selected = interpolationMode == 0 ? frameRateChoices.Count - 1 : 0;
            for (int i = 0; i < frameRateChoices.Count; i++)
            {
                int rate = frameRateChoices[i];
                frameRate.Items.Add(rate != 0 ? rate + " FPS"
                    : refreshHz != 0 ? NebulaText.Of("Match refresh rate ({0} Hz)", refreshHz) : NebulaText.Of("Match refresh rate"));
                int value = rate == 0 ? (refreshHz != 0 ? refreshHz : 1000) : rate;
                if (value <= wanted) selected = i;
            }
            frameRate.SelectedIndex = Math.Min(selected, frameRate.Items.Count - 1);
        }

        private bool Commit()
        {
            try
            {
                var screen = SelectedMonitor;
                graphics.Monitor = screen != null ? screen.Device : "";
                graphics.Gpu = SelectedGpuId;
                graphics.DisplayMode = Math.Max(0, mode.SelectedIndex);
                if (resolution.SelectedIndex >= 0 && resolution.SelectedIndex < resolutionChoices.Count)
                {
                    var size = resolutionChoices[resolution.SelectedIndex];
                    if (graphics.DisplayMode == 0) { graphics.WindowWidth = size.Width; graphics.WindowHeight = size.Height; }
                }
                graphics.Aspect = Math.Max(0, aspect.SelectedIndex);
                graphics.VSync = vsync.Checked;
                if (frameRowsChosen || !LaunchSession.RuntimeLeaseHeld())
                {
                    if (frameRate.SelectedIndex >= 0 && frameRate.SelectedIndex < frameRateChoices.Count)
                        graphics.FrameRate = frameRateChoices[frameRate.SelectedIndex];
                    if (upscaler.SelectedIndex >= 0 && upscaler.SelectedIndex < upscalerChoices.Count)
                        graphics.Upscaler = upscalerChoices[upscaler.SelectedIndex];
                    graphics.FrameGeneration = SelectedInterpolation == 2 ? 1 : SelectedInterpolation == 3 ? 2 : 0;
                    KeepQuality(ShownUpscaler);
                }
                graphics.InternalHeight = GraphicsSettings.InternalHeights[Math.Max(0, internalResolution.SelectedIndex)];
                graphics.AntiAliasing = antiAliasing.SelectedIndex;
                graphics.Quality = aaQuality.SelectedIndex;
                graphics.Msaa = Math.Max(0, msaa.SelectedIndex);
                graphics.Sharpness = (int)sharpness.Value;
                graphics.TextureFilter = textureFilter.SelectedIndex;
                graphics.EnhancedLighting = lighting.SelectedIndex == 0;
                graphics.PerPixelLighting = lighting.SelectedIndex <= 1;
                graphics.GeneratedMipmaps = mipmaps.Checked;
                graphics.OcclusionMode = Math.Max(0, occlusionMode.SelectedIndex);
                graphics.Bloom = (int)bloom.Value;
                graphics.CurvedSurfaces = curvedSurfaces.Checked;
                graphics.TexturePackMode = Math.Max(0, texturePacks.SelectedIndex);
                graphics.TextureDump = textureDump.Checked;
                graphics.Hdr = hdr.Checked;
                graphics.PaperWhite = (int)paperWhite.Value;
                // A fixed HUD row shows what the game does, not the player's choice: that is kept.
                graphics.HudOutputResolution = hudOutput.Enabled ? hudOutput.Checked : hudChosen;
                if (rayTracingOffered)
                {
                    graphics.RayTracing = rayTracing.Checked;
                    graphics.RtQuality = rtQuality.SelectedIndex < 0 ? 1 : GraphicsSettings.RtQualityOrder[rtQuality.SelectedIndex];
                }
                graphics.Save();
                for (int i = 0; i < volumes.Length; i++) audio.Percent[i] = volumes[i].Value;
                audio.SoundOutput = Math.Max(0, soundOutput.SelectedIndex);
                audio.Save();
                general.TextSpeed = Math.Max(0, textSpeed.SelectedIndex) + 1;
                general.Language = offeredLanguages[Math.Max(0, Math.Min(languageBox.SelectedIndex, offeredLanguages.Count - 1))];
                general.ButtonIcons = Math.Max(0, buttonIcons.SelectedIndex);
                general.MenuTextSize = menuTextSizes[Math.Max(0, Math.Min(menuTextSize.SelectedIndex, menuTextSizes.Count - 1))];
                general.FocusLoss = Math.Max(0, focusLoss.SelectedIndex);
                general.DiscordStatus = discordStatus.Checked;
                general.Save();
                EndCapture();
                if (controls.InputMode != ControlsSettings.ModeRealWiimote) controls.InputMode = Math.Max(0, inputSource.SelectedIndex);
                controls.CoStar = coStar.Checked;
                controls.Save();
                session.Recording = (RecordingMode)Math.Max(0, recording.SelectedIndex);
                if (!session.Portable) session.AskAboutUpdates = askUpdates.Checked;
                session.Save();
                return true;
            }
            catch (Exception error)
            {
                MessageBox.Show(this, NebulaText.Of("Settings could not be saved: {0}", error.Message), "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error);
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
                // Another disc's languages: the row lists them, its choice kept where the disc has it.
                int chosen = offeredLanguages[Math.Max(0, Math.Min(languageBox.SelectedIndex, offeredLanguages.Count - 1))];
                offeredLanguages = GeneralSettings.Offered(Path.Combine(session.Content, "game.pak"));
                SaveArchive.LooseTitle = SaveArchive.TitleOf(GameDisc.GameId(Path.Combine(session.Content, "game.pak")));
                languageBox.Items.Clear();
                foreach (int language in offeredLanguages) languageBox.Items.Add(GeneralSettings.LanguageNames[language]);
                languageBox.SelectedIndex = Math.Max(0, offeredLanguages.IndexOf(chosen));
                return true;
            }
        }

        private void ExportSaves()
        {
            using (var dialog = new FolderBrowserDialog { Description = NebulaText.Of("Choose a folder for the saves archive.") })
            {
                if (dialog.ShowDialog(this) != DialogResult.OK) return;
                try
                {
                    string archive = SaveArchive.Export(dialog.SelectedPath);
                    MessageBox.Show(this, NebulaText.Of("Saves exported to {0}.", archive), "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
                catch (Exception error) { MessageBox.Show(this, error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error); }
            }
        }

        // A zip from Export saves (or another zip of a saves folder), or a saves folder.
        private void ImportSaves(bool zip)
        {
            string source = null;
            if (zip)
            {
                using (var dialog = new OpenFileDialog { Title = NebulaText.Of("Import saves"), Filter = NebulaText.Of("Zip archives (*.zip)|*.zip"), CheckFileExists = true })
                    if (dialog.ShowDialog(this) == DialogResult.OK) source = dialog.FileName;
            }
            else
            {
                using (var dialog = new FolderBrowserDialog { Description = "A Nebula saves folder, or Dolphin's Super Mario Galaxy save folder (with GameData.bin)", ShowNewFolderButton = false })
                    if (dialog.ShowDialog(this) == DialogResult.OK) source = dialog.SelectedPath;
            }
            if (source == null) return;
            try
            {
                string message = SaveArchive.Import(source, delegate
                {
                    return MessageBox.Show(this, NebulaText.Of("Replace the current saves? They are moved to a backup folder."), "Nebula",
                                           MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
                });
                if (message != null) MessageBox.Show(this, message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception error) { MessageBox.Show(this, error.Message, "Nebula", MessageBoxButtons.OK, MessageBoxIcon.Error); }
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
            throw new IOException(NebulaText.Of("Could not create a new session folder."));
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
                    // Per-thread CPU beside the process-wide row, which cannot say which thread spends the time. The
                    // columns are those tools/session_report.ps1 reads from threads.csv.
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
                        // A thread can exit between the enumeration and the reads: it is skipped, not the whole sample. A
                        // thread that never ran reports zero CPU time, a real value.
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
                string result = NebulaText.Of("Recording saved to {0}", session);
                string saved = session;
                try
                {
                    System.IO.Compression.ZipFile.CreateFromDirectory(session, session + ".zip", System.IO.Compression.CompressionLevel.Optimal, false);
                    result = NebulaText.Of("Recording saved to {0}", session + ".zip");
                    saved = session + ".zip";
                }
                catch (Exception) { /* Logs remain available if ZIP creation fails. */ }
                if (process.ExitCode != 0) result = NebulaText.Of("The game exited with code {0}.", process.ExitCode) + "\r\n" + result;
                RecordingNotice.Show(result, process.ExitCode != 0, saved);
                Environment.Exit(0);
            });
            thread.IsBackground = false;
            thread.Start();
        }
    }
}
