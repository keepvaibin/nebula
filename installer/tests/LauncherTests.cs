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

        // Windowed sizes for a fixed aspect, and the size chosen when the aspect changes.
        static void WindowSizeCases()
        {
            Check(GraphicsSettings.AspectWindowSizes(0, 4000, 4000).Count == 0, "Auto aspect has no fixed window sizes");
            int[,] ratios = { { 0, 0 }, { 4, 3 }, { 16, 10 }, { 16, 9 }, { 21, 9 }, { 32, 9 } };
            for (int aspect = 1; aspect < GraphicsSettings.Aspects.Length; aspect++)
            {
                var sizes = GraphicsSettings.AspectWindowSizes(aspect, 2600, 1400);
                Check(sizes.Count >= 3, GraphicsSettings.Aspects[aspect] + ": at least three window sizes fit 2600 x 1400");
                Check(sizes.All(s => s.Width * ratios[aspect, 1] == s.Height * ratios[aspect, 0]), GraphicsSettings.Aspects[aspect] + ": every size has the exact aspect");
                Check(sizes.All(s => s.Width <= 2600 && s.Height <= 1400), GraphicsSettings.Aspects[aspect] + ": every size fits");
                Check(sizes.Zip(sizes.Skip(1), (a, b) => a.Height < b.Height).All(x => x), GraphicsSettings.Aspects[aspect] + ": smallest first");
            }
            var wide = GraphicsSettings.AspectWindowSizes(4, 2600, 1400);
            Check(wide[GraphicsSettings.SnapWindowSize(wide, new Size(1440, 1080))] == new Size(2520, 1080), "21:9 from 1440 x 1080 snaps to 2520 x 1080");
            Check(wide[GraphicsSettings.SnapWindowSize(wide, new Size(1280, 720))] == new Size(1680, 720), "21:9 from 1280 x 720 snaps to 1680 x 720");
            var small = GraphicsSettings.AspectWindowSizes(4, 1900, 1000);
            Check(small[GraphicsSettings.SnapWindowSize(small, new Size(1920, 1080))] == small[small.Count - 1], "a size taller than the screen allows snaps to the largest");
        }

        // ---- Save export and import ----------------------------------------------------------------------------

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
            Check(GeneralSettings.Load().Language == 0 && GeneralSettings.Languages.Length == 3 &&
                  GeneralSettings.Languages[1] == "Français" && GeneralSettings.Languages[2] == "Español",
                  "the game's languages are listed in their own names, English first");
            general = GeneralSettings.Load();
            general.Language = 2;
            general.Save();
            saved = File.ReadAllText(path);
            Check(saved.Contains("language=spanish") && saved.Contains("text_speed=3") && GeneralSettings.Load().Language == 2,
                  "the language is saved for the runtime");
            File.WriteAllText(path, "language=FRENCH\n");
            Check(GeneralSettings.Load().Language == 1, "the runtime's language values read in any case");
            File.WriteAllText(path, "language=klingon\n");
            Check(GeneralSettings.Load().Language == 0, "an unknown language reads as English");
            File.Delete(path);
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
            general.Language = 1;
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
            Check(first.Count == 18 && first.All(i => i.Streams.Count == 1), "save fixture: 12 folders and 6 files, each with a metadata stream");
            string zip = SaveArchive.Export(work);
            Check(Regex.IsMatch(Path.GetFileName(zip), @"^Nebula-Saves-\d{8}-\d{6}\.zip$"), "export writes Nebula-Saves-<yyyyMMdd-HHmmss>.zip: " + Path.GetFileName(zip));
            using (ZipArchive archive = ZipFile.OpenRead(zip))
                Check(archive.Entries.All(e => !e.FullName.Contains("tmp/") && !e.FullName.EndsWith("desktop.ini", StringComparison.Ordinal)) &&
                      archive.Entries.Count(e => e.FullName.StartsWith("streams/", StringComparison.Ordinal)) == 18,
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
            Check(imported.Count(i => !i.IsFolder) == 6 && imported.All(i => i.Streams.Count == 0) &&
                  imported.First(i => i.Path == SaveArchive.GameDataPath).Data.SequenceEqual(GameData(2)), "a plain zip of a saves folder imports the six files without streams");

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
                GeneralCases();
                SharedFileCases();
                SaveArchiveCases(outDir);
                Application.EnableVisualStyles();
                RecordingNoticeCases(outDir);

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
