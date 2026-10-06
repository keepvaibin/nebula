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
        private readonly CheckBox desktop = new CheckBox(), license = new CheckBox();
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
            Text = "Nebula Setup " + BuildInfo.Version;
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
            clockTimer.Interval = 1000;
            clockTimer.Tick += delegate { elapsed.Text = "Elapsed " + clock.Elapsed.ToString(@"hh\:mm\:ss"); };
            Shown += delegate
            {
                if (options.ContainsKey("uninstall")) { modeUninstall.Checked = true; }
                if (options.ContainsKey("update")) CheckForUpdate();
                else if (options.ContainsKey("apply-update") && installed != null) { modeRepair.Checked = true; StartWork(InstallMode.Update); }
            };
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
            AddLabel(choose, "Unofficial native PC port of Super Mario Galaxy (USA, RMGE01). Development preview.", 20, 50, 720, 40);

            int y = 98;
            if (installed != null)
            {
                AddLabel(choose, "Installed: Nebula " + installed.Version + (installed.Previous != null ? " (previous " + installed.Previous + " kept)" : ""), 20, y, 720, 22);
                y += 26;
                modeRepair.Text = "Repair this version";
                modeRepair.Checked = true;
                modeRestore.Text = "Restore the previous version" + (installed.Previous != null ? " (" + installed.Previous + ")" : " (none available)");
                modeRestore.Enabled = installed.Previous != null;
                modeUninstall.Text = "Uninstall (keeps saves and settings)";
                foreach (var radio in new[] { modeRepair, modeRestore, modeUninstall })
                {
                    radio.Location = new Point(28, y);
                    radio.Size = new Size(710, 24);
                    choose.Controls.Add(radio);
                    y += 26;
                }
                var update = new Button { Text = "Check for updates…", Location = new Point(28, y + 2), Size = new Size(170, 30) };
                update.Click += delegate { CheckForUpdate(); };
                choose.Controls.Add(update);
                y += 42;
            }
            else
            {
                modeInstall.Checked = true;
            }

            AddLabel(choose, "Your game: ISO, RVZ or extracted folder.", 20, y, 720, 22);
            y += 24;
            input.Location = new Point(20, y);
            input.Size = new Size(470, 26);
            choose.Controls.Add(input);
            var file = new Button { Text = "Image file…", Location = new Point(498, y - 1), Size = new Size(116, 28) };
            file.Click += delegate
            {
                using (var dialog = new OpenFileDialog { Title = "Choose your Super Mario Galaxy disc image", Filter = "Wii disc images (*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz)|*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz|All files (*.*)|*.*" })
                    if (dialog.ShowDialog(this) == DialogResult.OK) input.Text = dialog.FileName;
            };
            var folder = new Button { Text = "Folder…", Location = new Point(620, y - 1), Size = new Size(116, 28) };
            folder.Click += delegate
            {
                using (var dialog = new FolderBrowserDialog { Description = "Choose the extracted game folder" })
                    if (dialog.ShowDialog(this) == DialogResult.OK) input.Text = dialog.SelectedPath;
            };
            choose.Controls.Add(file);
            choose.Controls.Add(folder);
            y += 38;

            AddLabel(choose, "Install folder:", 20, y, 720, 22);
            y += 24;
            root.Text = layout.Root;
            root.Location = new Point(20, y);
            root.Size = new Size(594, 26);
            root.Enabled = installed == null;
            choose.Controls.Add(root);
            var browse = new Button { Text = "Browse…", Location = new Point(620, y - 1), Size = new Size(116, 28), Enabled = installed == null };
            browse.Click += delegate
            {
                using (var dialog = new FolderBrowserDialog { Description = "Choose where to install Nebula" })
                    if (dialog.ShowDialog(this) == DialogResult.OK) root.Text = Path.Combine(dialog.SelectedPath, "Nebula");
            };
            choose.Controls.Add(browse);
            y += 38;

            desktop.Text = "Create a desktop shortcut";
            desktop.Location = new Point(20, y);
            desktop.Size = new Size(700, 24);
            choose.Controls.Add(desktop);
            y += 28;

            var toolchain = new Toolchain(Json.Parse(Payload.ReadText("toolchain.json")), layout.Toolchains);
            license.Text = "I accept the Microsoft Visual Studio Build Tools license";
            license.Location = new Point(20, y);
            license.Size = new Size(620, 24);
            choose.Controls.Add(license);
            var link = new LinkLabel { Text = "Read license", Location = new Point(644, y + 3), Size = new Size(100, 22) };
            link.LinkClicked += delegate { Process.Start(toolchain.LicenseUrl); };
            choose.Controls.Add(link);
            y += 32;

            summary.Location = new Point(20, y);
            summary.Size = new Size(720, 170);
            summary.Text =
                "This is responsible for checking your game, recompiling it and compiling it into a Windows program on this PC. " +
                "It downloads a C++ compiler (" + FileUtil.FormatBytes(toolchain.DownloadBytes) + ").\r\n\r\n" +
                "Needs: Windows 10/11 64-bit, 8 GB RAM, " + FileUtil.FormatBytes(InstallEngine.RequiredFreeBytes) + " free disk space, a DirectX 12 GPU.";
            choose.Controls.Add(summary);

            start.Text = installed == null ? "Install" : "Continue";
            start.Location = new Point(530, 596);
            start.Size = new Size(100, 32);
            start.Click += delegate { OnStart(); };
            var close = new Button { Text = "Close", Location = new Point(640, 596), Size = new Size(100, 32) };
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
            cancelButton.Text = "Cancel";
            cancelButton.Location = new Point(640, 596);
            cancelButton.Size = new Size(100, 32);
            cancelButton.Click += delegate
            {
                if (cancel != null && MessageBox.Show(this, "Cancel setup?", "Nebula Setup", MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
                {
                    cancel.Cancel();
                    cancelButton.Enabled = false;
                    step.Text = "Canceling…";
                }
            };
            launch.Text = "Play Nebula";
            launch.Location = new Point(530, 596);
            launch.Size = new Size(100, 32);
            launch.Visible = false;
            launch.Click += delegate
            {
                if (engine != null && engine.InstalledVersionDir != null)
                    Process.Start(new ProcessStartInfo(Path.Combine(engine.InstalledVersionDir, "Nebula.exe")) { WorkingDirectory = engine.InstalledVersionDir });
                Close();
            };
            openLog.Text = "Open log";
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
                    var answer = MessageBox.Show(this, "Uninstall Nebula?\r\n\r\nYour saves, settings and shader cache in " + NebulaPaths.DataRoot +
                        " are kept. Choose No to also delete them.", "Uninstall Nebula", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);
                    if (answer == DialogResult.Cancel) return;
                    Integration.Uninstall(layout, answer == DialogResult.No);
                    MessageBox.Show(this, "Nebula was uninstalled.", "Nebula Setup");
                    Close();
                    return;
                }
                if (installed != null && modeRestore.Checked)
                {
                    string version = Integration.Rollback(layout);
                    MessageBox.Show(this, "Nebula " + version + " is active again.", "Nebula Setup");
                    Close();
                    return;
                }
                layout = new InstallLayout(root.Text.Trim());
                StartWork(installed == null ? InstallMode.Install : InstallMode.Repair);
            }
            catch (Exception error) { MessageBox.Show(this, error.Message, "Nebula Setup", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        }

        private void StartWork(InstallMode mode)
        {
            string game = input.Text.Trim();
            if (game.Length == 0) game = null;
            if (mode == InstallMode.Install && game == null) { Fail("Choose your Super Mario Galaxy ISO, RVZ or extracted folder."); return; }
            if (game != null && !File.Exists(game) && !Directory.Exists(game)) { Fail("The selected game was not found: " + game); return; }
            var toolchain = new Toolchain(Json.Parse(Payload.ReadText("toolchain.json")), layout.Toolchains);
            if (!toolchain.IsReady && !license.Checked)
            {
                if (mode == InstallMode.Update && MessageBox.Show(this, "This update needs the Microsoft Visual Studio Build Tools license. Accept it?\r\n" + toolchain.LicenseUrl,
                    "Nebula Setup", MessageBoxButtons.YesNo) == DialogResult.Yes) { }
                else { Fail("Accept the Microsoft Visual Studio Build Tools license to continue."); return; }
            }
            var problems = InstallEngine.CheckRequirements(layout.Root.Length > 3 ? Path.GetDirectoryName(layout.Root) ?? layout.Root : layout.Root);
            if (problems.Count > 0) { Fail(string.Join("\r\n", problems)); return; }

            var request = new InstallRequest
            {
                Mode = mode, GameInput = game, InstallRoot = layout.Root, DesktopShortcut = desktop.Checked,
                SourceOverride = options.ContainsKey("source") ? options["source"] : null,
                CompileJobs = options.ContainsKey("jobs") ? int.Parse(options["jobs"]) : 0
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
            cancelButton.Text = "Close";
            cancelButton.Enabled = true;
            cancel = null;
            cancelButton.Click += delegate { Close(); };
            if (failure == null)
            {
                step.Text = "Nebula " + BuildInfo.Version + " is installed and ready.";
                overall.Value = overall.Maximum;
                launch.Visible = true;
                return;
            }
            bool canceled = failure is OperationCanceledByUserException || failure is OperationCanceledException;
            step.Text = canceled ? "Canceled." : "Setup failed.";
            if (!canceled) MessageBox.Show(this, failure.Message, "Nebula Setup", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }

        private void Fail(string message) { MessageBox.Show(this, message, "Nebula Setup", MessageBoxButtons.OK, MessageBoxIcon.Warning); }

        private void CheckForUpdate()
        {
            try
            {
                Cursor = Cursors.WaitCursor;
                var release = Updater.FindNewer();
                Cursor = Cursors.Default;
                if (release == null) { MessageBox.Show(this, "Nebula " + BuildInfo.Version + " is the newest release.", "Nebula Update"); return; }
                string notes = release.Notes.Length > 1200 ? release.Notes.Substring(0, 1200) + "…" : release.Notes;
                if (MessageBox.Show(this, "Nebula " + release.Version + (release.Prerelease ? " (development preview)" : "") + " is available. You have " +
                    BuildInfo.Version + ".\r\n\r\n" + notes + "\r\n\r\nDownload and install it? Your current version stays available until the new one is ready.",
                    "Nebula Update", MessageBoxButtons.YesNo, MessageBoxIcon.Information) != DialogResult.Yes) return;
                Cursor = Cursors.WaitCursor;
                string exe = Updater.Download(release, layout.Staging, null, CancellationToken.None);
                Cursor = Cursors.Default;
                Process.Start(new ProcessStartInfo(exe, "--apply-update --root " + ProcessRunner.Quote(layout.Root)) { UseShellExecute = false });
                Close();
            }
            catch (Exception error)
            {
                Cursor = Cursors.Default;
                MessageBox.Show(this, "Could not check for updates: " + error.Message, "Nebula Update", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        // ---- IInstallProgress, called from the worker thread ----------------------

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
    }
}
