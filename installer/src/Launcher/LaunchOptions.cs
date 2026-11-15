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
    /// The game's programs beside the launcher, one per CPU level: Nebula.exe (AVX2; on a CPU without it its
    /// entry starts the x86-64-v2 program and waits) and Nebula-x86-64-v2.exe (SSE4.2), and the Direct3D 11
    /// line's pair once it merges. The launcher starts the one the CPU runs, so the process it watches is the game,
    /// and finds a running game by any of them.
    /// </summary>
    internal static class GamePrograms
    {
        internal static readonly string[] Names = { "Nebula", "Nebula-x86-64-v2", "NebulaD11", "NebulaD11-x86-64-v2" };

        // PF_AVX2_INSTRUCTIONS_AVAILABLE: the CPU and the OS support AVX2 (tests replace it).
        internal static Func<bool> Avx2 = () =>
        {
            try { return NativeMethods.IsProcessorFeaturePresent(40); }
            catch (Exception) { return false; }
        };

        /// <summary>The program a game start runs: Nebula.exe, or on a CPU without AVX2 the x86-64-v2 program
        /// when the package has it (else Nebula.exe, whose own check then decides).</summary>
        public static string Start(string app)
        {
            string v2 = Path.Combine(app, "Nebula-x86-64-v2.exe");
            return !Avx2() && File.Exists(v2) ? v2 : Path.Combine(app, "Nebula.exe");
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
    }
}
