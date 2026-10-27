using System;
using System.Drawing;
using System.Threading;
using System.Windows.Forms;

namespace Nebula
{
    /// <summary>
    /// NVIDIA's DLSS license and the download. With licenseOnly the window
    /// shows the license text and a Close button.
    /// </summary>
    internal sealed class DlssInstallDialog : Form
    {
        private readonly bool licenseOnly;
        private readonly TextBox license = new TextBox();
        private readonly CheckBox accept = new CheckBox();
        private readonly Label status = new Label();
        private readonly ProgressBar bar = new ProgressBar();
        private readonly Button install = new Button(), close = new Button();
        private readonly CancellationTokenSource cancel = new CancellationTokenSource();
        private Thread worker;

        public DlssInstallDialog(bool licenseOnly)
        {
            this.licenseOnly = licenseOnly;
            Text = licenseOnly ? "NVIDIA RTX SDKs license" : "NVIDIA DLSS";
            Font = new Font("Segoe UI", 9.5f);
            AutoScaleMode = AutoScaleMode.None;
            ClientSize = new Size(640, 540);
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;

            int y = 14;
            if (!licenseOnly)
            {
                Controls.Add(new Label
                {
                    Text = "Nebula downloads DLSS from NVIDIA's Streamline 2.14.1 release on GitHub (" +
                           FileUtil.FormatBytes(DlssPackage.DownloadBytes) + "). The game uses it the next time it starts.",
                    Location = new Point(16, y), Size = new Size(608, 42)
                });
                y += 48;
            }
            license.Multiline = true;
            license.ReadOnly = true;
            license.BackColor = SystemColors.Window;
            license.ScrollBars = ScrollBars.Vertical;
            license.Font = new Font("Segoe UI", 9f);
            license.Location = new Point(16, y);
            license.Size = new Size(608, licenseOnly ? 470 : 350);
            license.Text = "Loading the license from GitHub…";
            Controls.Add(license);
            y += license.Height + 10;

            if (!licenseOnly)
            {
                accept.Text = "I accept the NVIDIA RTX SDKs license (required)";
                accept.Location = new Point(16, y);
                accept.Size = new Size(608, 24);
                accept.Enabled = false;
                accept.CheckedChanged += delegate { accept.ForeColor = SystemColors.ControlText; status.Text = ""; };
                Controls.Add(accept);
                y += 28;
                status.Location = new Point(16, y);
                status.Size = new Size(608, 22);
                Controls.Add(status);
                y += 24;
                bar.Location = new Point(16, y);
                bar.Size = new Size(608, 14);
                bar.Maximum = 1000;
                bar.Visible = false;
                Controls.Add(bar);
                install.Text = "Install";
                install.Location = new Point(418, 496);
                install.Size = new Size(100, 30);
                install.Enabled = false;
                install.Click += delegate { OnInstall(); };
                Controls.Add(install);
                AcceptButton = install;
            }
            close.Text = licenseOnly ? "Close" : "Cancel";
            close.Location = new Point(524, 496);
            close.Size = new Size(100, 30);
            close.Click += delegate { Close(); };
            Controls.Add(close);
            CancelButton = close;
            UiScale.Apply(this);
            Shown += delegate { LoadLicense(); };
            FormClosing += delegate(object sender, FormClosingEventArgs e)
            {
                cancel.Cancel();
                if (worker != null && worker.IsAlive)
                {
                    // The download stops at its next read; the window closes then.
                    e.Cancel = true;
                    close.Enabled = false;
                    status.Text = "Canceling…";
                }
            };
        }

        private void LoadLicense()
        {
            var thread = new Thread(delegate()
            {
                string text = null, error = null;
                try { text = DlssPackage.LicenseText(cancel.Token); }
                catch (OperationCanceledException) { return; }
                catch (Exception failure) { error = failure.Message; }
                Post(delegate
                {
                    if (text == null)
                    {
                        license.Text = "The license could not be loaded: " + error + "\r\n\r\nCheck the internet connection and try again.";
                        return;
                    }
                    license.Text = text.Replace("\r\n", "\n").Replace("\n", "\r\n");
                    accept.Enabled = true;
                    install.Enabled = true;
                });
            });
            thread.IsBackground = true;
            thread.Start();
        }

        private void OnInstall()
        {
            if (!accept.Checked)
            {
                accept.ForeColor = Color.Firebrick;
                status.ForeColor = Color.Firebrick;
                status.Text = "Accept the license to install DLSS.";
                accept.Focus();
                return;
            }
            install.Enabled = false;
            accept.Enabled = false;
            bar.Visible = true;
            status.ForeColor = SystemColors.ControlText;
            status.Text = "Downloading…";
            worker = new Thread(delegate()
            {
                Exception failure = null;
                try
                {
                    DlssPackage.Install((done, total) => Post(delegate
                    {
                        bar.Value = total > 0 ? (int)Math.Min(1000, done * 1000 / total) : 0;
                        status.Text = "Downloading… " + FileUtil.FormatBytes(done) + " of " + FileUtil.FormatBytes(total);
                    }), cancel.Token);
                }
                catch (Exception error) { failure = error; }
                Post(delegate
                {
                    worker = null;
                    if (failure == null)
                    {
                        DialogResult = DialogResult.OK;
                        Close();
                        return;
                    }
                    if (failure is OperationCanceledException) { Close(); return; }
                    bar.Visible = false;
                    status.ForeColor = Color.Firebrick;
                    status.Text = "Download failed: " + failure.Message;
                    install.Text = "Retry";
                    install.Enabled = true;
                    accept.Enabled = true;
                });
            });
            worker.IsBackground = true;
            worker.Start();
        }

        private void Post(Action action)
        {
            try { if (IsHandleCreated && !IsDisposed) BeginInvoke(action); }
            catch (InvalidOperationException) { }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) cancel.Dispose();
            base.Dispose(disposing);
        }
    }
}
