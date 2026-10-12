using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
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

        [STAThread]
        public static int Main(string[] args)
        {
            try
            {
                string dataRoot = Environment.GetEnvironmentVariable("NEBULA_DATA_ROOT");
                Check(!string.IsNullOrEmpty(dataRoot) && NebulaPaths.DataRoot == dataRoot, "NEBULA_DATA_ROOT replaces the user data root");
                string outDir = args[0];
                Application.EnableVisualStyles();

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

                // TryLaunch starts <App>\NebulaRuntime.exe, here a stub that records its arguments.
                string marker = Path.Combine(later.App, "stub-launch.txt");
                if (File.Exists(marker)) File.Delete(marker);
                string error = null;
                for (int attempt = 0; attempt < 120; attempt++)
                {
                    error = later.TryLaunch();
                    if (error != "Nebula is already running.") break;
                    Thread.Sleep(1000); // another session on this machine; wait for it
                }
                if (error == "Nebula is already running.")
                {
                    Console.WriteLine("SKIP: launch arguments (another NebulaRuntime stayed open for 2 minutes)");
                    return 0;
                }
                Check(error == null, "TryLaunch starts the runtime: " + (error ?? "ok"));
                for (int i = 0; i < 100 && !File.Exists(marker); i++) Thread.Sleep(100);
                Check(File.Exists(marker), "stub runtime ran");
                string[] lines = File.ReadAllLines(marker);
                Check(lines.Length >= 3 && lines[0] == later.Content, "runtime receives the content folder: " + (lines.Length > 0 ? lines[0] : ""));
                Check(lines[1] == Path.Combine(later.App, "RMGE01_game.dll"), "runtime receives the module path");
                Check(lines[2].StartsWith(dataRoot, StringComparison.OrdinalIgnoreCase), "saves live under the data root: " + lines[2]);
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
