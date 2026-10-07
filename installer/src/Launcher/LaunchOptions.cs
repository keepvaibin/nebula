using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;

namespace Nebula.Launcher
{
    internal enum RecordingMode { None, Lightweight, Detailed }

    internal static class LaunchOptions
    {
        public static ProcessStartInfo Create(string app, string content, string saves,
            DisplayPlan plan, bool borderless, RecordingMode recording)
        {
            var start = new ProcessStartInfo(Path.Combine(app, "NebulaRuntime.exe"),
                ProcessRunner.Quote(content) + " " + ProcessRunner.Quote(Path.Combine(app, "RMGE01_game.dll")));
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            start.WorkingDirectory = app;
            start.RedirectStandardOutput = start.RedirectStandardError = recording != RecordingMode.None;
            foreach (var name in start.EnvironmentVariables.Keys.Cast<string>().Where(n => n.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)).ToList())
                start.EnvironmentVariables.Remove(name);
            foreach (var entry in (System.Collections.IList)Json.ParseAny(File.ReadAllText(Path.Combine(app, "runtime-env.json"))))
            {
                var pair = (Dictionary<string, object>)entry;
                start.EnvironmentVariables[Json.Str(pair, "name")] = Json.Str(pair, "value");
            }
            var env = start.EnvironmentVariables;
            env["GALAXY_NAND_ROOT"] = saves;
            env["GALAXY_WINDOW_WIDTH"] = plan.OutputWidth.ToString();
            env["GALAXY_WINDOW_HEIGHT"] = plan.OutputHeight.ToString();
            env["GALAXY_FULLSCREEN"] = borderless ? "1" : "0";
            env["GALAXY_EXCLUSIVE_FULLSCREEN"] = "0";
            env["GALAXY_EFB_SCALE"] = plan.EfbScale.ToString();
            env["GALAXY_EXPERIMENTAL_NATIVE_4_3"] = plan.NativeFourThree ? "1" : "0";
            env["GALAXY_EXPERIMENTAL_DYNAMIC_ASPECT"] = "0";
            env["GALAXY_EXPERIMENTAL_ULTRAWIDE_ASPECT"] =
                plan.NativeFourThree || Math.Abs(plan.AspectRatio - 16.0 / 9.0) < 1e-10 ? "" : plan.Aspect;
            env["GALAXY_NATIVE_THP_VIDEO_DECODE"] = "1";
            env["GALAXY_DUSK_DSP_HLE"] = "0";
            ApplyRecording(start, recording);
            return start;
        }

        internal static void ApplyRecording(ProcessStartInfo start, RecordingMode mode)
        {
            bool record = mode != RecordingMode.None, detailed = mode == RecordingMode.Detailed;
            var env = start.EnvironmentVariables;
            foreach (var flag in new[] { "GALAXY_TRACE_PRESENT_STATS", "GALAXY_MONITOR_POINTER_LATENCY",
                "GALAXY_MONITOR_FRAME_TAILS", "GALAXY_MONITOR_DISPLAY_LAYOUT", "GALAXY_FRAME_TELEMETRY" })
                env[flag] = record ? "1" : "0";
            env["GALAXY_TRACE_NATIVE_THP_BOUNDARY"] = detailed ? "1" : "0";
            env["GALAXY_TRACE_GX_STALLS"] = detailed ? "1" : "0";
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
