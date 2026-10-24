using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;

namespace Nebula.Launcher
{
    internal enum RecordingMode { None, Lightweight, Detailed }

    internal static class LaunchOptions
    {
        /// <summary>
        /// The game reads display and graphics settings from config.ini (the settings app and the in-game menu
        /// edit it); only paths and diagnostics travel on the command line and in the environment.
        /// </summary>
        public static ProcessStartInfo Create(string app, string content, string saves, RecordingMode recording)
        {
            var game = new ProcessStartInfo(Path.Combine(app, "NebulaNative.exe"),
                "--pak " + ProcessRunner.Quote(Path.Combine(content, "game.pak")) + " --nand " + ProcessRunner.Quote(saves));
            game.UseShellExecute = false;
            game.CreateNoWindow = true;
            game.WorkingDirectory = app;
            game.RedirectStandardOutput = game.RedirectStandardError = recording != RecordingMode.None;
            if (game.RedirectStandardOutput) game.StandardOutputEncoding = game.StandardErrorEncoding = Encoding.UTF8;
            foreach (var name in game.EnvironmentVariables.Keys.Cast<string>().Where(n => n.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)).ToList())
                game.EnvironmentVariables.Remove(name);
            game.EnvironmentVariables["GALAXY_STREAMLINE_DIR"] = GraphicsSettings.StreamlineDir;
            ApplyRecording(game, recording);
            return game;
        }

        internal static void ApplyRecording(ProcessStartInfo start, RecordingMode mode)
        {
            bool record = mode != RecordingMode.None, detailed = mode == RecordingMode.Detailed;
            var env = start.EnvironmentVariables;
            foreach (var flag in new[] { "GALAXY_TRACE_PRESENT_STATS", "GALAXY_MONITOR_POINTER_LATENCY",
                "GALAXY_MONITOR_FRAME_TAILS", "GALAXY_MONITOR_DISPLAY_LAYOUT", "GALAXY_FRAME_TELEMETRY" })
                env[flag] = record ? "1" : "0";
            env["GALAXY_TRACE_GX_STALLS"] = detailed ? "1" : "0";
            env["GALAXY_FRAME_TIMELINE"] = record ? "1" : "0";
            env["GALAXY_FRAME_TIMELINE_EVENTS"] = "4000000";
            env["GALAXY_GPU_TIMESTAMPS"] = detailed ? "1" : "0";
            env["GALAXY_TRACE_GX_MICROPROFILE"] = "0";
            env["GALAXY_GX_FRAME_TIMING_SAMPLE"] = detailed ? "32" : "0";
            env["GALAXY_GX_PSO_CYCLES"] = detailed ? "1" : "0";
            env["GALAXY_TRACE_GX_STALL_US"] = "20000";
        }

        public static void ValidatePair(string app)
        {
            string manifest = Path.Combine(app, "validation-pair.json");
            if (!File.Exists(manifest)) return; // Standard setup validates its own payload.
            var files = Json.Obj(Json.Load(manifest), "files");
            if (files == null) throw new InvalidDataException("The portable build manifest is missing.");
            string prefix = Path.GetFullPath(app).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            foreach (var file in files)
            {
                string path = Path.GetFullPath(Path.Combine(app, file.Key));
                if (!path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase) || !File.Exists(path) ||
                    !string.Equals(FileUtil.Sha256File(path), Convert.ToString(file.Value), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidDataException("Portable build file mismatch: " + file.Key + ". Extract the complete ZIP again.");
            }
        }

        // Called once per portable beta before opening the shared save/cache.
        public static void PreserveUserData(string dataRoot, string backupKey)
        {
            if (backupKey.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || backupKey == "." || backupKey == "..")
                throw new ArgumentException("Invalid backup identity.");
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
                            throw new IOException("Save/cache backup verification failed: " + relative + ". The game was not started.");
                    }
                }
                foreach (string name in new[] { "settings.json", "config.ini" })
                {
                    string source = Path.Combine(dataRoot, name), target = Path.Combine(snapshot, name);
                    if (!File.Exists(source)) continue;
                    if (!NativeMethods.CopyFileW(source, target, true)) throw new IOException("Settings backup failed: " + name);
                    var a = FileUtil.StreamDigests(source); var b = FileUtil.StreamDigests(target);
                    if (a.Count != b.Count || a.Any(p => !b.ContainsKey(p.Key) || b[p.Key] != p.Value))
                        throw new IOException("Settings backup verification failed: " + name);
                }
                Json.Save(completed, new Dictionary<string, object> { { "createdUtc", DateTime.UtcNow.ToString("o") },
                    { "source", dataRoot }, { "version", backupKey }, { "snapshot", snapshot } });
            }
        }
    }
}
