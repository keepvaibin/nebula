using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Setup
{
    /// <summary>The graphical setup: install, repair, update, restore and uninstall.</summary>
    internal sealed class SetupForm : Form, IInstallProgress
    {
        private readonly Dictionary<string, string> options;
        private InstallLayout layout;
        private InstalledInfo installed;
        private readonly Panel choose = new Panel(), work = new Panel();
        private readonly TextBox input = new TextBox(), root = new TextBox(), log = new TextBox();
        private readonly CheckBox desktop = new CheckBox(), dlss = new CheckBox(), dlssLicense = new CheckBox(),
            prepareRayTracing = new CheckBox(), lumaWindow = new CheckBox(), lumaCommand = new CheckBox(), lumaPath = new CheckBox();
        private readonly RadioButton modeRepair = new RadioButton(), modeRestore = new RadioButton(), modeUninstall = new RadioButton(), modeInstall = new RadioButton();
        private readonly Label summary = new Label(), step = new Label(), elapsed = new Label();
        private readonly ProgressBar overall = new ProgressBar(), stepBar = new ProgressBar();
        private readonly Button start = new Button(), cancelButton = new Button(), launch = new Button(), openLog = new Button();
        private CancellationTokenSource cancel;
        private InstallEngine engine;
        private readonly System.Windows.Forms.Timer clockTimer = new System.Windows.Forms.Timer();
        private readonly Stopwatch clock = new Stopwatch();

        public SetupForm(Dictionary<string, string> options)
        {
            this.options = options;
            Text = NebulaText.Of("Nebula Setup") + " " + BuildInfo.Version;
            Font = new Font("Segoe UI", 9.5f);

            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(760, 640);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            string rootPath = options.ContainsKey("root") ? options["root"] : (NebulaPaths.RegisteredInstallRoot ?? NebulaPaths.DefaultInstallRoot);
            layout = new InstallLayout(rootPath);
            installed = InstalledInfo.Read(layout);
            BuildChoosePage();
            BuildWorkPage();
            Controls.Add(choose);
            Controls.Add(work);
            work.Visible = false;
            UiScale.Apply(this);
            UpdateChoices();
            clockTimer.Interval = 1000;
            clockTimer.Tick += delegate { elapsed.Text = NebulaText.Of("Elapsed {0}", clock.Elapsed.ToString(@"hh\:mm\:ss")); };
            Shown += delegate
            {
                if (options.ContainsKey("uninstall")) { modeUninstall.Checked = true; }
                if (options.ContainsKey("update")) CheckForUpdate();
                // An update that needs the disc stays on this page, which asks for it; a package no newer than the
                // installed version is never applied as an update (no downgrade by --apply-update).
                else if (options.ContainsKey("apply-update") && installed != null && InstallEngine.RetainedGameValid(layout) &&
                         InstallIntent.ForPackage(installed.Version, BuildInfo.Version) == InstallMode.Update) { modeRepair.Checked = true; StartWork(InstallMode.Update); }
            };
        }

        /// <summary>The start button names the chosen action.</summary>
        private void UpdateChoices()
        {
            InstallMode mode = installed == null ? InstallMode.Install : InstallIntent.ForPackage(installed.Version, BuildInfo.Version);
            start.Text = installed == null ? NebulaText.Of("Install") : modeRestore.Checked ? NebulaText.Of("Restore") : modeUninstall.Checked ? NebulaText.Of("Uninstall") :
                mode == InstallMode.Update ? NebulaText.Of("Update") : NebulaText.Of("Repair");
        }

        private Label AddLabel(Control parent, string text, int x, int y, int width, int height)
        {
            var label = new Label { Text = text, Location = new Point(x, y), Size = new Size(width, height) };
            parent.Controls.Add(label);
            return label;
        }

        private void BuildChoosePage()
        {
            choose.Dock = DockStyle.Fill;
            var title = AddLabel(choose, "Nebula", 20, 14, 700, 34);
            title.Font = new Font("Segoe UI Semibold", 17f);
            AddLabel(choose, NebulaText.Of("Unofficial native PC port of Super Mario Galaxy (North America, Europe and Japan)."), 20, 50, 720, 40);

            int y = 98;
            if (installed != null)
            {
                AddLabel(choose, NebulaText.Of("Installed: Nebula {0}", installed.Version) + (installed.Previous != null ? NebulaText.Of(" (previous {0} kept)", installed.Previous) : ""), 20, y, 720, 22);
                y += 26;
                modeRepair.Text = InstallIntent.ForPackage(installed.Version, BuildInfo.Version) == InstallMode.Update
                    ? NebulaText.Of("Update to Nebula {0} (keeps saves and settings)", BuildInfo.Version)
                    : NebulaText.Of("Install or repair Nebula {0}", BuildInfo.Version);
                modeRepair.Checked = true;
                modeRestore.Text = NebulaText.Of("Restore the previous version ({0})", installed.Previous);
                modeUninstall.Text = NebulaText.Of("Uninstall (keeps saves and settings)");
                foreach (var radio in new[] { modeRepair, modeRestore, modeUninstall })
                {
                    if (radio == modeRestore && installed.Previous == null) continue;
                    radio.Location = new Point(28, y);
                    radio.Size = new Size(710, 24);
                    choose.Controls.Add(radio);
                    y += 26;
                }
                var update = new Button { Text = NebulaText.Of("Check for updates…"), Location = new Point(28, y + 2), Size = new Size(170, 30) };
                update.Click += delegate { CheckForUpdate(); };
                choose.Controls.Add(update);
                y += 42;
            }
            else
            {
                modeInstall.Checked = true;
            }

            // An install keeps its packed game; the game is asked for only when that is gone.
            if (installed == null || !InstallEngine.RetainedGameValid(layout))
            {
                AddLabel(choose, installed == null ? NebulaText.Of("Your game: ISO, RVZ or extracted folder.")
                                                   : NebulaText.Of("Your game: ISO, RVZ or extracted folder (this version needs to read it again)."),
                         20, y, 720, 22);
                y += 24;
                input.Location = new Point(20, y);
                input.Size = new Size(470, 26);
                choose.Controls.Add(input);
                var file = new Button { Text = NebulaText.Of("Image file…"), Location = new Point(498, y - 1), Size = new Size(116, 28) };
                file.Click += delegate
                {
                    using (var dialog = new OpenFileDialog { Title = NebulaText.Of("Choose your Super Mario Galaxy disc image"), Filter = NebulaText.Of("Wii disc images (*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz)|*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz|All files (*.*)|*.*") })
                        if (dialog.ShowDialog(this) == DialogResult.OK) input.Text = dialog.FileName;
                };
                var folder = new Button { Text = NebulaText.Of("Folder…"), Location = new Point(620, y - 1), Size = new Size(116, 28) };
                folder.Click += delegate
                {
                    using (var dialog = new FolderBrowserDialog { Description = NebulaText.Of("Choose the extracted game folder") })
                        if (dialog.ShowDialog(this) == DialogResult.OK) input.Text = dialog.SelectedPath;
                };
                choose.Controls.Add(file);
                choose.Controls.Add(folder);
                y += 38;
            }

            root.Text = layout.Root;
            if (installed == null)
            {
                AddLabel(choose, NebulaText.Of("Install folder:"), 20, y, 720, 22);
                y += 24;
                root.Location = new Point(20, y);
                root.Size = new Size(594, 26);
                choose.Controls.Add(root);
                var browse = new Button { Text = NebulaText.Of("Browse…"), Location = new Point(620, y - 1), Size = new Size(116, 28) };
                browse.Click += delegate
                {
                    using (var dialog = new FolderBrowserDialog { Description = NebulaText.Of("Choose where to install Nebula") })
                        if (dialog.ShowDialog(this) == DialogResult.OK) root.Text = Path.Combine(dialog.SelectedPath, "Nebula");
                };
                choose.Controls.Add(browse);
                y += 38;
            }
            else
            {
                AddLabel(choose, NebulaText.Of("Install folder: {0}", layout.Root), 20, y, 720, 22);
                y += 32;
            }

            desktop.Text = NebulaText.Of("Create a desktop shortcut");
            desktop.Location = new Point(20, y);
            desktop.Size = new Size(700, 24);
            choose.Controls.Add(desktop);
            y += 28;

            // LumaLink, the mod tools: the window and the command line, each a choice; the command line on PATH if asked.
            bool lumaLinkWindow, lumaLinkCommand, lumaLinkPath;
            Integration.LumaLinkChoices(out lumaLinkWindow, out lumaLinkCommand, out lumaLinkPath);
            lumaWindow.Text = NebulaText.Of("LumaLink window");
            lumaWindow.Location = new Point(20, y);
            lumaWindow.Size = new Size(340, 24);
            lumaWindow.Checked = lumaLinkWindow;
            choose.Controls.Add(lumaWindow);
            lumaCommand.Text = NebulaText.Of("LumaLink command line");
            lumaCommand.Location = new Point(380, y);
            lumaCommand.Size = new Size(360, 24);
            lumaCommand.Checked = lumaLinkCommand;
            choose.Controls.Add(lumaCommand);
            y += 26;
            lumaPath.Text = NebulaText.Of("Add LumaLink to PATH");
            lumaPath.Location = new Point(400, y);
            lumaPath.Size = new Size(340, 24);
            lumaPath.Checked = lumaLinkPath && lumaLinkCommand;
            lumaPath.Enabled = lumaLinkCommand;
            lumaCommand.CheckedChanged += delegate
            {
                lumaPath.Enabled = lumaCommand.Checked;
                if (!lumaCommand.Checked) lumaPath.Checked = false;
            };
            choose.Controls.Add(lumaPath);
            y += 30;

            if (RayTracingPreparation.Offered())
            {
                prepareRayTracing.Text = NebulaText.Of("Prepare ray tracing shaders");
                prepareRayTracing.Location = new Point(20, y);
                prepareRayTracing.Size = new Size(700, 24);
                choose.Controls.Add(prepareRayTracing);
                y += 28;
            }

            if (DlssPackage.RtxAdapterPresent() && !DlssPackage.Installed())
            {
                dlss.Text = NebulaText.Of("Install NVIDIA DLSS ({0} download from NVIDIA)", FileUtil.FormatBytes(DlssPackage.DownloadBytes));
                dlss.Location = new Point(20, y);
                dlss.Size = new Size(620, 24);
                choose.Controls.Add(dlss);
                y += 26;
                dlssLicense.Text = NebulaText.Of("I accept the NVIDIA RTX SDKs license");
                dlssLicense.Location = new Point(40, y);
                dlssLicense.Size = new Size(600, 24);
                dlssLicense.Enabled = false;
                dlssLicense.CheckedChanged += delegate { dlssLicense.ForeColor = SystemColors.ControlText; };
                dlss.CheckedChanged += delegate
                {
                    dlssLicense.Enabled = dlss.Checked;
                    if (!dlss.Checked) { dlssLicense.Checked = false; dlssLicense.ForeColor = SystemColors.ControlText; }
                };
                choose.Controls.Add(dlssLicense);
                var dlssLink = new LinkLabel { Text = NebulaText.Of("Read license"), Location = new Point(644, y + 3), Size = new Size(100, 22) };
                dlssLink.LinkClicked += delegate { using (var dialog = new VendorInstallDialog(DlssPackage.Package, true)) dialog.ShowDialog(this); };
                choose.Controls.Add(dlssLink);
                y += 32;
            }

            summary.Location = new Point(20, y);
            summary.Size = new Size(720, Math.Max(40, 590 - y));
            summary.Text = NebulaText.Of("Requires Windows 10 or 11 (64-bit), 8 GB of RAM, {0} of free disk space and a DirectX 12 GPU.", FileUtil.FormatBytes(InstallEngine.RequiredFreeBytes));
            choose.Controls.Add(summary);
            foreach (var radio in new[] { modeRepair, modeRestore, modeUninstall })
                radio.CheckedChanged += delegate { UpdateChoices(); };

            start.Location = new Point(530, 596);
            start.Size = new Size(100, 32);
            start.Click += delegate { OnStart(); };
            var close = new Button { Text = NebulaText.Of("Close"), Location = new Point(640, 596), Size = new Size(100, 32) };
            close.Click += delegate { Close(); };
            choose.Controls.Add(start);
            choose.Controls.Add(close);
            AcceptButton = start;
        }

        private void BuildWorkPage()
        {
            work.Dock = DockStyle.Fill;
            step.Location = new Point(20, 20);
            step.Size = new Size(720, 24);
            step.Font = new Font("Segoe UI Semibold", 10.5f);
            overall.Location = new Point(20, 50);
            overall.Size = new Size(720, 22);
            overall.Maximum = 1000;
            stepBar.Location = new Point(20, 80);
            stepBar.Size = new Size(720, 14);
            stepBar.Maximum = 1000;
            elapsed.Location = new Point(20, 100);
            elapsed.Size = new Size(720, 22);
            log.Location = new Point(20, 126);
            log.Size = new Size(720, 460);
            log.Multiline = true;
            log.ReadOnly = true;
            log.ScrollBars = ScrollBars.Vertical;
            log.Font = new Font("Consolas", 8.5f);
            cancelButton.Text = NebulaText.Of("Cancel");
            cancelButton.Location = new Point(640, 596);
            cancelButton.Size = new Size(100, 32);
            cancelButton.Click += delegate
            {
                if (cancel != null && MessageBox.Show(this, NebulaText.Of("Cancel setup?"), NebulaText.Of("Nebula Setup"), MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
                {
                    cancel.Cancel();
                    cancelButton.Enabled = false;
                    step.Text = NebulaText.Of("Canceling…");
                }
            };
            launch.Text = NebulaText.Of("Play Nebula");
            launch.Location = new Point(530, 596);
            launch.Size = new Size(100, 32);
            launch.Visible = false;
            launch.Click += delegate
            {
                if (engine != null && engine.InstalledVersionDir != null)
                    Process.Start(new ProcessStartInfo(Path.Combine(engine.InstalledVersionDir, "Nebula.exe")) { WorkingDirectory = engine.InstalledVersionDir });
                Close();
            };
            openLog.Text = NebulaText.Of("Open log");
            openLog.Location = new Point(20, 596);
            openLog.Size = new Size(100, 32);
            openLog.Click += delegate { if (engine != null) Process.Start("notepad.exe", "\"" + engine.LogPath + "\""); };
            foreach (Control control in new Control[] { step, overall, stepBar, elapsed, log, cancelButton, launch, openLog }) work.Controls.Add(control);
        }

        private void OnStart()
        {
            try
            {
                if (installed != null && modeUninstall.Checked)
                {
                    var answer = MessageBox.Show(this, NebulaText.Of("Uninstall Nebula?") + "\r\n\r\n" + NebulaText.Of("Your saves, settings and shader cache in {0} are kept. Choose No to also delete them.", NebulaPaths.DataRoot), NebulaText.Of("Uninstall Nebula"), MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
                    if (answer == DialogResult.Cancel) return;
                    Integration.Uninstall(layout, answer == DialogResult.No);
                    MessageBox.Show(this, NebulaText.Of("Nebula was uninstalled."), NebulaText.Of("Nebula Setup"));
                    Close();
                    return;
                }
                if (installed != null && modeRestore.Checked)
                {
                    string version = Integration.Rollback(layout);
                    MessageBox.Show(this, NebulaText.Of("Nebula {0} is active again.", version), NebulaText.Of("Nebula Setup"));
                    Close();
                    return;
                }
                layout = new InstallLayout(root.Text.Trim());
                installed = InstalledInfo.Read(layout);
                StartWork(InstallIntent.ForPackage(installed == null ? null : installed.Version, BuildInfo.Version));
            }
            catch (Exception error) { MessageBox.Show(this, error.Message, NebulaText.Of("Nebula Setup"), MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        private void StartWork(InstallMode mode)
        {
            string game = input.Text.Trim();
            if (game.Length == 0) game = null;
            if (mode == InstallMode.Install && game == null) { Fail(NebulaText.Of("Choose your Super Mario Galaxy ISO, RVZ or extracted folder.")); return; }
            if (game != null && !File.Exists(game) && !Directory.Exists(game)) { Fail(NebulaText.Of("The selected game was not found: {0}", game)); return; }
            if (dlss.Checked && !dlssLicense.Checked)
            {
                dlssLicense.ForeColor = Color.Firebrick;
                dlssLicense.Focus();
                Fail(NebulaText.Of("Accept the NVIDIA RTX SDKs license to install DLSS, or clear the DLSS option.")); return;
            }
            var problems = InstallEngine.CheckRequirements(layout.Root.Length > 3 ? Path.GetDirectoryName(layout.Root) ?? layout.Root : layout.Root);
            if (problems.Count > 0) { Fail(string.Join("\r\n", problems)); return; }

            var request = new InstallRequest
            {
                Mode = mode, GameInput = game, InstallRoot = layout.Root, DesktopShortcut = desktop.Checked, InstallDlss = dlss.Checked,
                LumaLinkWindow = lumaWindow.Checked, LumaLinkCommand = lumaCommand.Checked, LumaLinkPath = lumaPath.Checked && lumaCommand.Checked
            };
            choose.Visible = false;
            work.Visible = true;
            cancel = new CancellationTokenSource();
            clock.Start();
            clockTimer.Start();
            var thread = new Thread(delegate()
            {
                Exception failure = null;
                try
                {
                    engine = new InstallEngine(request, this, cancel.Token);
                    engine.Run();
                }
                catch (Exception error) { failure = error; }
                BeginInvoke((Action)delegate { Finished(failure); });
            });
            thread.IsBackground = true;
            thread.Start();
        }

        private void Finished(Exception failure)
        {
            clockTimer.Stop();
            cancelButton.Text = NebulaText.Of("Close");
            cancelButton.Enabled = true;
            cancel = null;
            cancelButton.Click += delegate { Close(); };
            if (failure == null)
            {
                step.Text = NebulaText.Of("Nebula {0} is installed.", BuildInfo.Version);
                if (prepareRayTracing.Checked) RayTracingPreparation.Request();
                if (engine.DlssFailure != null)
                    MessageBox.Show(this, NebulaText.Of("NVIDIA DLSS was not installed: {0}", engine.DlssFailure) + "\r\n\r\n" + NebulaText.Of("You can install it later from the game's graphics settings."),
                        NebulaText.Of("Nebula Setup"), MessageBoxButtons.OK, MessageBoxIcon.Warning);
                overall.Value = overall.Maximum;
                launch.Visible = true;
                if (options.ContainsKey("launch") && engine.InstalledVersionDir != null)
                {
                    Process.Start(new ProcessStartInfo(Path.Combine(engine.InstalledVersionDir, "Nebula.exe")) { WorkingDirectory = engine.InstalledVersionDir });
                    Close();
                }
                return;
            }
            bool canceled = failure is OperationCanceledByUserException || failure is OperationCanceledException;
            step.Text = canceled ? NebulaText.Of("Canceled.") : NebulaText.Of("Setup failed.");
            if (!canceled) MessageBox.Show(this, failure.Message, NebulaText.Of("Nebula Setup"), MessageBoxButtons.OK, MessageBoxIcon.Error);
        }

        private void Fail(string message) { MessageBox.Show(this, message, NebulaText.Of("Nebula Setup"), MessageBoxButtons.OK, MessageBoxIcon.Warning); }

        private void CheckForUpdate()
        {
            try
            {
                Cursor = Cursors.WaitCursor;
                var release = Updater.FindNewer();
                Cursor = Cursors.Default;
                if (release == null) { MessageBox.Show(this, NebulaText.Of("Nebula {0} is the newest release.", BuildInfo.Version), NebulaText.Of("Nebula Update")); return; }
                string notes = ReleaseNotes.Plain(release.Notes, 1200);
                if (MessageBox.Show(this, (release.Prerelease ? NebulaText.Of("Nebula {0} (prerelease) is available. You have {1}.", release.Version, BuildInfo.Version) : NebulaText.Of("Nebula {0} is available. You have {1}.", release.Version, BuildInfo.Version)) +
                    "\r\n\r\n" + notes + "\r\n\r\n" + NebulaText.Of("Download and install it? Your current version stays available until the new one is ready."),
                    NebulaText.Of("Nebula Update"), MessageBoxButtons.YesNo, MessageBoxIcon.Information) != DialogResult.Yes) return;
                Cursor = Cursors.WaitCursor;
                string exe = Updater.Download(release, layout.Staging, null, CancellationToken.None);
                Cursor = Cursors.Default;
                Process.Start(new ProcessStartInfo(exe, "--apply-update --root " + ProcessRunner.Quote(layout.Root)) { UseShellExecute = false });
                Close();
            }
            catch (Exception error)
            {
                Cursor = Cursors.Default;
                MessageBox.Show(this, NebulaText.Of("Could not check for updates: {0}", error.Message), NebulaText.Of("Nebula Update"), MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        // IInstallProgress, called from the worker thread.

        public void Report(double fraction, string text, double stepFraction)
        {
            BeginInvoke((Action)delegate
            {
                overall.Value = (int)Math.Max(0, Math.Min(1000, fraction * 1000));
                step.Text = text;
                stepBar.Style = stepFraction < 0 ? ProgressBarStyle.Marquee : ProgressBarStyle.Continuous;
                if (stepFraction >= 0) stepBar.Value = (int)Math.Max(0, Math.Min(1000, stepFraction * 1000));
            });
        }

        public void Log(string line)
        {
            BeginInvoke((Action)delegate
            {
                if (log.TextLength > 200000) log.Text = log.Text.Substring(100000);
                log.AppendText(line + "\r\n");
            });
        }

        public bool AskToClose(string message)
        {
            return (bool)Invoke((Func<bool>)delegate
            {
                return MessageBox.Show(this, message, NebulaText.Of("Nebula Setup"), MessageBoxButtons.RetryCancel, MessageBoxIcon.Warning) == DialogResult.Retry;
            });
        }
    }
}
