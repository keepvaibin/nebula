using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    /// <summary>
    /// The message at the end of a recording: OK, and Open folder, which shows the recording in Explorer (selected,
    /// ready to attach to a report).
    /// </summary>
    internal static class RecordingNotice
    {
        /// <summary>Shows the notice and returns when it is closed. <paramref name="saved"/> is the ZIP, or the
        /// session folder when the ZIP could not be written.</summary>
        public static void Show(string message, bool warning, string saved)
        {
            // The recording finishes on a worker thread; a dialog needs a single-threaded apartment.
            if (Thread.CurrentThread.GetApartmentState() != ApartmentState.STA)
            {
                var sta = new Thread(delegate() { Show(message, warning, saved); });
                sta.SetApartmentState(ApartmentState.STA);
                sta.Start();
                sta.Join();
                return;
            }
            using (var form = Create(message, warning, saved)) form.ShowDialog();
        }

        /// <summary>Explorer at the recording's folder with the recording selected.</summary>
        public static ProcessStartInfo FolderCommand(string saved)
        {
            return new ProcessStartInfo(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe"),
                "/select,\"" + Path.GetFullPath(saved) + "\"") { UseShellExecute = false };
        }

        internal static Form Create(string message, bool warning, string saved)
        {
            var form = new Form
            {
                Text = "Nebula recording", FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false,
                StartPosition = FormStartPosition.CenterScreen, ShowInTaskbar = true, AutoSize = true,
                AutoSizeMode = AutoSizeMode.GrowAndShrink, Font = SystemFonts.MessageBoxFont, Padding = new Padding(12)
            };
            var layout = new TableLayoutPanel { ColumnCount = 2, RowCount = 2, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink };
            var icon = new PictureBox
            {
                Image = (warning ? SystemIcons.Warning : SystemIcons.Information).ToBitmap(), SizeMode = PictureBoxSizeMode.AutoSize,
                Margin = new Padding(0, 0, 12, 12)
            };
            string text = message;
            if (saved.EndsWith(".zip", StringComparison.OrdinalIgnoreCase)) text += "\r\n\r\nAttach this ZIP file to your report.";
            var label = new Label { Text = text, AutoSize = true, MaximumSize = new Size(720, 0), Margin = new Padding(0, 4, 0, 16) };
            var buttons = new FlowLayoutPanel
            {
                FlowDirection = FlowDirection.RightToLeft, AutoSize = true, AutoSizeMode = AutoSizeMode.GrowAndShrink,
                Anchor = AnchorStyles.Right, Margin = Padding.Empty
            };
            var ok = new Button { Text = "OK", DialogResult = DialogResult.OK, AutoSize = true, MinimumSize = new Size(96, 0) };
            var open = new Button { Text = "Open folder", AutoSize = true, MinimumSize = new Size(96, 0) };
            open.Click += delegate
            {
                try { Process.Start(FolderCommand(saved)); }
                catch (Exception error)
                {
                    MessageBox.Show(form, "Could not open the folder: " + error.Message + "\r\n" + saved, "Nebula recording",
                        MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }
                form.DialogResult = DialogResult.OK;
            };
            buttons.Controls.Add(ok);
            buttons.Controls.Add(open);
            layout.Controls.Add(icon, 0, 0);
            layout.Controls.Add(label, 1, 0);
            layout.Controls.Add(buttons, 0, 1);
            layout.SetColumnSpan(buttons, 2);
            form.Controls.Add(layout);
            form.AcceptButton = ok;
            form.CancelButton = ok;
            // The launcher window is gone by now; bring the notice to the front.
            form.Shown += delegate { form.Activate(); };
            return form;
        }
    }
}
