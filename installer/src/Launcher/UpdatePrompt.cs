using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    /// <summary>
    /// The update offer (UpdateService decides when Nebula looks and what it offers). A start offers the newer release
    /// the check found, fresh or from the last start, before the game starts; with the settings open it shows as a
    /// banner there. Update downloads the release's Setup, checks its signature and version and runs it, which
    /// installs the release (and starts the game when offered at a start); Not now asks again at the next start; Skip
    /// this version asks again for a newer one. The launcher's Advanced tab turns the service off.
    /// </summary>
    internal static class UpdateCheck
    {
        internal static bool Enabled(LaunchSession session) { return !session.Portable && session.AskAboutUpdates; }

        /// <summary>The release to offer now without waiting: the check's answer if it has come, else the last one.</summary>
        internal static ReleaseInfo Pending(LaunchSession session)
        {
            return Enabled(session) ? UpdateService.Offer(0, session.SkippedUpdate) : null;
        }

        /// <summary>True when an update was started: it starts the game itself once installed.</summary>
        public static bool OfferBeforeLaunch(LaunchSession session)
        {
            if (!Enabled(session)) return false;
            return Prompt(session, UpdateService.Offer(UpdateService.FreshBudgetMilliseconds, session.SkippedUpdate), null, true);
        }

        /// <summary>Shows the offer of <paramref name="release"/>; true when its Setup was started (with
        /// <paramref name="launch"/>, it starts the game once installed).</summary>
        public static bool Prompt(LaunchSession session, ReleaseInfo release, IWin32Window owner, bool launch)
        {
            if (release == null) return false;
            UpdateChoice choice;
            bool prepareRayTracing;
            using (var prompt = new UpdatePromptForm(release))
            {
                if (owner != null) prompt.ShowDialog(owner); else prompt.ShowDialog();
                choice = prompt.Choice;
                prepareRayTracing = prompt.PrepareRayTracing;
            }
            if (choice == UpdateChoice.Skip)
            {
                session.SkippedUpdate = release.Version;
                try { session.Save(); } catch (Exception) { }
                return false;
            }
            if (choice != UpdateChoice.Update) return false;
            string setup;
            using (var download = new UpdateDownloadForm(release, new InstallLayout(session.Root).Staging))
            {
                if ((owner != null ? download.ShowDialog(owner) : download.ShowDialog()) != DialogResult.OK) return false;
                setup = download.SetupPath;
            }
            if (prepareRayTracing) RayTracingPreparation.Request();
            Process.Start(new ProcessStartInfo(setup, "--apply-update " + (launch ? "--launch " : "") + "--root " + ProcessRunner.Quote(session.Root))
            {
                UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(setup)
            });
            return true;
        }
    }

    internal enum UpdateChoice { Later, Update, Skip }

    internal sealed class UpdatePromptForm : Form
    {
        private readonly CheckBox prepare = new CheckBox();

        public UpdateChoice Choice { get; private set; }
        public bool PrepareRayTracing { get { return prepare.Checked; } }

        internal void Choose(UpdateChoice choice)
        {
            Choice = choice;
            DialogResult = choice == UpdateChoice.Update ? DialogResult.Yes : DialogResult.No;
            Close();
        }

        public UpdatePromptForm(ReleaseInfo release)
        {
            Text = NebulaText.Of("Nebula update");
            Font = new Font("Segoe UI", 9.5f);
            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(560, 320);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            TopMost = true;

            Controls.Add(new Label
            {
                Text = NebulaText.Of("Nebula {0} is available", release.Version),
                Font = new Font("Segoe UI", 11f, FontStyle.Bold), Location = new Point(16, 14), Size = new Size(528, 26)
            });
            if (release.SetupSize > 0)
            {
                Controls.Add(new Label
                {
                    Text = NebulaText.Of("Download: {0}", FileUtil.FormatBytes(release.SetupSize)),
                    ForeColor = SystemColors.GrayText, Location = new Point(16, 40), Size = new Size(528, 20)
                });
            }
            bool offerRayTracing = RayTracingPreparation.Offered();
            string notes = ReleaseNotes.Plain(release.Notes, 1500);
            Controls.Add(new TextBox
            {
                Text = notes, Multiline = true, ReadOnly = true,
                ScrollBars = ScrollBars.Vertical, Location = new Point(16, 64), Size = new Size(528, offerRayTracing ? 168 : 196),
                TabStop = false
            });
            if (offerRayTracing)
            {
                prepare.Text = NebulaText.Of("Prepare ray tracing shaders");
                prepare.Location = new Point(16, 240);
                prepare.Size = new Size(528, 24);
                Controls.Add(prepare);
            }
            var skip = new Button { Text = NebulaText.Of("Skip this version"), Location = new Point(16, 274), Size = new Size(200, 32) };
            skip.Click += delegate { Choose(UpdateChoice.Skip); };
            var update = new Button { Text = NebulaText.Of("Update"), Location = new Point(316, 274), Size = new Size(112, 32) };
            update.Click += delegate { Choose(UpdateChoice.Update); };
            var later = new Button { Text = NebulaText.Of("Not now"), Location = new Point(432, 274), Size = new Size(112, 32) };
            later.Click += delegate { Choose(UpdateChoice.Later); };
            Controls.Add(skip);
            Controls.Add(update);
            Controls.Add(later);
            AcceptButton = update;
            CancelButton = later;
            ActiveControl = update;
            new PadNavigator(this);
            UiScale.Apply(this);
        }
    }

    internal sealed class UpdateDownloadForm : Form
    {
        private readonly ProgressBar bar = new ProgressBar();
        private readonly Label status = new Label();
        private readonly CancellationTokenSource cancel = new CancellationTokenSource();
        private readonly ReleaseInfo release;
        private readonly string folder;

        public string SetupPath { get; private set; }

        public UpdateDownloadForm(ReleaseInfo release, string folder)
        {
            this.release = release;
            this.folder = folder;
            Text = NebulaText.Of("Nebula update");
            Font = new Font("Segoe UI", 9.5f);
            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(440, 130);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            TopMost = true;
            status.Text = NebulaText.Of("Downloading Nebula {0}", release.Version);
            status.Location = new Point(16, 14);
            status.Size = new Size(408, 22);
            bar.Location = new Point(16, 44);
            bar.Size = new Size(408, 22);
            bar.Maximum = 1000;
            var stop = new Button { Text = NebulaText.Of("Cancel"), Location = new Point(324, 84), Size = new Size(100, 32) };
            stop.Click += delegate { cancel.Cancel(); };
            Controls.Add(status);
            Controls.Add(bar);
            Controls.Add(stop);
            CancelButton = stop;
            ActiveControl = stop;
            new PadNavigator(this);
            UiScale.Apply(this);
            Shown += delegate { Start(); };
        }

        private void Start()
        {
            var thread = new Thread(delegate()
            {
                try
                {
                    string path = Updater.Download(release, folder, bytes =>
                    {
                        if (release.SetupSize <= 0) return;
                        BeginInvoke((Action)delegate { bar.Value = (int)Math.Min(1000, bytes * 1000 / release.SetupSize); });
                    }, cancel.Token);
                    BeginInvoke((Action)delegate { SetupPath = path; DialogResult = DialogResult.OK; Close(); });
                }
                catch (Exception error)
                {
                    bool canceled = cancel.IsCancellationRequested;
                    BeginInvoke((Action)delegate
                    {
                        if (!canceled)
                            MessageBox.Show(this, NebulaText.Of("Couldn't download the update.") + "\r\n\r\n" + error.Message,
                                NebulaText.Of("Nebula update"), MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        DialogResult = DialogResult.Cancel;
                        Close();
                    });
                }
            });
            thread.IsBackground = true;
            thread.Start();
        }
    }
}
