using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace Nebula.Launcher
{
    internal enum RecordingMode { None, Lightweight, Detailed }

    /// <summary>
    /// The game's programs beside the launcher, one per CPU level: NebulaNative.exe (AVX2; on a CPU without it its
    /// entry starts the x86-64-v2 program and waits) and NebulaNative-x86-64-v2.exe (SSE4.2), and the Direct3D 11
    /// line's pair once it merges. The launcher starts the one the CPU runs, so the process it watches is the game,
    /// and finds a running game by any of them.
    /// </summary>
    internal static class GamePrograms
    {
        internal static readonly string[] Names = { "NebulaNative", "NebulaNative-x86-64-v2", "NebulaNativeD11", "NebulaNativeD11-x86-64-v2" };

        // PF_AVX2_INSTRUCTIONS_AVAILABLE: the CPU and the OS support AVX2 (tests replace it).
        internal static Func<bool> Avx2 = () =>
        {
            try { return NativeMethods.IsProcessorFeaturePresent(40); }
            catch (Exception) { return false; }
        };

        /// <summary>The program a game start runs: NebulaNative.exe, or on a CPU without AVX2 the x86-64-v2 program
        /// when the package has it (else NebulaNative.exe, whose own check then decides).</summary>
        public static string Start(string app)
        {
            string v2 = Path.Combine(app, "NebulaNative-x86-64-v2.exe");
            return !Avx2() && File.Exists(v2) ? v2 : Path.Combine(app, "NebulaNative.exe");
        }

        /// <summary>Whether <paramref name="path"/> is one of the game's programs in <paramref name="app"/>.</summary>
        public static bool IsGame(string app, string path)
        {
            if (string.IsNullOrEmpty(path)) return false;
            foreach (string name in Names)
                if (string.Equals(Path.GetFullPath(path), Path.GetFullPath(Path.Combine(app, name + ".exe")), StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }

        /// <summary>This install's running game processes, every program (the caller disposes them).</summary>
        public static List<Process> Running(string app)
        {
            var running = new List<Process>();
            foreach (string name in Names)
                foreach (var process in Process.GetProcessesByName(name))
                {
                    try
                    {
                        if (IsGame(app, process.MainModule.FileName)) { running.Add(process); continue; }
                    }
                    catch (Exception) { }
                    process.Dispose();
                }
            return running;
        }
    }

    internal static class LaunchOptions
    {
        /// <summary>
        /// The game reads display and graphics settings from config.ini (the settings app and the in-game menu
        /// edit it); only paths and diagnostics travel on the command line and in the environment.
        /// </summary>
        public static ProcessStartInfo Create(string app, string content, string saves, RecordingMode recording)
        {
            var game = new ProcessStartInfo(GamePrograms.Start(app),
                "--pak " + ProcessRunner.Quote(Path.Combine(content, "game.pak")) + " --saves " + ProcessRunner.Quote(saves) +
                (recording != RecordingMode.None ? " --diagnostics 1" : ""));
            game.UseShellExecute = false;
            game.CreateNoWindow = true;
            game.WorkingDirectory = app;
            game.RedirectStandardOutput = game.RedirectStandardError = recording != RecordingMode.None;
            if (game.RedirectStandardOutput) game.StandardOutputEncoding = game.StandardErrorEncoding = Encoding.UTF8;
            foreach (var name in game.EnvironmentVariables.Keys.Cast<string>().Where(n => n.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)).ToList())
                game.EnvironmentVariables.Remove(name);
            return game;
        }

        public static void ValidatePair(string app)
        {
            string manifest = Path.Combine(app, "validation-pair.json");
            if (!File.Exists(manifest)) return; // Standard setup validates its own payload.
            var files = Json.Obj(Json.Load(manifest), "files");
            if (files == null) throw new InvalidDataException(NebulaText.Of("The portable build manifest is missing."));
            string prefix = Path.GetFullPath(app).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            foreach (var file in files)
            {
                string path = Path.GetFullPath(Path.Combine(app, file.Key));
                if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) || !File.Exists(path) ||
                    !string.Equals(FileUtil.Sha256File(path), Convert.ToString(file.Value), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException(NebulaText.Of("Portable build file mismatch: {0}. Extract the complete ZIP again.", file.Key));
            }
        }

        // Called once per portable beta before opening the shared save/cache.
        public static void PreserveUserData(string dataRoot, string backupKey)
        {
            if (backupKey.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || backupKey == "." || backupKey == "..")
                throw new ArgumentException(NebulaText.Of("Invalid backup identity."));
            string backup = Path.Combine(dataRoot, "backups", backupKey);
            string completed = Path.Combine(backup, "backup.json");
            if (File.Exists(completed)) return;
            Directory.CreateDirectory(backup);
            using (var owner = new FileStream(Path.Combine(backup, "owner.lock"), FileMode.OpenOrCreate, FileAccess.Write, FileShare.None))
            {
                if (File.Exists(completed)) return;
                string snapshot = Path.Combine(backup, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + "-" + Guid.NewGuid().ToString("N"));
                Directory.CreateDirectory(snapshot);
                foreach (string name in new[] { "saves", "shadercache" })
                {
                    string source = Path.Combine(dataRoot, name), target = Path.Combine(snapshot, name);
                    if (!Directory.Exists(source)) continue;
                    FileUtil.CopyTreeWithStreams(source, target, null);
                    var entries = new List<string> { source };
                    entries.AddRange(Directory.GetDirectories(source, "*", SearchOption.AllDirectories));
                    entries.AddRange(Directory.GetFiles(source, "*", SearchOption.AllDirectories));
                    foreach (string entry in entries)
                    {
                        string relative = entry.Substring(source.Length).TrimStart('\\');
                        var a = FileUtil.StreamDigests(entry);
                        var b = FileUtil.StreamDigests(Path.Combine(target, relative));
                        if (a.Count != b.Count || a.Any(p => !b.ContainsKey(p.Key) || b[p.Key] != p.Value))
                            throw new IOException(NebulaText.Of("Save/cache backup verification failed: {0}. The game was not started.", relative));
                    }
                }
                foreach (string name in new[] { "settings.json", "config.ini" })
                {
                    string source = Path.Combine(dataRoot, name), target = Path.Combine(snapshot, name);
                    if (!File.Exists(source)) continue;
                    if (!NativeMethods.CopyFileW(source, target, true)) throw new IOException(NebulaText.Of("Settings backup failed: {0}", name));
                    var a = FileUtil.StreamDigests(source); var b = FileUtil.StreamDigests(target);
                    if (a.Count != b.Count || a.Any(p => !b.ContainsKey(p.Key) || b[p.Key] != p.Value))
                        throw new IOException(NebulaText.Of("Settings backup verification failed: {0}", name));
                }
                Json.Save(completed, new Dictionary<string, object> { { "createdUtc", DateTime.UtcNow.ToString("o") },
                    { "source", dataRoot }, { "version", backupKey }, { "snapshot", snapshot } });
            }
        }
    }
}
