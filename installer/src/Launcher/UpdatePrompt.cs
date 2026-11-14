using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    /// <summary>
    /// The update service. Nothing touches the network before the game starts: a start offers the newer release an
    /// earlier check found (update-check.json in the data folder), and once the game has started the launcher reads
    /// the release list behind it for the next start (at most once an hour; offline, a captive portal, a rate limit
    /// or a malformed answer leave the last answer as it was). Update downloads the release's Setup, checks its
    /// signature and version and runs it, which installs the release and starts the game; Not now asks again at the
    /// next start; Skip this version asks again for a newer one. The launcher's Advanced tab turns the service off.
    /// </summary>
    internal static class UpdateCheck
    {
        private const int CheckMilliseconds = 15000;
        private static readonly TimeSpan CheckInterval = TimeSpan.FromHours(1);

        internal static string CachePath { get { return Path.Combine(NebulaPaths.DataRoot, "update-check.json"); } }

        /// <summary>The release to offer at this start, from the last check; null for none.</summary>
        internal static ReleaseInfo Pending(LaunchSession session)
        {
            if (session.Portable || !session.AskAboutUpdates) return null;
            ReleaseInfo release;
            DateTime checkedUtc;
            if (!ReadCache(out release, out checkedUtc) || release == null) return null;
            // A check made by an older install, or on the other channel, offers nothing this install would take.
            if (Updater.CompareVersions(release.Version, BuildInfo.Version) <= 0 ||
                Updater.IsBetaVersion(release.Version) != Updater.IsBetaVersion(BuildInfo.Version)) return null;
            if (!string.IsNullOrEmpty(session.SkippedUpdate) && Updater.CompareVersions(release.Version, session.SkippedUpdate) <= 0) return null;
            return release;
        }

        /// <summary>True when an update was started: it starts the game itself once installed.</summary>
        public static bool OfferBeforeLaunch(LaunchSession session)
        {
            ReleaseInfo release = Pending(session);
            if (release == null) return false;
            UpdateChoice choice;
            bool prepareRayTracing;
            using (var prompt = new UpdatePromptForm(release))
            {
                prompt.ShowDialog();
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
                if (download.ShowDialog() != DialogResult.OK) return false;
                setup = download.SetupPath;
            }
            if (prepareRayTracing) RayTracingPreparation.Request();
            Process.Start(new ProcessStartInfo(setup, "--apply-update --launch --root " + ProcessRunner.Quote(session.Root))
            {
                UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(setup)
            });
            return true;
        }

        /// <summary>
        /// Reads the release list and keeps the answer for the next start; called once the game has started or while
        /// the settings are open, never before Play. A check dated after now (a clock set wrong) is checked again.
        /// </summary>
        public static void Refresh(LaunchSession session)
        {
            if (session.Portable || !session.AskAboutUpdates) return;
            try
            {
                ReleaseInfo cached;
                DateTime checkedUtc, now = DateTime.UtcNow;
                if (ReadCache(out cached, out checkedUtc) && checkedUtc <= now && now - checkedUtc < CheckInterval) return;
                WriteCache(Updater.FindNewer(CheckMilliseconds), now);
            }
            catch (Exception) { }
        }

        internal static void WriteCache(ReleaseInfo release, DateTime checkedUtc)
        {
            var values = new Dictionary<string, object> { { "checkedUtc", checkedUtc.ToString("o", CultureInfo.InvariantCulture) } };
            if (release != null)
            {
                values["version"] = release.Version;
                values["tag"] = release.Tag;
                values["notes"] = release.Notes;
                values["setupUrl"] = release.SetupUrl;
                values["signatureUrl"] = release.SignatureUrl;
                values["setupSize"] = release.SetupSize;
                values["prerelease"] = release.Prerelease;
            }
            Json.Save(CachePath, values);
        }

        // False without a readable cache; release null when the last check found nothing newer.
        internal static bool ReadCache(out ReleaseInfo release, out DateTime checkedUtc)
        {
            release = null;
            checkedUtc = DateTime.MinValue;
            try
            {
                if (!File.Exists(CachePath)) return false;
                var values = Json.Load(CachePath);
                if (!DateTime.TryParse(Json.Str(values, "checkedUtc"), CultureInfo.InvariantCulture,
                                       DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal, out checkedUtc)) return false;
                string version = Json.Str(values, "version");
                if (string.IsNullOrEmpty(version)) return true;
                release = new ReleaseInfo
                {
                    Version = version, Tag = Json.Str(values, "tag"), Notes = Json.Str(values, "notes") ?? "",
                    SetupUrl = Json.Str(values, "setupUrl"), SignatureUrl = Json.Str(values, "signatureUrl"),
                    SetupSize = Json.Long(values, "setupSize"), Prerelease = Json.Bool(values, "prerelease")
                };
                if (release.SetupUrl == null || release.SignatureUrl == null) release = null;
                return true;
            }
            catch (Exception) { return false; }
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
