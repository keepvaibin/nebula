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

    /// <summary>
    /// The benchmark's result file (perf/bench, schema 1): how the run ended, the verdict, and the recommended
    /// settings when it completed.
    /// </summary>
    internal sealed class BenchmarkResult
    {
        public string Status = "", Failure = "", Verdict = "none";
        public bool Fits, CpuBound;
        public List<BenchmarkSetting> Settings = new List<BenchmarkSetting>();

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

        /// <summary>The rows the player is shown and Apply writes: those whose recommended value differs.</summary>
        public List<BenchmarkSetting> Changes { get { return Settings.Where(s => s.Current != s.Recommended).ToList(); } }

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
            var start = new ProcessStartInfo(Path.Combine(app, "NebulaNative.exe"),
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
    }
}
