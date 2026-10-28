using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    // Run by run-launcher-tests.ps1 from <scratch>\root\versions\test\ with
    // NEBULA_DATA_ROOT pointing into the scratch folder. No window is shown:
    // the settings form is rendered with DrawToBitmap and the game is a stub.
    internal static class LauncherTests
    {
        static void Check(bool ok, string what)
        {
            if (!ok) throw new Exception("FAIL: " + what);
            Console.WriteLine("PASS: " + what);
        }

        // The end-of-recording notice: Open folder beside OK, and the Explorer command it runs. Rendered to
        // recording-notice.png.
        static void RecordingNoticeCases(string outDir)
        {
            string zip = Path.Combine(outDir, "20261016-151013-808-example.zip");
            using (var form = RecordingNotice.Create("Recording saved to " + zip, false, zip))
            {
                var buttons = All(form).OfType<Button>().ToList();
                Check(buttons.Count == 2 && buttons.Any(b => b.Text == "Open folder") && buttons.Any(b => b.Text == "OK") &&
                      form.AcceptButton == buttons.First(b => b.Text == "OK"), "the recording notice offers Open folder and OK, OK by default");
                CreateHandles(form);
                form.PerformLayout();
                using (var bitmap = new Bitmap(form.Width, form.Height))
                {
                    form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
                    bitmap.Save(Path.Combine(outDir, "recording-notice.png"), ImageFormat.Png);
                }
            }
            var command = RecordingNotice.FolderCommand(zip);
            Check(command.FileName.EndsWith("\\explorer.exe", StringComparison.OrdinalIgnoreCase) &&
                  command.Arguments == "/select,\"" + Path.GetFullPath(zip) + "\"", "Open folder shows the recording selected in Explorer");
        }

        // The update offer: Update and Not now, Don't ask again unticked; Update ignores the box, Not now keeps it.
        // Rendered to update-prompt.png.
        static void UpdatePromptCases(string outDir)
        {
            var release = new ReleaseInfo { Version = "9.9.9", Notes = "What changed in this release.\n\n- One fix.\n- Another." };
            using (var form = new UpdatePromptForm(release))
            {
                var buttons = All(form).OfType<Button>().ToList();
                // Don't ask again (a ray tracing machine's offer also holds Prepare ray tracing shaders).
                var box = All(form).OfType<CheckBox>().Single(b => b.Text == "Don't ask again");
                Check(buttons.Any(b => b.Text == "Update") && buttons.Any(b => b.Text == "Not now") && !box.Checked &&
                      form.AcceptButton == buttons.First(b => b.Text == "Update"), "the update offer: Update by default, Not now, Don't ask again unticked");
                CreateHandles(form);
                form.PerformLayout();
                using (var bitmap = new Bitmap(form.Width, form.Height))
                {
                    form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
                    bitmap.Save(Path.Combine(outDir, "update-prompt.png"), ImageFormat.Png);
                }
                box.Checked = true;
                form.Choose(true);
                Check(form.DialogResult == DialogResult.Yes && !form.DontAskAgain, "Update ignores Don't ask again");
            }
            using (var form = new UpdatePromptForm(release))
            {
                All(form).OfType<CheckBox>().Single(b => b.Text == "Don't ask again").Checked = true;
                form.Choose(false);
                Check(form.DialogResult == DialogResult.No && form.DontAskAgain, "Not now keeps Don't ask again");
            }
            // A release's notes as GitHub holds them (Markdown, 1.0.2's opening): plain text in the prompt.
            var marked = new ReleaseInfo
            {
                Version = "9.9.9",
                Notes = "> ## Join the Nebula Discord\r\n> **https://discord.gg/example**\r\n>\r\n> We post every update there.\r\n\r\n" +
                        "## Why Windows flagged Nebula 1.0.1\r\n\r\nThe `Nebula.exe` it installs (see **Security** below, #26).\r\n\r\n" +
                        "* One fix, [the notes](https://example.org/notes).\r\n- Another.\r\n\r\n---\r\n"
            };
            using (var form = new UpdatePromptForm(marked))
            {
                string shown = All(form).OfType<TextBox>().Single().Text;
                Check(shown == "Join the Nebula Discord\r\nhttps://discord.gg/example\r\n\r\nWe post every update there.\r\n\r\n" +
                               "Why Windows flagged Nebula 1.0.1\r\n\r\nThe Nebula.exe it installs (see Security below, #26).\r\n\r\n" +
                               "- One fix, the notes (https://example.org/notes).\r\n- Another.",
                      "the notes show as plain text (no Markdown marks): " + shown.Replace("\r\n", "|"));
                CreateHandles(form);
                form.PerformLayout();
                using (var bitmap = new Bitmap(form.Width, form.Height))
                {
                    form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
                    bitmap.Save(Path.Combine(outDir, "update-prompt-notes.png"), ImageFormat.Png);
                }
            }
            Check(ReleaseNotes.Plain(new string('a', 30) + "\r\n" + new string('b', 30), 40) == new string('a', 30) + "\r\n…",
                  "long notes end at a line's end with an ellipsis");
        }

        // The pad in Nebula's dialogs: a press acts once, a held direction repeats at the game's pace, what is held as
        // a dialog opens does nothing, A and B act when pressed.
        static void PadMenuCases()
        {
            var input = new PadMenuInput();
            var none = new PadState();
            var right = new PadState { Right = true };
            var up = new PadState { Up = true };
            var confirm = new PadState { Confirm = true };
            var back = new PadState { Back = true };
            input.HoldCurrent(none);
            Check(input.Step(right, 0) == PadMenuInput.Action.Next, "pad: a press moves once");
            Check(input.Step(right, 100) == PadMenuInput.Action.None && input.Step(right, 299) == PadMenuInput.Action.None,
                "pad: held, nothing before the repeat delay");
            Check(input.Step(right, 300) == PadMenuInput.Action.Next, "pad: the first repeat at 300 ms");
            Check(input.Step(right, 350) == PadMenuInput.Action.None && input.Step(right, 383) == PadMenuInput.Action.Next,
                "pad: then every 83 ms");
            Check(input.Step(none, 400) == PadMenuInput.Action.None && input.Step(up, 410) == PadMenuInput.Action.Previous,
                "pad: up goes back");
            Check(input.Step(confirm, 420) == PadMenuInput.Action.Confirm && input.Step(confirm, 500) == PadMenuInput.Action.None,
                "pad: A acts once per press");
            Check(input.Step(back, 510) == PadMenuInput.Action.Back, "pad: B cancels");

            var opened = new PadMenuInput();
            opened.HoldCurrent(new PadState { Down = true, Confirm = true });
            Check(opened.Step(new PadState { Down = true, Confirm = true }, 0) == PadMenuInput.Action.None &&
                  opened.Step(new PadState { Down = true, Confirm = true }, 2000) == PadMenuInput.Action.None,
                "pad: what is held as a dialog opens does nothing, even held on");
            Check(opened.Step(none, 2100) == PadMenuInput.Action.None && opened.Step(confirm, 2110) == PadMenuInput.Action.Confirm,
                "pad: released and pressed again, it acts");
        }

        // A form that is never shown creates no child windows; reading Handle creates them.
        static void CreateHandles(Control control)
        {
            IntPtr unused = control.Handle;
            foreach (Control child in control.Controls) CreateHandles(child);
        }

        static System.Collections.Generic.IEnumerable<Control> All(Control control)
        {
            foreach (Control child in control.Controls)
            {
                yield return child;
                foreach (Control grandchild in All(child)) yield return grandchild;
            }
        }

        // DrawToBitmap leaves drop-down lists blank, so their selections are listed in a text file.
        static void Render(Form form, TabControl tabs, string outDir, string prefix)
        {
            CreateHandles(form);
            var combos = All(form).OfType<ComboBox>().ToList();
            File.WriteAllLines(Path.Combine(outDir, prefix + "-choices.txt"),
                combos.Select(c => c.Parent.Text + ": " + (c.SelectedItem ?? "<none>") + " [" + c.Items.Count + " items]"));
            Check(combos.All(c => c.SelectedIndex >= 0), prefix + ": every drop-down has a selection");
            for (int i = 0; i < tabs.TabPages.Count; i++)
            {
                tabs.SelectedIndex = i;
                using (var bitmap = new Bitmap(form.Width, form.Height))
                {
                    form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
                    bitmap.Save(Path.Combine(outDir, prefix + "-" + tabs.TabPages[i].Text.ToLowerInvariant() + ".png"), ImageFormat.Png);
                }
            }
        }

        // A PNG of the form with each visible drop-down's selection drawn into it (DrawToBitmap leaves them blank).
        static void Shot(Form form, string path)
        {
            using (var bitmap = new Bitmap(form.Width, form.Height))
            {
                form.DrawToBitmap(bitmap, new Rectangle(Point.Empty, form.Size));
                using (var graphics = Graphics.FromImage(bitmap))
                {
                    // The bitmap holds the whole window: the client area starts after the border and the caption.
                    int border = (form.Width - form.ClientSize.Width) / 2;
                    int caption = form.Height - form.ClientSize.Height - border;
                    var tabs = form.Controls.OfType<TabControl>().Single();
                    foreach (var combo in All(tabs.SelectedTab).OfType<ComboBox>().Where(c => c.SelectedItem != null))
                    {
                        Point at = form.PointToClient(combo.Parent.PointToScreen(combo.Location));
                        var box = new Rectangle(at.X + border, at.Y + caption, combo.Width, combo.Height);
                        box.Inflate(-6, 0);
                        box.Width -= box.Height;
                        TextRenderer.DrawText(graphics, combo.SelectedItem.ToString(), combo.Font, box,
                            combo.Enabled ? SystemColors.WindowText : SystemColors.GrayText,
                            TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
                    }
                }
                bitmap.Save(path, ImageFormat.Png);
            }
        }

        static object Private(object target, string name)
        {
            return target.GetType().GetField(name, System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).GetValue(target);
        }

        // The GPU picker: the GPUs DXGI lists, the GPU the game will run on for a gpu setting, and the DLSS and ray
        // tracing that follow it. A laptop: an NVIDIA RTX GPU (DXGI's high-performance first) and an integrated one.
        static readonly GpuAdapter Rtx = new GpuAdapter { Name = "NVIDIA GeForce RTX 4070 Laptop GPU", VendorId = 0x10DE, DeviceId = 0x2860, SubSysId = 0x3FA1103C };
        static readonly GpuAdapter Integrated = new GpuAdapter { Name = "AMD Radeon(TM) Graphics", VendorId = 0x1002, DeviceId = 0x1681, SubSysId = 0x3FA1103C };

        static void GpuCases()
        {
            var real = GpuAdapters.List();
            Check(real != null && real.Count >= 1 && real.Select(g => g.Id).Distinct().Count() == real.Count, "DXGI lists this PC's GPUs, each once: " +
                  (real == null ? "none" : string.Join(", ", real.Select(g => g.Name + " " + g.Id))));
            Check(Rtx.Id == "10de:2860:3fa1103c" && Rtx.IsNvidiaRtx && !Integrated.IsNvidiaRtx,
                  "a GPU's id is the runtime's gpu_identity, and an NVIDIA RTX GPU is recognised");

            var original = GpuAdapters.Source;
            GpuAdapters.Source = () => new List<GpuAdapter> { Rtx, Integrated };
            string adapterIni = Path.Combine(NebulaPaths.DataRoot, "adapter.ini");
            try
            {
                if (File.Exists(adapterIni)) File.Delete(adapterIni);
                Check(GraphicsSettings.TargetAdapter("") == Rtx && GraphicsSettings.TargetAdapter(Integrated.Id) == Integrated &&
                      GraphicsSettings.TargetAdapter("dead:beef:00000000") == Rtx,
                      "the game runs on the chosen GPU, or automatically when none is chosen or it is not present");
                Check(GraphicsSettings.UpscalerChoices("").Contains(3) && GraphicsSettings.UpscalerChoices(Rtx.Id).Contains(3) &&
                      !GraphicsSettings.UpscalerChoices(Integrated.Id).Contains(3),
                      "DLSS is offered for the RTX GPU and not for the integrated one");
                Check(GraphicsSettings.RayTracingAdapter(Rtx.Id) && !GraphicsSettings.RayTracingAdapter(Integrated.Id),
                      "before any run, ray tracing is offered for an RTX GPU only");
                File.WriteAllText(adapterIni, "description=" + Integrated.Name + "\nid=" + Integrated.Id + "\ngpu_preference=high\nray_tracing=1\n");
                Check(GraphicsSettings.RayTracingAdapter(Integrated.Id) && GraphicsSettings.RayTracingAdapter(Rtx.Id),
                      "a GPU the game has run on is offered ray tracing as the game recorded it");
                File.WriteAllText(adapterIni, "description=" + Rtx.Name + "\nid=" + Rtx.Id + "\ngpu_preference=high\nray_tracing=0\n");
                Check(!GraphicsSettings.RayTracingAdapter(Rtx.Id), "the game's record wins over the RTX rule");
                File.Delete(adapterIni);

                File.WriteAllText(GraphicsSettings.ConfigPath, "gpu=1002:1681:3FA1103C\n");
                var settings = GraphicsSettings.Load();
                Check(settings.Gpu == Integrated.Id, "the gpu key loads as an id: " + settings.Gpu);
                settings.Gpu = Rtx.Id;
                settings.Save();
                Check(File.ReadAllText(GraphicsSettings.ConfigPath).Contains("gpu=" + Rtx.Id) && GraphicsSettings.Load().Gpu == Rtx.Id,
                      "the chosen GPU saves for the game");
                settings.Gpu = "";
                settings.Save();
                Check(GraphicsSettings.Load().Gpu == "", "Automatic saves as an empty gpu");
            }
            finally
            {
                GpuAdapters.Source = original;
                if (File.Exists(adapterIni)) File.Delete(adapterIni);
            }
        }

        static GpuAdapter Gpu(GpuAdapter model, params string[] outputs)
        {
            return new GpuAdapter { Name = model.Name, VendorId = model.VendorId, DeviceId = model.DeviceId, SubSysId = model.SubSysId,
                                    Outputs = new List<string>(outputs) };
        }

        // The notice under the Monitor row, from adapter and display lists: which GPU each display is connected
        // to (DXGI's outputs), against the GPU the game will run on.
        static void DisplayNoticeCases()
        {
            const string panel = @"\\.\DISPLAY1", hdmi = @"\\.\DISPLAY2";
            var desktop = new List<GpuAdapter> { Gpu(Rtx, panel, hdmi) };
            Check(GpuAdapters.DisplayNotice(desktop, "", panel) == null && GpuAdapters.DisplayNotice(desktop, Rtx.Id, hdmi) == null,
                  "one GPU driving every display: no notice");

            // A hybrid laptop: the built-in panel hangs off the integrated GPU, the NVIDIA GPU has no outputs.
            var hybrid = new List<GpuAdapter> { Gpu(Rtx), Gpu(Integrated, panel) };
            string copied = "Connected to the " + Integrated.Name + "; frames are copied from the " + Rtx.Name + ".";
            Check(GpuAdapters.DisplayNotice(hybrid, "", panel) == copied && GpuAdapters.DisplayNotice(hybrid, Rtx.Id, panel) == copied,
                  "hybrid laptop, NVIDIA GPU (chosen or Automatic): " + copied);
            Check(GpuAdapters.DisplayNotice(hybrid, Integrated.Id, panel) == null, "hybrid laptop, integrated GPU: no notice");
            Check(GpuAdapters.DisplayNotice(hybrid, "dead:beef:00000000", panel) == copied,
                  "a chosen GPU that is not present is Automatic here too");
            Check(GpuAdapters.DisplayNotice(hybrid, Rtx.Id, @"\\.\display1") == copied, "GDI device names compare without case");

            // The same laptop with an external display on the NVIDIA GPU's HDMI port.
            var docked = new List<GpuAdapter> { Gpu(Rtx, hdmi), Gpu(Integrated, panel) };
            Check(GpuAdapters.DisplayNotice(docked, Rtx.Id, hdmi) == null &&
                  GpuAdapters.DisplayNotice(docked, Integrated.Id, hdmi) ==
                      "Connected to the " + Rtx.Name + "; frames are copied from the " + Integrated.Name + ".",
                  "an external display on the NVIDIA GPU: notice only with the integrated GPU chosen");
            Check(GpuAdapters.DisplayNotice(docked, Rtx.Id, @"\\.\DISPLAY9") == null && GpuAdapters.DisplayNotice(docked, Rtx.Id, "") == null,
                  "a display no listed GPU names (USB, remote) has no notice");
            Check(GpuAdapters.DisplayNotice(new List<GpuAdapter>(), "", panel) == null, "no GPUs listed: no notice");

            var twins = new List<GpuAdapter> { Gpu(Rtx, panel), new GpuAdapter { Name = Rtx.Name, VendorId = Rtx.VendorId, DeviceId = Rtx.DeviceId, SubSysId = 0x12345678 } };
            Check(GpuAdapters.DisplayNotice(twins, twins[1].Id, panel) ==
                      "Connected to the " + Rtx.Name + " #1; frames are copied from the " + Rtx.Name + " #2.",
                  "two GPUs of one model are named as in the Graphics card list");

            // This PC through DXGI. Software adapters are listed here only (the game never renders on them): the
            // Microsoft Basic Render Driver has no outputs, so choosing it shows the notice on real lists.
            var all = GpuAdapters.Enumerate(true);
            var monitors = DisplayInfo.Enumerate();
            Console.WriteLine("INFO: DXGI adapters with software: " + string.Join("; ", all.Select(g => g.Name + " " + g.Id + " outputs=[" + string.Join(",", g.Outputs) + "]")));
            Console.WriteLine("INFO: monitors: " + string.Join("; ", monitors.Select(m => m.Device + (m.Primary ? " (main)" : ""))));
            Check(monitors.All(m => all.Count(g => g.Drives(m.Device)) <= 1), "every monitor is an output of at most one GPU");
            var hardware = GpuAdapters.List();
            Check(hardware != null && hardware.All(h => all.Exists(g => g.Id == h.Id)) && hardware.Count < all.Count,
                  "the game's list leaves out software adapters");
            foreach (var m in monitors)
                foreach (var g in hardware)
                {
                    var connected = all.Find(other => other.Drives(m.Device));
                    string text = GpuAdapters.DisplayNotice(hardware, g.Id, m.Device);
                    Check(connected == null || g.Drives(m.Device) ? text == null
                              : text == "Connected to the " + GpuAdapters.Label(hardware, connected) +
                                        "; frames are copied from the " + GpuAdapters.Label(hardware, g) + ".",
                          "real lists, " + g.Name + " and " + m.Device + ": " + (text ?? "no notice"));
                }
            var main = DisplayInfo.Find(monitors, "");
            var basic = all.Find(g => g.VendorId == 0x1414 && g.Outputs.Count == 0);
            var driver = main == null ? null : all.Find(g => g.Drives(main.Device));
            if (basic == null || driver == null)
            {
                Console.WriteLine("SKIP: real lists (no software adapter without outputs, or no GPU names the main monitor)");
                return;
            }
            Check(GpuAdapters.DisplayNotice(all, driver.Id, main.Device) == null,
                  "real lists: the GPU the main monitor is connected to has no notice (" + driver.Name + ")");
            string real = GpuAdapters.DisplayNotice(all, basic.Id, main.Device);
            Check(real == "Connected to the " + driver.Name + "; frames are copied from the " + basic.Name + ".",
                  "real lists, " + basic.Name + " chosen: " + real);
        }

        // The settings window on the laptop: the Graphics card row, and the options switching with it.
        static void GpuFormCases(LaunchSession session, string outDir)
        {
            var original = GpuAdapters.Source;
            // The integrated GPU drives this PC's main monitor, the RTX GPU none: the hybrid laptop's layout.
            string mainMonitor = DisplayInfo.Find(DisplayInfo.Enumerate(), "").Device;
            GpuAdapters.Source = () => new List<GpuAdapter> { Gpu(Rtx), Gpu(Integrated, mainMonitor) };
            File.WriteAllText(GraphicsSettings.ConfigPath, "gpu=" + Integrated.Id + "\n");
            try
            {
                using (var form = new SettingsForm(session))
                {
                    CreateHandles(form);
                    var gpuBox = (ComboBox)Private(form, "gpuBox");
                    var upscaler = (ComboBox)Private(form, "upscaler");
                    var interpolation = (ComboBox)Private(form, "interpolation");
                    var monitor = (ComboBox)Private(form, "monitor");
                    var mode = (ComboBox)Private(form, "mode");
                    var notice = (Label)Private(form, "displayNotice");
                    var tabs = form.Controls.OfType<TabControl>().Single();
                    tabs.SelectedIndex = 0;
                    Check(gpuBox.Parent != null && gpuBox.Parent.Text == "Display" && gpuBox.Items.Count == 3 &&
                          (string)gpuBox.Items[0] == "Automatic (" + Rtx.Name + ")" && (string)gpuBox.Items[1] == Rtx.Name &&
                          (string)gpuBox.Items[2] == Integrated.Name,
                          "with two GPUs the Display tab has a Graphics card list: Automatic (the GPU it picks), then each GPU");
                    int rowStep = mode.Top - monitor.Top;
                    Check(notice.Text == "" && notice.Top > monitor.Bottom && notice.Top < mode.Top,
                          "the integrated GPU drives the monitor: no notice; its place is between the Monitor and Display mode rows");
                    Shot(form, Path.Combine(outDir, "gpu-display-match.png"));
                    gpuBox.SelectedIndex = 1;
                    Check(notice.Text == "Connected to the " + Integrated.Name + "; frames are copied from the " + Rtx.Name + "." &&
                          mode.Top - monitor.Top == rowStep + notice.Height && notice.Bottom <= mode.Top,
                          "the RTX GPU chosen: the notice shows directly below the Monitor row and the rows below move down");
                    Shot(form, Path.Combine(outDir, "gpu-display-notice.png"));
                    gpuBox.SelectedIndex = 0;
                    Check(notice.Text.EndsWith("from the " + Rtx.Name + ".") && mode.Top - monitor.Top == rowStep + notice.Height,
                          "Automatic (the RTX GPU) shows the same notice");
                    gpuBox.SelectedIndex = 2;
                    Check(notice.Text == "" && mode.Top - monitor.Top == rowStep, "back on the integrated GPU the notice is gone and the rows move back");
                    Func<bool> dlssListed = () => upscaler.Items.Cast<string>().Any(i => i.StartsWith("NVIDIA DLSS"));
                    Func<bool> frameGenerationListed = () => interpolation.Items.Cast<string>().Contains("DLSS Frame Generation");
                    Check(gpuBox.SelectedIndex == 2 && !dlssListed() && !frameGenerationListed() && !(bool)Private(form, "rayTracingOffered"),
                          "the saved integrated GPU is selected, without DLSS, Frame Generation or ray tracing");
                    gpuBox.SelectedIndex = 1;
                    Check(dlssListed() && frameGenerationListed() && (bool)Private(form, "rayTracingOffered") && upscaler.SelectedIndex >= 0,
                          "choosing the RTX GPU offers DLSS, Frame Generation and ray tracing at once");
                    gpuBox.SelectedIndex = 0;
                    Check(dlssListed() && (bool)Private(form, "rayTracingOffered"), "Automatic offers what the GPU it picks runs");
                    gpuBox.SelectedIndex = 2;
                    Check(!dlssListed() && !frameGenerationListed() && !(bool)Private(form, "rayTracingOffered") &&
                          upscaler.SelectedIndex >= 0 && interpolation.SelectedIndex >= 0,
                          "back on the integrated GPU they are withdrawn and every list keeps a selection");
                    gpuBox.SelectedIndex = 1;
                    form.GetType().GetMethod("Commit", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic).Invoke(form, null);
                    Check(GraphicsSettings.Load().Gpu == Rtx.Id, "Save stores the chosen GPU for the game");
                }
                // While the game runs (its lease held by another thread), a GPU without DLSS chosen for the next start
                // leaves the running game's upscaler, Frame Generation and frame rate in config.ini; the player's own
                // choice of upscaler is saved.
                File.WriteAllText(GraphicsSettings.ConfigPath, "gpu=" + Rtx.Id + "\nupscaler=3\nframe_generation=1\nmax_fps=120\n");
                using (var held = new ManualResetEvent(false))
                using (var release = new ManualResetEvent(false))
                {
                    var holder = new Thread(() =>
                    {
                        using (var lease = new Mutex(true, @"Local\Nebula.RMGE01.NativeRuntime"))
                        {
                            held.Set();
                            release.WaitOne();
                            lease.ReleaseMutex();
                        }
                    });
                    holder.Start();
                    held.WaitOne();
                    try
                    {
                        Check(LaunchSession.RuntimeLeaseHeld(), "the test's lease reads as a running game");
                        using (var form = new SettingsForm(session))
                        {
                            CreateHandles(form);
                            var gpuBox = (ComboBox)Private(form, "gpuBox");
                            var upscaler = (ComboBox)Private(form, "upscaler");
                            var commit = form.GetType().GetMethod("Commit", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic);
                            gpuBox.SelectedIndex = 2;
                            Check((string)upscaler.SelectedItem == GraphicsSettings.UpscalerName(1),
                                  "the integrated GPU chosen while the game runs: the list shows its fallback, FSR 1");
                            commit.Invoke(form, null);
                            var saved = GraphicsSettings.Load();
                            Check(saved.Gpu == Integrated.Id && saved.Upscaler == 3 && saved.FrameGeneration == 1 && saved.FrameRate == 120,
                                  "saved while the game runs: the GPU for the next start; the game's DLSS, Frame Generation and 120 fps kept");
                            upscaler.SelectedIndex = upscaler.Items.IndexOf(GraphicsSettings.UpscalerName(0));
                            commit.Invoke(form, null);
                            Check(GraphicsSettings.Load().Upscaler == 0, "an upscaler the player picks while the game runs is saved");
                        }
                    }
                    finally
                    {
                        release.Set();
                        holder.Join();
                    }
                }
                Check(!LaunchSession.RuntimeLeaseHeld(), "the test's lease is released");
                // One GPU: no Graphics card row and no notice; the Monitor row is the first.
                GpuAdapters.Source = () => new List<GpuAdapter> { Gpu(Rtx, mainMonitor) };
                using (var form = new SettingsForm(session))
                {
                    CreateHandles(form);
                    var monitor = (ComboBox)Private(form, "monitor");
                    var mode = (ComboBox)Private(form, "mode");
                    var resolution = (ComboBox)Private(form, "resolution");
                    Check(((ComboBox)Private(form, "gpuBox")).Parent == null && ((Label)Private(form, "displayNotice")).Text == "" &&
                          mode.Top - monitor.Top == resolution.Top - mode.Top &&
                          monitor.Parent.Controls.OfType<ComboBox>().All(c => c.Top >= monitor.Top),
                          "with one GPU the Display tab starts with the Monitor row, without a Graphics card row or notice");
                }
                // This PC's own lists: a GPU the main monitor is not connected to, when it has one.
                GpuAdapters.Source = original;
                var real = GpuAdapters.List();
                var main = DisplayInfo.Find(DisplayInfo.Enumerate(), "");
                var other = real == null ? null : real.Find(g => !g.Drives(main.Device));
                if (real == null || real.Count < 2 || other == null || !real.Exists(g => g.Drives(main.Device)))
                {
                    Console.WriteLine("SKIP: real Display tab with a notice (this PC has no second GPU besides the main monitor's)");
                    return;
                }
                var driver = real.Find(g => g.Drives(main.Device));
                File.WriteAllText(GraphicsSettings.ConfigPath, "gpu=" + driver.Id + "\nmonitor=" + main.Device + "\n");
                using (var form = new SettingsForm(session))
                {
                    CreateHandles(form);
                    Check(((Label)Private(form, "displayNotice")).Text == "",
                          "real lists, " + driver.Name + " chosen for " + main.Device + ": no notice");
                    form.Controls.OfType<TabControl>().Single().SelectedIndex = 0;
                    Shot(form, Path.Combine(outDir, "real-display-match.png"));
                }
                File.WriteAllText(GraphicsSettings.ConfigPath, "gpu=" + other.Id + "\nmonitor=" + main.Device + "\n");
                using (var form = new SettingsForm(session))
                {
                    CreateHandles(form);
                    var notice = (Label)Private(form, "displayNotice");
                    Check(notice.Text == GpuAdapters.DisplayNotice(real, other.Id, main.Device) && notice.Text.Contains(other.Name),
                          "real lists, " + other.Name + " chosen for " + main.Device + ": " + notice.Text);
                    form.Controls.OfType<TabControl>().Single().SelectedIndex = 0;
                    Shot(form, Path.Combine(outDir, "real-display-notice.png"));
                }
            }
            finally
            {
                GpuAdapters.Source = original;
                File.Delete(GraphicsSettings.ConfigPath);
            }
        }

        // Windowed sizes for a fixed aspect, and the size chosen when the aspect changes.
        static void WindowSizeCases()
        {
            Check(GraphicsSettings.AspectWindowSizes(0, 4000, 4000).Count == 0, "Auto aspect has no fixed window sizes");
            int[,] ratios = { { 0, 0 }, { 4, 3 }, { 3, 2 }, { 16, 10 }, { 16, 9 }, { 21, 9 }, { 32, 9 } };
            for (int aspect = 1; aspect < GraphicsSettings.Aspects.Length; aspect++)
            {
                var sizes = GraphicsSettings.AspectWindowSizes(aspect, 2600, 1400);
                Check(sizes.Count >= 3, GraphicsSettings.Aspects[aspect] + ": at least three window sizes fit 2600 x 1400");
                Check(sizes.All(s => s.Width * ratios[aspect, 1] == s.Height * ratios[aspect, 0]), GraphicsSettings.Aspects[aspect] + ": every size has the exact aspect");
                Check(sizes.All(s => s.Width <= 2600 && s.Height <= 1400), GraphicsSettings.Aspects[aspect] + ": every size fits");
                Check(sizes.Zip(sizes.Skip(1), (a, b) => a.Height < b.Height).All(x => x), GraphicsSettings.Aspects[aspect] + ": smallest first");
            }
            var wide = GraphicsSettings.AspectWindowSizes(5, 2600, 1400);
            Check(wide[GraphicsSettings.SnapWindowSize(wide, new Size(1440, 1080))] == new Size(2520, 1080), "21:9 from 1440 x 1080 snaps to 2520 x 1080");
            Check(wide[GraphicsSettings.SnapWindowSize(wide, new Size(1280, 720))] == new Size(1680, 720), "21:9 from 1280 x 720 snaps to 1680 x 720");
            var small = GraphicsSettings.AspectWindowSizes(5, 1900, 1000);
            Check(small[GraphicsSettings.SnapWindowSize(small, new Size(1920, 1080))] == small[small.Count - 1], "a size taller than the screen allows snaps to the largest");
        }

        static void RecordingEncodingCases(string app)
        {
            var plain = LaunchOptions.Create(app, "content", "saves", RecordingMode.None);
            var recorded = LaunchOptions.Create(app, "content", "saves", RecordingMode.Detailed);
            Check(plain.StandardErrorEncoding == null && recorded.StandardOutputEncoding.CodePage == 65001 &&
                recorded.StandardErrorEncoding.CodePage == 65001, "a recording reads the runtime's output as UTF-8");
        }

        static void AudioCases()
        {
            string path = AudioSettings.SettingsPath;
            if (File.Exists(path)) File.Delete(path);
            Check(AudioSettings.Names[6] == "Wiimote sounds" && AudioSettings.Load().Percent[6] == 0, "Wiimote sounds start at 0%");
            File.WriteAllText(path, "audio_master_gain=0.5\naudio_cutscene_gain=1\n");
            var older = AudioSettings.Load();
            Check(older.Percent[0] == 50 && older.Percent[6] == 0, "a settings file without the key keeps Wiimote sounds at 0%");
            older.Percent[6] = 75;
            older.Save();
            Check(File.ReadAllText(path).Contains("audio_wiimote_speaker_gain=0.75") && AudioSettings.Load().Percent[6] == 75,
                  "Wiimote sounds are saved with the other volumes");
            File.Delete(path);
        }

        static byte[] Noise(int length, int seed)
        {
            var bytes = new byte[length];
            new Random(seed).NextBytes(bytes);
            return bytes;
        }

        static void PutBe32(byte[] bytes, int offset, uint value)
        {
            bytes[offset] = (byte)(value >> 24);
            bytes[offset + 1] = (byte)(value >> 16);
            bytes[offset + 2] = (byte)(value >> 8);
            bytes[offset + 3] = (byte)value;
        }

        static byte[] GameData(int seed)
        {
            byte[] bytes = Noise(SaveFormat.GameDataSize, seed);
            PutBe32(bytes, 4, 2);
            PutBe32(bytes, 8, 19);
            PutBe32(bytes, 12, SaveFormat.GameDataSize);
            for (int i = 0; i < SaveFormat.GameDataNames.Length; i++)
            {
                int info = 0x10 + i * 0x10;
                Array.Clear(bytes, info, 12);
                Encoding.ASCII.GetBytes(SaveFormat.GameDataNames[i]).CopyTo(bytes, info);
                PutBe32(bytes, info + 12, SaveFormat.GameDataOffsets[i]);
            }
            PutBe32(bytes, 0, SaveFormat.GameDataChecksum(bytes));
            return bytes;
        }

        static byte[] Banner(int seed)
        {
            byte[] bytes = Noise(SaveFormat.BannerSize, seed);
            Encoding.ASCII.GetBytes("WIBN").CopyTo(bytes, 0);
            Array.Clear(bytes, 4, 28);
            bytes[9] = 2;
            return bytes;
        }

        static byte[] MiiDatabase(int seed)
        {
            byte[] bytes = Noise(SaveFormat.MiiDatabaseSize, seed);
            PutBe32(bytes, 0, 0x524E4F44);
            PutBe32(bytes, SaveFormat.MiiHiddenOffset, 0x524E4844);
            bytes[bytes.Length - 2] = bytes[bytes.Length - 1] = 0;
            int crc = SaveFormat.MiiCrc(bytes);
            bytes[bytes.Length - 2] = (byte)(crc >> 8);
            bytes[bytes.Length - 1] = (byte)crc;
            return bytes;
        }

        // A saves tree as the runtime keeps it, each folder and file with a metadata stream, plus files the game
        // never reads.
        static void BuildSaves(string root, int seed)
        {
            int stream = 0;
            foreach (string folder in SaveArchive.Folders)
            {
                string host = SaveArchive.HostPath(root, folder);
                Directory.CreateDirectory(host);
                FileUtil.WriteNamedStream(host, "galaxy.isfs.meta", Noise(18, seed * 100 + stream++));
            }
            var files = new Dictionary<string, byte[]>
            {
                { SaveArchive.GameDataPath, GameData(seed) }, { SaveArchive.BannerPath, Banner(seed) },
                { SaveArchive.GameDataPathOf("524d4750"), GameData(seed + 7) },
                { SaveArchive.MiiDatabasePath, MiiDatabase(seed) }, { "shared2/sys/SYSCONF", Noise(0x4000, seed + 1) },
                { "title/00000001/00000002/data/setting.txt", Noise(0x100, seed + 2) },
                { "title/00000001/00000002/data/play_rec.dat", Noise(0x80, seed + 3) },
            };
            foreach (var file in files)
            {
                string host = SaveArchive.HostPath(root, file.Key);
                File.WriteAllBytes(host, file.Value);
                FileUtil.WriteNamedStream(host, "galaxy.isfs.meta", Noise(18, seed * 100 + stream++));
            }
            Directory.CreateDirectory(Path.Combine(root, "tmp", "sys"));
            File.WriteAllBytes(Path.Combine(root, "tmp", "sys", "leftover.bin"), Noise(64, seed + 4));
            File.WriteAllBytes(SaveArchive.HostPath(root, "title/00010000/524d4745/data/desktop.ini"), Noise(16, seed + 5));
        }

        static Exception Failure(Action action)
        {
            try { action(); return null; }
            catch (Exception error) { return error; }
        }

        // A copy of an archive with one byte of an entry's stored data inverted.
        static void DamageEntry(string zip, string damaged, string entryName)
        {
            byte[] bytes = File.ReadAllBytes(zip);
            byte[] name = Encoding.ASCII.GetBytes(entryName);
            int at = -1;
            for (int i = 30; i + name.Length <= bytes.Length && at < 0; i++)
                if (bytes[i] == name[0] && bytes.Skip(i).Take(name.Length).SequenceEqual(name)) at = i;
            int header = at - 30, extra = bytes[header + 28] | bytes[header + 29] << 8;
            bytes[at + name.Length + extra + 64] ^= 0xFF;
            File.WriteAllBytes(damaged, bytes);
        }

        static string ZipWith(string path, string manifest)
        {
            using (ZipArchive archive = ZipFile.Open(path, ZipArchiveMode.Create))
            using (var writer = new StreamWriter(archive.CreateEntry("nebula-saves.json").Open()))
                writer.Write(manifest);
            return path;
        }

        // A game.pak's start: boot.bin with the game id and where the file table is, then the table with a directory
        // at the root for each name (each empty), as the disc's file table lists them.
        static byte[] TestPak(string gameId, params string[] directories)
        {
            var names = new List<byte>();
            var entries = new List<byte>();
            Action<byte, uint, uint, uint> entry = (kind, name, a, b) =>
            {
                entries.Add(kind);
                entries.Add((byte)(name >> 16)); entries.Add((byte)(name >> 8)); entries.Add((byte)name);
                foreach (uint word in new[] { a, b })
                {
                    entries.Add((byte)(word >> 24)); entries.Add((byte)(word >> 16)); entries.Add((byte)(word >> 8)); entries.Add((byte)word);
                }
            };
            uint count = (uint)directories.Length + 1;
            entry(1, 0, 0, count);
            names.Add(0);
            for (int i = 0; i < directories.Length; i++)
            {
                uint at = (uint)names.Count;
                names.AddRange(System.Text.Encoding.ASCII.GetBytes(directories[i]));
                names.Add(0);
                entry(1, at, 0, (uint)i + 2);
            }
            var fst = new List<byte>(entries);
            fst.AddRange(names);
            while (fst.Count % 4 != 0) fst.Add(0);
            var pak = new byte[0x440 + fst.Count];
            System.Text.Encoding.ASCII.GetBytes(gameId).CopyTo(pak, 0);
            uint offset = 0x440 >> 2, size = (uint)fst.Count >> 2;
            pak[0x424] = (byte)(offset >> 24); pak[0x425] = (byte)(offset >> 16); pak[0x426] = (byte)(offset >> 8); pak[0x427] = (byte)offset;
            pak[0x428] = (byte)(size >> 24); pak[0x429] = (byte)(size >> 16); pak[0x42a] = (byte)(size >> 8); pak[0x42b] = (byte)size;
            fst.CopyTo(pak, 0x440);
            return pak;
        }

        static void GeneralCases()
        {
            string path = AudioSettings.SettingsPath;
            if (File.Exists(path)) File.Delete(path);
            Check(GeneralSettings.Load().TextSpeed == 1, "text speed starts at the game's pace");
            File.WriteAllText(path, "audio_master_gain=0.5\ntext_speed=7\n");
            Check(GeneralSettings.Load().TextSpeed == 1, "an unknown text speed reads as 1x");
            var general = GeneralSettings.Load();
            general.TextSpeed = 3;
            general.Save();
            string saved = File.ReadAllText(path);
            Check(saved.Contains("text_speed=3") && saved.Contains("audio_master_gain=0.5") && GeneralSettings.Load().TextSpeed == 3,
                  "text speed is saved beside the other runtime settings");
            Check(GeneralSettings.Load().Language == 0 && GeneralSettings.LanguageNames[0] == "Automatic" &&
                  GeneralSettings.LanguageNames[3] == "Fran\u00E7ais" && GeneralSettings.LanguageNames[7] == "\u65E5\u672C\u8A9E",
                  "the game's languages are listed in their own names, Automatic first");
            general = GeneralSettings.Load();
            general.Language = 4;
            general.Save();
            saved = File.ReadAllText(path);
            Check(saved.Contains("language=spanish") && saved.Contains("text_speed=3") && GeneralSettings.Load().Language == 4,
                  "the language is saved for the runtime");
            File.WriteAllText(path, "language=FRENCH\n");
            Check(GeneralSettings.Load().Language == 3, "the runtime's language values read in any case");
            File.WriteAllText(path, "language=klingon\n");
            Check(GeneralSettings.Load().Language == 0, "an unknown language reads as Automatic");
            File.WriteAllText(path, "language=auto\n");
            Check(GeneralSettings.Load().Language == 0, "auto reads as Automatic");
            File.Delete(path);

            // The installed disc's languages: its game.pak's language folders, Automatic first.
            string pak = Path.Combine(Path.GetTempPath(), "nebula-launcher-test-" + Guid.NewGuid().ToString("N") + ".pak");
            File.WriteAllBytes(pak, TestPak("RMGP01", "EuEnglish", "EuGerman", "EuFrench", "LayoutData", "EuSpanish", "EuItalian"));
            Check(GameDisc.GameId(pak) == "RMGP01", "game.pak's game id reads");
            var europe = GeneralSettings.Offered(pak);
            Check(string.Join(",", europe) == "0,1,2,3,4,5", "Europe's disc offers Automatic and its five languages");
            File.WriteAllBytes(pak, TestPak("RMGJ01", "JpJapanese", "LayoutData"));
            Check(string.Join(",", GeneralSettings.Offered(pak)) == "0,7", "Japan's disc offers Automatic and Japanese");
            File.WriteAllBytes(pak, new byte[] { 1, 2, 3 });
            Check(GeneralSettings.Offered(pak).Count == GeneralSettings.LanguageKeys.Length,
                  "an unreadable game.pak offers every language");
            File.Delete(pak);
        }

        // Nebula Settings open while the game runs: the in-game menu saves the same files. Settings write back
        // only what was changed in them, so the game's changes made meanwhile stay.
        static void SharedFileCases()
        {
            string runtime = AudioSettings.SettingsPath, config = GraphicsSettings.ConfigPath;
            File.WriteAllText(runtime, "input_mode=auto\naudio_master_gain=1\naudio_music_gain=1\ntext_speed=1\nlanguage=english\n");
            File.WriteAllText(config, "aa=0\nsharpness=0\nper_pixel_lighting=0\nwindow_width=1280\nwindow_height=720\n");
            var general = GeneralSettings.Load();
            var audio = AudioSettings.Load();
            var graphics = GraphicsSettings.Load();
            // The game: 2x text, music at half, SMAA and a window dragged to 1600x900.
            File.WriteAllText(runtime, "input_mode=auto\naudio_master_gain=1\naudio_music_gain=0.5\ntext_speed=2\nlanguage=english\n");
            File.WriteAllText(config, "aa=1\nsharpness=0\nper_pixel_lighting=0\nwindow_width=1600\nwindow_height=900\n");
            general.Language = 3;  // French
            general.Save();
            audio.Percent[0] = 80;
            audio.Save();
            graphics.Sharpness = 30;
            graphics.Save();
            string saved = File.ReadAllText(runtime), savedConfig = File.ReadAllText(config);
            Check(saved.Contains("language=french") && saved.Contains("text_speed=2") &&
                  saved.Contains("audio_master_gain=0.8") && saved.Contains("audio_music_gain=0.5"),
                  "runtime settings changed in the game meanwhile are kept beside those changed here");
            Check(savedConfig.Contains("sharpness=30") && savedConfig.Contains("aa=1") &&
                  savedConfig.Contains("window_width=1600") && savedConfig.Contains("window_height=900"),
                  "graphics changed in the game meanwhile are kept beside those changed here");
            // A second save writes nothing it did not change since the first.
            File.WriteAllText(config, savedConfig.Replace("aa=1", "aa=3"));
            graphics.Save();
            Check(File.ReadAllText(config).Contains("aa=3") && File.ReadAllText(config).Contains("sharpness=30"),
                  "saving again keeps a change the game made after the first save");
            File.Delete(runtime);
            File.Delete(config);
        }

        // config.ini's aspect: a name, or an index of the list before 3:2.
        static void AspectNameCases()
        {
            string config = GraphicsSettings.ConfigPath;
            File.WriteAllText(config, "aspect=4\n");
            Check(GraphicsSettings.Load().Aspect == 5, "a numeric aspect is an index of the list before 3:2 (4 is 21:9)");
            File.WriteAllText(config, "aspect=3:2\n");
            var graphics = GraphicsSettings.Load();
            Check(graphics.Aspect == 2, "aspect=3:2 reads by name");
            graphics.Aspect = 6;
            graphics.Save();
            Check(File.ReadAllText(config).Contains("aspect=32:9"), "the aspect saves by name");
            File.Delete(config);
        }

        static void SaveArchiveCases(string scratch)
        {
            string saves = NebulaPaths.Saves, parent = Path.GetDirectoryName(saves);
            string work = Path.Combine(scratch, "save-archive");
            Directory.CreateDirectory(work);
            Func<int> backupCount = delegate { return Directory.GetDirectories(parent, "saves-backup-*").Length; };
            Func<bool> noStaging = delegate { return Directory.GetDirectories(parent, "saves.import-*").Length == 0; };
            SaveArchive.GameIsRunning = delegate { return false; };

            BuildSaves(saves, 1);
            List<SaveItem> first = SaveArchive.ReadTree(saves);
            Check(first.Count == 23 && first.All(i => i.Streams.Count == 1),
                  "save fixture: 16 folders (every release's save folders) and 7 files (North America's and Europe's saves), each with a metadata stream");
            string zip = SaveArchive.Export(work);
            Check(Regex.IsMatch(Path.GetFileName(zip), @"^Nebula-Saves-\d{8}-\d{6}\.zip$"), "export writes Nebula-Saves-<yyyyMMdd-HHmmss>.zip: " + Path.GetFileName(zip));
            using (ZipArchive archive = ZipFile.OpenRead(zip))
                Check(archive.Entries.All(e => !e.FullName.Contains("tmp/") && !e.FullName.EndsWith("desktop.ini", StringComparison.Ordinal)) &&
                      archive.Entries.Count(e => e.FullName.StartsWith("streams/", StringComparison.Ordinal)) == 23,
                      "export holds one stream entry per folder and file, and leaves out /tmp and files the game does not read");
            Check(SaveArchive.Same(first, SaveArchive.ReadArchive(zip)), "the archive reads back as the exported tree");

            FileUtil.DeleteTree(saves);
            string message = SaveArchive.Import(zip, delegate { throw new Exception("asked without saves"); });
            Check(SaveArchive.Same(first, SaveArchive.ReadWholeTree(saves)), "zip round trip: every folder, file and stream, and nothing else");
            Check(backupCount() == 0 && noStaging() && !message.Contains("previous saves"), "an import without saves makes no backup: " + message);

            FileUtil.DeleteTree(saves);
            BuildSaves(saves, 2);
            List<SaveItem> second = SaveArchive.ReadWholeTree(saves);
            Check(SaveArchive.Import(zip, delegate { return false; }) == null && SaveArchive.Same(second, SaveArchive.ReadWholeTree(saves)) &&
                  backupCount() == 0 && noStaging(), "declining the replacement changes nothing");
            int asked = 0;
            message = SaveArchive.Import(zip, delegate { asked++; return true; });
            string[] backups = Directory.GetDirectories(parent, "saves-backup-*");
            Check(asked == 1 && SaveArchive.Same(first, SaveArchive.ReadWholeTree(saves)), "import over saves restores the exported tree");
            Check(backups.Length == 1 && SaveArchive.Same(second, SaveArchive.ReadWholeTree(backups[0])) && message.Contains(Path.GetFileName(backups[0])),
                  "the replaced saves are kept whole in " + Path.GetFileName(backups.FirstOrDefault() ?? ""));

            List<SaveItem> current = SaveArchive.ReadWholeTree(saves);
            Func<bool> unchanged = delegate
            {
                return SaveArchive.Same(current, SaveArchive.ReadWholeTree(saves)) && backupCount() == 1 && noStaging();
            };
            byte[] zipBytes = File.ReadAllBytes(zip);
            string truncated = Path.Combine(work, "truncated.zip");
            File.WriteAllBytes(truncated, zipBytes.Take(zipBytes.Length / 2).ToArray());
            string damaged = Path.Combine(work, "damaged.zip");
            DamageEntry(zip, damaged, "saves/" + SaveArchive.GameDataPath);
            string mismatched = Path.Combine(work, "mismatched.zip");
            File.Copy(zip, mismatched);
            using (ZipArchive archive = ZipFile.Open(mismatched, ZipArchiveMode.Update))
            {
                string name = "streams/" + SaveArchive.GameDataPath + "/galaxy.isfs.meta";
                archive.GetEntry(name).Delete();
                using (Stream stream = archive.CreateEntry(name).Open()) stream.Write(new byte[18], 0, 18);
            }
            string notZip = Path.Combine(work, "not-a-zip.zip");
            File.WriteAllText(notZip, "saves");
            string otherFormat = ZipWith(Path.Combine(work, "other-format.zip"), "{\"format\":\"other\",\"version\":1,\"items\":[]}");
            string newer = ZipWith(Path.Combine(work, "newer.zip"), "{\"format\":\"nebula-saves\",\"version\":2,\"items\":[]}");
            foreach (string bad in new[] { truncated, damaged, mismatched, notZip, otherFormat, newer })
            {
                Exception error = Failure(delegate { SaveArchive.Import(bad, null); });
                Check(error is InvalidDataException && unchanged(), Path.GetFileName(bad) + " is refused and the saves are unchanged: " +
                      (error == null ? "imported" : error.Message));
            }

            SaveArchive.Staged = delegate(string path) { if (path == SaveArchive.GameDataPath) throw new IOException("staging failed"); };
            Exception partial = Failure(delegate { SaveArchive.Import(zip, null); });
            SaveArchive.Staged = delegate { };
            Check(partial != null && partial.Message == "staging failed" && unchanged(), "an import that fails part-way leaves the saves and removes its staging");
            using (File.Open(SaveArchive.HostPath(saves, SaveArchive.GameDataPath), FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                Exception held = Failure(delegate { SaveArchive.Import(zip, null); });
                Check(held != null && unchanged(), "saves that cannot be moved aside are left as they were: " + (held == null ? "imported" : held.Message));
            }

            SaveArchive.GameIsRunning = delegate { return true; };
            Exception running = Failure(delegate { SaveArchive.Import(zip, null); });
            int archives = Directory.GetFiles(work, "Nebula-Saves-*").Length;
            Exception exportRunning = Failure(delegate { SaveArchive.Export(work); });
            SaveArchive.GameIsRunning = delegate { return false; };
            Check(running is InvalidOperationException && exportRunning is InvalidOperationException && unchanged() &&
                  Directory.GetFiles(work, "Nebula-Saves-*").Length == archives, "import and export are refused while the game runs");

            // A saves folder copied with its streams imports losslessly; files the game does not read stay behind.
            string folder = Path.Combine(work, "folder-source");
            FileUtil.CopyTreeWithStreams(backups[0], folder, null);
            SaveArchive.Import(folder, null);
            Check(SaveArchive.Same(SaveArchive.ReadTree(backups[0]), SaveArchive.ReadWholeTree(saves)), "folder import keeps every stream and leaves out /tmp and desktop.ini");

            // A zip made by another tool has no streams; the runtime rebuilds the metadata at the next start.
            string plain = Path.Combine(work, "plain.zip");
            ZipFile.CreateFromDirectory(folder, plain, CompressionLevel.Optimal, true);
            SaveArchive.Import(plain, null);
            List<SaveItem> imported = SaveArchive.ReadWholeTree(saves);
            Check(imported.Count(i => !i.IsFolder) == 7 && imported.All(i => i.Streams.Count == 0) &&
                  imported.First(i => i.Path == SaveArchive.GameDataPath).Data.SequenceEqual(GameData(2)) &&
                  imported.First(i => i.Path == SaveArchive.GameDataPathOf("524d4750")).Data.SequenceEqual(GameData(2 + 7)),
                  "a plain zip of a saves folder imports the seven files (both releases' saves) without streams");

            string invalidBanner = Path.Combine(work, "invalid-banner");
            Directory.CreateDirectory(invalidBanner);
            File.WriteAllBytes(Path.Combine(invalidBanner, "GameData.bin"), GameData(3));
            File.WriteAllBytes(Path.Combine(invalidBanner, "banner.bin"), Noise(SaveFormat.BannerSize, 3));
            message = SaveArchive.Import(invalidBanner, null);
            Check(message.Contains("banner.bin") && File.Exists(SaveArchive.HostPath(saves, SaveArchive.GameDataPath)) &&
                  !File.Exists(SaveArchive.HostPath(saves, SaveArchive.BannerPath)), "a data folder imports GameData.bin and leaves out an invalid banner");
            File.WriteAllBytes(Path.Combine(invalidBanner, "GameData.bin"), Noise(SaveFormat.GameDataSize, 4));
            Check(Failure(delegate { SaveArchive.Import(invalidBanner, null); }) is InvalidDataException, "an invalid GameData.bin is refused");

            FileUtil.WriteNamedStream(SaveArchive.HostPath(saves, SaveArchive.GameDataPath), "galaxy.isfs.meta.pending", Noise(18, 5));
            Check(Failure(delegate { SaveArchive.Export(work); }) is InvalidOperationException, "export is refused while a save is unfinished");

            FileUtil.DeleteTree(saves);
            foreach (string backup in Directory.GetDirectories(parent, "saves-backup-*")) FileUtil.DeleteTree(backup);
        }

        [STAThread]
        public static int Main(string[] args)
        {
            try
            {
                string dataRoot = Environment.GetEnvironmentVariable("NEBULA_DATA_ROOT");
                Check(!string.IsNullOrEmpty(dataRoot) && NebulaPaths.DataRoot == dataRoot, "NEBULA_DATA_ROOT replaces the user data root");
                string outDir = args[0];
                WindowSizeCases();
                PadMenuCases();
                AudioCases();
                GeneralCases();
                SharedFileCases();
                AspectNameCases();
                SaveArchiveCases(outDir);
                Application.EnableVisualStyles();
                RecordingNoticeCases(outDir);
                UpdatePromptCases(outDir);

                var first = new LaunchSession();
                Check(!first.SetupComplete, "no settings.json: setup not complete");
                Check(Program.OpensSettings(new string[0], first), "first start opens the settings");
                using (var form = new SettingsForm(first))
                {
                    Check(form.Text == "Welcome to Nebula", "first start title: " + form.Text);
                    var tabs = form.Controls.OfType<TabControl>().Single();
                    var buttons = form.Controls.OfType<Button>().ToList();
                    Check(buttons.Any(b => b.Text == "Save and play"), "first start offers Save and play");
                    Check(buttons.Where(b => b.Text == "Save").All(b => !b.Visible), "first start hides Save");
                    Render(form, tabs, outDir, "first");
                }

                first.Save();
                Check(File.Exists(Path.Combine(dataRoot, "settings.json")), "Save writes settings.json under the data root");
                var later = new LaunchSession();
                Check(later.SetupComplete, "settings.json version 3: setup complete");
                Check(!Program.OpensSettings(new string[0], later), "later starts go straight to the game");
                Check(Program.OpensSettings(new[] { "--settings" }, later), "--settings opens the settings");
                using (var form = new SettingsForm(later))
                {
                    Check(form.Text == "Nebula settings", "later title: " + form.Text);
                    Render(form, form.Controls.OfType<TabControl>().Single(), outDir, "later");
                }
                GpuCases();
                DisplayNoticeCases();
                GpuFormCases(later, outDir);
                RecordingEncodingCases(later.App);

                // TryLaunch starts <App>\NebulaNative.exe, here a stub that records its arguments.
                string marker = Path.Combine(later.App, "stub-launch.txt");
                if (File.Exists(marker)) File.Delete(marker);
                string error = later.TryLaunch();
                Check(error == null, "TryLaunch starts the game: " + (error ?? "ok"));
                for (int i = 0; i < 100 && !File.Exists(marker); i++) Thread.Sleep(100);
                Check(File.Exists(marker), "stub game ran");
                string[] lines = File.ReadAllLines(marker);
                Check(lines.Length == 4 && lines[0] == "--pak" && lines[1] == Path.Combine(later.Content, "game.pak"),
                      "the game receives game.pak: " + string.Join(" ", lines));
                Check(lines[2] == "--nand" && lines[3].StartsWith(dataRoot, StringComparison.OrdinalIgnoreCase),
                      "saves live under the data root: " + (lines.Length > 3 ? lines[3] : ""));
                return 0;
            }
            catch (Exception error)
            {
                Console.WriteLine(error.Message);
                return 1;
            }
        }
    }
}
