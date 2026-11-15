using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace Nebula.Launcher
{
    /// <summary>One recommended setting: a config.ini key, its value when the benchmark ran, and the recommended one.</summary>
    internal sealed class BenchmarkSetting
    {
        public string Key;
        public int Current, Recommended;
    }

    /// <summary>Whether a result still holds: Current (config.ini as it ran), Applied (config.ini as it recommends),
    /// Stale (a setting, Nebula or the graphics card changed since: shown, not applied).</summary>
    internal enum BenchmarkState { Current, Applied, Stale }

    /// <summary>
    /// The benchmark's result file (perf/bench, schema 1): how the run ended, the verdict, the recommended settings
    /// when it completed, and what it ran with (the player's settings, the adapter) to tell whether it still holds.
    /// </summary>
    internal sealed class BenchmarkResult
    {
        public string Status = "", Failure = "", Verdict = "none";
        public bool Fits, CpuBound;
        public List<BenchmarkSetting> Settings = new List<BenchmarkSetting>();
        // `settings`: the player's config.ini values when it ran (every key the recommendation depends on).
        public Dictionary<string, double> RanWith = new Dictionary<string, double>();
        public string Gpu = "";
        public long VendorId, DeviceId;

        public bool Complete { get { return Status == "complete"; } }

        // The keys the benchmark may recommend, the ones Apply writes (the contract's list).
        internal static readonly string[] Keys = { "internal_height", "msaa_samples", "ambient_occlusion", "enhanced_lighting", "aa", "smaa_quality", "bloom" };

        /// <summary>The result, or null when the file is not schema 1's.</summary>
        public static BenchmarkResult Parse(string json)
        {
            Dictionary<string, object> root;
            try { root = Json.Parse(json); }
            catch (Exception) { return null; }
            object schema;
            if (root == null || !root.TryGetValue("schema", out schema) || !(schema is int || schema is long) || Convert.ToInt64(schema) != 1) return null;
            var result = new BenchmarkResult
            {
                Status = Json.Str(root, "status") ?? "",
                Failure = Json.Str(root, "failure") ?? "",
            };
            var summary = Json.Obj(root, "summary");
            result.Verdict = Json.Str(summary, "verdict") ?? "none";
            var device = Json.Obj(root, "device");
            result.Gpu = Json.Str(device, "gpu") ?? "";
            int id;
            if (Number(device, "vendor_id", out id)) result.VendorId = id;
            if (Number(device, "device_id", out id)) result.DeviceId = id;
            var ran = Json.Obj(root, "settings");
            if (ran != null)
                foreach (var pair in ran)
                {
                    if (pair.Value is bool) result.RanWith[pair.Key] = (bool)pair.Value ? 1 : 0;
                    else if (pair.Value != null && !(pair.Value is string))
                        try { result.RanWith[pair.Key] = Convert.ToDouble(pair.Value, CultureInfo.InvariantCulture); }
                        catch (Exception) { }
                }
            var recommendation = Json.Obj(root, "recommendation");
            if (recommendation != null)
            {
                result.Fits = Json.Bool(recommendation, "fits");
                result.CpuBound = Json.Bool(recommendation, "cpu_bound");
                foreach (var item in Json.List(recommendation, "settings") ?? (IList)new object[0])
                {
                    var row = item as Dictionary<string, object>;
                    string key = Json.Str(row, "key");
                    int current, recommended;
                    if (key == null || Array.IndexOf(Keys, key) < 0 || !Number(row, "current", out current) || !Number(row, "recommended", out recommended)) continue;
                    result.Settings.Add(new BenchmarkSetting { Key = key, Current = current, Recommended = recommended });
                }
            }
            return result;
        }

        private static bool Number(Dictionary<string, object> row, string key, out int value)
        {
            value = 0;
            object raw;
            if (row == null || !row.TryGetValue(key, out raw) || raw == null || raw is bool || raw is string) return false;
            try { value = (int)Math.Round(Convert.ToDouble(raw, CultureInfo.InvariantCulture)); return true; }
            catch (Exception) { return false; }
        }

        /// <summary>The rows whose recommended value differs from the one it ran with.</summary>
        public List<BenchmarkSetting> Changes { get { return Settings.Where(s => s.Current != s.Recommended).ToList(); } }

        // The engine's value of a key absent from config.ini (runtime/src/ui/settings_table.tsv, Windows and Android).
        private static readonly Dictionary<string, double> Defaults = new Dictionary<string, double>
        {
            { "internal_height", 0 }, { "upscaler", 0 }, { "msaa_samples", 1 }, { "ambient_occlusion", 0 },
            { "ambient_occlusion_ssao", 0 }, { "enhanced_lighting", 0 }, { "per_pixel_lighting", 0 }, { "aa", 0 },
            { "smaa_quality", 2 }, { "bloom", 100 }, { "vsync_interval", 0 }, { "max_fps", 60 }, { "frame_generation", 0 },
            { "ray_tracing_enabled", 0 }, { "display_mode", 1 },
        };
        private static readonly HashSet<string> Flags = new HashSet<string>
        {
            "ambient_occlusion", "ambient_occlusion_ssao", "enhanced_lighting", "per_pixel_lighting", "ray_tracing_enabled",
        };
        // Choices config.ini may hold by name, the n-th name meaning n (render_config.cpp).
        private static readonly Dictionary<string, string[]> Names = new Dictionary<string, string[]>
        {
            { "upscaler", new[] { "bilinear", "fsr", "fsr3", "dlss", "xess", "sgsr2", "metalfx" } },
            { "aa", new[] { "off", "smaa", "auto", "fxaa" } },
            { "smaa_quality", new[] { "low", "medium", "high", "ultra" } },
            { "display_mode", new[] { "windowed", "borderless", "fullscreen" } },
        };

        /// <summary>A key's value as the engine reads config.ini's text (null: a key this check does not know).</summary>
        internal static double? ConfigValue(Dictionary<string, string> config, string key)
        {
            double fallback;
            if (!Defaults.TryGetValue(key, out fallback)) return null;
            string text;
            if (config == null || !config.TryGetValue(key, out text)) return fallback;
            text = text.Trim();
            if (Flags.Contains(key)) return text == "1" || text == "true" ? 1 : text == "0" || text == "false" ? 0 : fallback;
            string[] names;
            if (Names.TryGetValue(key, out names))
            {
                int index = Array.FindIndex(names, n => n.Equals(text, StringComparison.OrdinalIgnoreCase));
                if (index >= 0) return index;
            }
            double value;
            return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out value) ? value : fallback;
        }

        /// <summary>
        /// Whether this result still holds (perf/bench's rule): a run this launcher's version started (<paramref
        /// name="ranBy"/>, recorded when it ended, against <paramref name="version"/>), on the adapter the game last
        /// started on (adapter.ini, when there is one), and config.ini holding every value it ran with (Current) or,
        /// after Apply, the recommended ones in their place (Applied). Anything else is Stale.
        /// </summary>
        public BenchmarkState StateNow(Dictionary<string, string> config, Dictionary<string, string> adapter, string ranBy, string version)
        {
            if (!Complete || RanWith.Count == 0 || ranBy != version) return BenchmarkState.Stale;
            if (adapter != null && !SameAdapter(adapter)) return BenchmarkState.Stale;
            bool current = true, applied = true;
            // With enhanced lighting or ray tracing on, the engine lights at the pixel whatever per_pixel_lighting holds
            // (render_config.h lights_at_pixel), and the settings window's Save writes it from the Lighting row: it is
            // not compared then. A run with either key different already fails on that key.
            bool perPixelInert = ConfigValue(config, "enhanced_lighting") == 1 || ConfigValue(config, "ray_tracing_enabled") == 1;
            foreach (var pair in RanWith)
            {
                if (pair.Key == "per_pixel_lighting" && perPixelInert) continue;
                double? now = ConfigValue(config, pair.Key);
                if (now == null) continue;   // a key the launcher cannot read stays out of the check
                var row = Settings.FirstOrDefault(s => s.Key == pair.Key);
                if (Math.Abs(now.Value - pair.Value) > 0.0005) current = false;
                if (Math.Abs(now.Value - (row != null ? row.Recommended : pair.Value)) > 0.0005) applied = false;
            }
            return current ? BenchmarkState.Current : applied ? BenchmarkState.Applied : BenchmarkState.Stale;
        }

        // adapter.ini (the game's last normal start): the same PCI ids, or with no ids the same name.
        private bool SameAdapter(Dictionary<string, string> adapter)
        {
            string vendor, device, name;
            long v, d;
            if (VendorId != 0 && adapter.TryGetValue("vendor_id", out vendor) && adapter.TryGetValue("device_id", out device) &&
                long.TryParse(vendor.Trim(), out v) && long.TryParse(device.Trim(), out d) && v != 0)
                return v == VendorId && d == DeviceId;
            if (Gpu.Length == 0 || !adapter.TryGetValue("description", out name)) return true;
            // The result keeps printable ASCII, at most 96 bytes.
            string ascii = new string(name.Trim().Where(c => c >= 0x20 && c < 0x7f).ToArray());
            return ascii.Substring(0, Math.Min(96, ascii.Length)) == Gpu;
        }

        /// <summary>The verdict's sentence.</summary>
        public string VerdictText
        {
            get
            {
                if (!Complete) return NebulaText.Of("The benchmark did not finish.") + (Failure.Length > 0 ? " " + Failure : "");
                if (CpuBound || Verdict == "cpu_bound") return NebulaText.Of("The processor limits the frame rate here: lower graphics settings will not raise it.");
                if (Fits || Verdict == "fits") return NebulaText.Of("These settings keep 60 frames per second on this device.");
                return NebulaText.Of("Even the lowest settings may not keep 60 frames per second on this device.");
            }
        }
    }

    /// <summary>
    /// The benchmark run from the launcher (perf/bench's contract): the game started with --benchmark 1 and the result
    /// file's path, its steps read from its output, its result read when it exits.
    /// </summary>
    internal static class Benchmark
    {
        private static readonly Regex Step = new Regex(@"^\[benchmark\] (\d+)/(\d+) ", RegexOptions.Compiled);
        private static readonly Regex Steps = new Regex(@"^\[benchmark\] (\d+) steps$", RegexOptions.Compiled);

        /// <summary>A progress line: the step done (0 before the first) and how many there are.</summary>
        public static bool Progress(string line, out int step, out int steps)
        {
            step = steps = 0;
            if (line == null) return false;
            var m = Step.Match(line);
            if (m.Success) return int.TryParse(m.Groups[1].Value, out step) & int.TryParse(m.Groups[2].Value, out steps);
            m = Steps.Match(line);
            return m.Success && int.TryParse(m.Groups[1].Value, out steps);
        }

        /// <summary>The game started for a benchmark: its own arguments without the saves, its output read.</summary>
        public static ProcessStartInfo Create(string app, string content, string resultFile)
        {
            var start = new ProcessStartInfo(GamePrograms.Start(app),
                "--pak " + ProcessRunner.Quote(Path.Combine(content, "game.pak")) + " --benchmark 1 --benchmark-out " + ProcessRunner.Quote(resultFile));
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            start.WorkingDirectory = app;
            start.RedirectStandardOutput = start.RedirectStandardError = true;
            start.StandardOutputEncoding = start.StandardErrorEncoding = Encoding.UTF8;
            // As a game start (LaunchOptions.Create): no test variables reach it.
            foreach (var name in start.EnvironmentVariables.Keys.Cast<string>().Where(n => n.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)).ToList())
                start.EnvironmentVariables.Remove(name);
            return start;
        }

        /// <summary>The result file of a run that has ended with <paramref name="exitCode"/>: null when it did not finish.</summary>
        public static BenchmarkResult Read(int exitCode, string resultFile)
        {
            if (exitCode != 0 || !File.Exists(resultFile)) return null;
            try { return BenchmarkResult.Parse(File.ReadAllText(resultFile)); }
            catch (IOException) { return null; }
        }

        /// <summary>Where a run writes its result (the data root's, one file, replaced by the next run).</summary>
        public static string ResultPath { get { return Path.Combine(NebulaPaths.DataRoot, "benchmark-result.json"); } }

        // Beside the result: the launcher version whose run wrote it (the result's app.version is the game's
        // NEBULA_VERSION, the CMake project version, not the release's, so the launcher records its own).
        private static string RanByPath(string resultFile) { return resultFile + ".launcher"; }

        public static void RecordRanBy(string resultFile, string version)
        {
            try { File.WriteAllText(RanByPath(resultFile), version); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        public static string RanBy(string resultFile)
        {
            try { return File.Exists(RanByPath(resultFile)) ? File.ReadAllText(RanByPath(resultFile)).Trim() : null; }
            catch (IOException) { return null; }
            catch (UnauthorizedAccessException) { return null; }
        }

        /// <summary>The result's state now, against this data root's config.ini and adapter.ini.</summary>
        public static BenchmarkState State(BenchmarkResult result, string resultFile)
        {
            string adapter = Path.Combine(NebulaPaths.DataRoot, "adapter.ini");
            try
            {
                // adapter.ini as a normal start wrote it after the run (a benchmark does not write it): before one,
                // the run's adapter is the latest the game used.
                bool started = File.Exists(adapter) && File.GetLastWriteTimeUtc(adapter) > File.GetLastWriteTimeUtc(resultFile);
                return result.StateNow(GraphicsSettings.ReadValues(GraphicsSettings.ConfigPath),
                                       started ? GraphicsSettings.ReadValues(adapter) : null, RanBy(resultFile), BuildInfo.Version);
            }
            // A file that cannot be read now: nothing is applied from it.
            catch (IOException) { return BenchmarkState.Stale; }
            catch (UnauthorizedAccessException) { return BenchmarkState.Stale; }
        }
    }
}
