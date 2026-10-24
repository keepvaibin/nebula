using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    /// <summary>
    /// The update offer at each start: a newer release on GitHub is offered before the game starts; offline, or when
    /// GitHub does not answer within a few seconds, the game starts without a word. Update downloads the release's
    /// Setup, checks its signature and runs it, which installs the release and starts the game. "Don't ask again"
    /// applies only to "Not now"; the launcher's Advanced tab turns the offer back on.
    /// </summary>
    internal static class UpdateCheck
    {
        private const int CheckMilliseconds = 3000;

        /// <summary>True when an update was started: it starts the game itself once installed.</summary>
        public static bool OfferBeforeLaunch(LaunchSession session)
        {
            if (session.Portable || !session.AskAboutUpdates) return false;
            ReleaseInfo release;
            try
            {
                var check = Task.Run(() => Updater.FindNewer(CheckMilliseconds));
                if (!check.Wait(CheckMilliseconds + 500)) return false;
                release = check.Result;
            }
            catch (Exception) { return false; }
            if (release == null) return false;
            using (var prompt = new UpdatePromptForm(release))
            {
                if (prompt.ShowDialog() != DialogResult.Yes)
                {
                    if (prompt.DontAskAgain)
                    {
                        session.AskAboutUpdates = false;
                        session.Save();
                    }
                    return false;
                }
            }
            string setup;
            using (var download = new UpdateDownloadForm(release, new InstallLayout(session.Root).Staging))
            {
                if (download.ShowDialog() != DialogResult.OK) return false;
                setup = download.SetupPath;
            }
            Process.Start(new ProcessStartInfo(setup, "--apply-update --launch --root " + ProcessRunner.Quote(session.Root))
            {
                UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(setup)
            });
            return true;
        }
    }

    internal sealed class UpdatePromptForm : Form
    {
        private readonly CheckBox dontAsk = new CheckBox();

        public bool DontAskAgain { get { return dontAsk.Checked; } }

        // Update never turns the offer off, whatever the box says; Not now keeps the box.
        internal void Choose(bool update)
        {
            if (update) dontAsk.Checked = false;
            DialogResult = update ? DialogResult.Yes : DialogResult.No;
            Close();
        }

        public UpdatePromptForm(ReleaseInfo release)
        {
            Text = "Nebula update";
            Font = new Font("Segoe UI", 9.5f);
            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(520, 290);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            TopMost = true;

            Controls.Add(new Label
            {
                Text = "Nebula " + release.Version + " is available",
                Font = new Font("Segoe UI", 11f, FontStyle.Bold), Location = new Point(16, 14), Size = new Size(488, 26)
            });
            string notes = release.Notes.Length > 1500 ? release.Notes.Substring(0, 1500) + "…" : release.Notes;
            Controls.Add(new TextBox
            {
                Text = notes.Replace("\r\n", "\n").Replace("\n", "\r\n"), Multiline = true, ReadOnly = true,
                ScrollBars = ScrollBars.Vertical, Location = new Point(16, 46), Size = new Size(488, 190), TabStop = false
            });
            dontAsk.Text = "Don't ask again";
            dontAsk.Location = new Point(16, 250);
            dontAsk.Size = new Size(200, 24);
            Controls.Add(dontAsk);
            var update = new Button { Text = "Update", Location = new Point(300, 246), Size = new Size(100, 32) };
            update.Click += delegate { Choose(true); };
            var later = new Button { Text = "Not now", Location = new Point(404, 246), Size = new Size(100, 32) };
            later.Click += delegate { Choose(false); };
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
            Text = "Nebula update";
            Font = new Font("Segoe UI", 9.5f);
            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(440, 130);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            TopMost = true;
            status.Text = "Downloading Nebula " + release.Version;
            status.Location = new Point(16, 14);
            status.Size = new Size(408, 22);
            bar.Location = new Point(16, 44);
            bar.Size = new Size(408, 22);
            bar.Maximum = 1000;
            var stop = new Button { Text = "Cancel", Location = new Point(324, 84), Size = new Size(100, 32) };
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
                            MessageBox.Show(this, "Couldn't download the update.\r\n\r\n" + error.Message,
                                "Nebula update", MessageBoxButtons.OK, MessageBoxIcon.Warning);
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
