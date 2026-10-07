using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;
using Nebula;
using Nebula.Launcher;

internal static class PortableLauncherTests
{
    static void Check(bool ok, string message) { if (!ok) throw new Exception(message); }
    [STAThread]
    public static int Main(string[] args)
    {
        string scratch = args[0], app = args[1];
        Directory.CreateDirectory(scratch);
        LaunchOptions.ValidatePair(app);
        string broken = Path.Combine(scratch, "mismatched-package");
        Directory.CreateDirectory(broken);
        File.Copy(Path.Combine(app, "runtime-env.json"), Path.Combine(broken, "runtime-env.json"));
        Json.Save(Path.Combine(broken, "validation-pair.json"), new System.Collections.Generic.Dictionary<string, object> {
            { "files", new System.Collections.Generic.Dictionary<string, object> { { "runtime-env.json", FileUtil.Sha256File(Path.Combine(broken, "runtime-env.json")) } } } });
        File.AppendAllText(Path.Combine(broken, "runtime-env.json"), " ");
        bool rejected = false;
        try { LaunchOptions.ValidatePair(broken); } catch(InvalidDataException) { rejected = true; }
        Check(rejected, "Mismatched portable package accepted");
        string profile = Path.Combine(scratch, "profile");
        Directory.CreateDirectory(Path.Combine(profile, "saves", "title"));
        Directory.CreateDirectory(Path.Combine(profile, "shadercache"));
        File.WriteAllText(Path.Combine(profile, "saves", "title", "save.bin"), "original-progress");
        File.WriteAllText(Path.Combine(profile, "shadercache", "cache.bin"), "original-cache");
        File.WriteAllText(Path.Combine(profile, "config.ini"), "original-options");
        LaunchOptions.PreserveUserData(profile, "test-beta");
        var marker = Json.Load(Path.Combine(profile, "backups", "test-beta", "backup.json"));
        string backup = Json.Str(marker, "snapshot");
        Check(File.ReadAllText(Path.Combine(backup, "saves", "title", "save.bin")) == "original-progress", "Save backup missing");
        Check(File.ReadAllText(Path.Combine(backup, "shadercache", "cache.bin")) == "original-cache", "Cache backup missing");
        Check(File.ReadAllText(Path.Combine(backup, "config.ini")) == "original-options", "Options backup missing");
        File.WriteAllText(Path.Combine(profile, "saves", "title", "save.bin"), "new-progress");
        LaunchOptions.PreserveUserData(profile, "test-beta");
        Check(File.ReadAllText(Path.Combine(profile, "saves", "title", "save.bin")) == "new-progress", "Backup overwrote live progress");
        Check(File.ReadAllText(Path.Combine(backup, "saves", "title", "save.bin")) == "original-progress", "Original backup overwritten");
        var plan = DisplaySettings.Resolve(1280, 720, "16:9", "480", 1920, 1080);
        foreach (RecordingMode mode in Enum.GetValues(typeof(RecordingMode)))
        {
            var start = LaunchOptions.Create(app, "content folder", Path.Combine(profile, "saves"), plan, false, mode);
            bool record = mode != RecordingMode.None, detail = mode == RecordingMode.Detailed;
            Check(start.RedirectStandardOutput == record && start.RedirectStandardError == record && start.CreateNoWindow, "Pipe ownership wrong");
            Check(start.EnvironmentVariables["GALAXY_NAND_ROOT"] == Path.Combine(profile, "saves"), "Save root changed between recording modes");
            Check(start.EnvironmentVariables["GALAXY_EFB_SCALE"] == "1", "Base scale wrong");
            Check(start.EnvironmentVariables["GALAXY_TRACE_PRESENT_STATS"] == (record ? "1" : "0"), "Frame monitoring wrong");
            Check(start.EnvironmentVariables["GALAXY_FRAME_TELEMETRY"] == (record ? "1" : "0"), "Telemetry override wrong");
            Check(start.EnvironmentVariables["GALAXY_GPU_TIMESTAMPS"] == (detail ? "1" : "0"), "Routine GPU timing leak");
            Check(start.EnvironmentVariables["GALAXY_GX_FRAME_TIMING_SAMPLE"] == (detail ? "32" : "0"), "Routine renderer clocks leak");
            Check(start.EnvironmentVariables["GALAXY_GX_PSO_CYCLES"] == (detail ? "1" : "0"), "Routine cycle clocks leak");
        }
        // Render the actual form without starting a game or writing user settings.
        File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "portable.json"), "{}");
        using (var form = new LauncherForm())
        {
            form.StartPosition = FormStartPosition.Manual;
            form.Location = new Point(-20000, -20000);
            form.ShowInTaskbar = false;
            form.Show();
            Application.DoEvents();
            var modeBox = form.Controls.OfType<ComboBox>().Single(c => c.Items.Contains("No recording"));
            Check(modeBox.SelectedIndex == 0 && modeBox.Items.Count == 3, "UI recording default/options wrong");
            using (var image = new Bitmap(form.Width, form.Height)) { form.DrawToBitmap(image, new Rectangle(Point.Empty, form.Size)); image.Save(Path.Combine(scratch, "launcher.png")); }
            form.Close();
        }
        Console.WriteLine("PASS: three real launch policies, shared save root, no-recording pipe/diagnostic overrides, default UI, one-time save/cache/options backup and retained progress. No game executed.");
        Console.WriteLine("Fixture artifacts: " + scratch);
        return 0;
    }
}
