using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;

namespace Nebula.Setup
{
    internal enum InstallMode { Install, Repair, Update }

    internal sealed class InstallRequest
    {
        public InstallMode Mode;
        /// <summary>ISO/RVZ/other image or extracted folder; null to reuse retained game files.</summary>
        public string GameInput;
        public string InstallRoot;
        /// <summary>Local source zip/folder instead of the GitHub download (testing).</summary>
        public string SourceOverride;
        public bool DesktopShortcut;
        public int CompileJobs;
    }

    /// <summary>Progress reporting from the engine to the UI.</summary>
    internal interface IInstallProgress
    {
        /// <summary>Overall fraction 0..1, current step description, step fraction (negative = unknown).</summary>
        void Report(double overall, string step, double stepFraction);
        void Log(string line);
    }

    /// <summary>
    /// Clean install, repair and update. All work happens in a staging folder;
    /// the working installation is switched only after the complete new
    /// version is assembled and validated, and the previous one is kept.
    /// </summary>
    internal sealed class InstallEngine
    {
        /// <summary>Bump when nebula-recomp package-content output changes shape.</summary>
        public const int ContentFormatVersion = 1;

        /// <summary>Accepted module build recipe; part of the module compatibility key.</summary>
        private static readonly string[] ModuleRecipe =
        {
            "Ninja Multi-Config", "Release", "GALAXY_NATIVE_ISA=SSE2", "GALAXY_MODULE_USE_PCH=ON",
            "GALAXY_MODULE_ENABLE_LTCG=OFF"
        };

        /// <summary>Files under game-inputs that regeneration needs.</summary>
        private static readonly string[] GameInputFiles =
        {
            "sys/main.dol", "sys/boot.bin", "sys/bi2.bin", "sys/apploader.img", "sys/fst.bin",
            "files/ModuleData/HomeButtonMenuWrapperRSO.rso", "files/ModuleData/product.sel"
        };

        private readonly InstallRequest request;
        private readonly IInstallProgress progress;
        private readonly CancellationToken cancel;
        private readonly InstallLayout layout;
        private readonly SourceTree source;
        private readonly Toolchain toolchain;
        private readonly StreamWriter logFile;
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private string staging;
        private double stageBase, stageWeight;

        public InstallEngine(InstallRequest request, IInstallProgress progress, CancellationToken cancel)
        {
            this.request = request;
            this.progress = progress;
            this.cancel = cancel;
            layout = new InstallLayout(request.InstallRoot);
            source = new SourceTree(Json.Parse(Payload.ReadText("source-manifest.json")));
            toolchain = new Toolchain(Json.Parse(Payload.ReadText("toolchain.json")), layout.Toolchains);
            Directory.CreateDirectory(NebulaPaths.SetupLogs);
            string logPath = Path.Combine(NebulaPaths.SetupLogs,
                DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + "-" + Guid.NewGuid().ToString("N") + ".log");
            logFile = new StreamWriter(new FileStream(logPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read));
            logFile.AutoFlush = true;
            LogPath = logPath;
        }

        public string LogPath { get; private set; }
        public string InstalledVersionDir { get; private set; }
        public TimeSpan Elapsed { get { return clock.Elapsed; } }

        // ---- compatibility keys ------------------------------------------------

        private string GeneratorKey
        {
            get
            {
                return source.SubsetSha256("crates/", "metadata/", "third_party/dolphin-free-dsp-rom/", "Cargo.lock", "Cargo.toml");
            }
        }

        public string ModuleKey
        {
            get
            {
                return FileUtil.Sha256Text(string.Join("\n", new[] { GeneratorKey, source.SubsetSha256("runtime/include/"), toolchain.Id }.Concat(ModuleRecipe)));
            }
        }

        public string RuntimeKey
        {
            get
            {
                return FileUtil.Sha256Text(string.Join("\n", GeneratorKey,
                    source.SubsetSha256("runtime/", "CMakeLists.txt", "third_party/berkeley-softfloat-3/"), toolchain.Id));
            }
        }

        // ---- requirements --------------------------------------------------------

        public const long RequiredFreeBytes = 22L << 30;
        public const ulong MinimumMemory = 15UL << 30;

        public static List<string> CheckRequirements(string installRoot)
        {
            var problems = new List<string>();
            if (!Environment.Is64BitOperatingSystem) problems.Add("Nebula requires 64-bit Windows 10 or Windows 11.");
            if (Environment.OSVersion.Version.Major < 10) problems.Add("Nebula requires Windows 10 or Windows 11.");
            ulong memory = NativeMethods.TotalPhysicalMemory();
            if (memory < MinimumMemory)
                problems.Add(string.Format("Compiling Nebula needs at least 16 GB of RAM (this PC has {0}). One recompiled module takes about 13 GB to compile.",
                    FileUtil.FormatBytes((long)memory)));
            try
            {
                string fs = FileUtil.FileSystemName(installRoot);
                if (!string.Equals(fs, "NTFS", StringComparison.OrdinalIgnoreCase))
                    problems.Add("The install folder must be on an NTFS drive (this drive is " + fs + ").");
                long free = FileUtil.FreeBytes(installRoot);
                if (free < RequiredFreeBytes)
                    problems.Add(string.Format("The install drive needs {0} free during setup; it has {1}.",
                        FileUtil.FormatBytes(RequiredFreeBytes), FileUtil.FormatBytes(free)));
            }
            catch (Exception error) { problems.Add("Cannot inspect the install folder: " + error.Message); }
            return problems;
        }

        public static int DefaultCompileJobs()
        {
            // Up to about 2.4 GB per compiler process (measured) for 512 KiB shards,
            // keeping 4 GB for Windows and the rest of the desktop.
            double gigabytes = NativeMethods.TotalPhysicalMemory() / (double)(1UL << 30);
            int byMemory = (int)Math.Floor((gigabytes - 4) / 2.5);
            return Math.Max(1, Math.Min(Math.Min(Environment.ProcessorCount, byMemory), 32));
        }

        // ---- main entry ------------------------------------------------------------

        public void Run()
        {
            Log("Nebula Setup " + BuildInfo.Version + " (" + BuildInfo.Commit + "), mode " + request.Mode);
            Log("Install folder: " + layout.Root);
            Directory.CreateDirectory(layout.Root);
            Directory.CreateDirectory(layout.Staging);
            CleanAbandonedStaging();
            staging = Path.Combine(layout.Staging, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
            Directory.CreateDirectory(staging);
            try
            {
                var current = InstalledInfo.Read(layout);
                bool needInput = request.GameInput != null || !RetainedInputsValid() || !ContentValid();
                if (needInput && request.GameInput == null)
                    throw new InvalidOperationException("Choose your Super Mario Galaxy ISO, RVZ or extracted folder to continue.");

                string data = null;
                if (request.GameInput != null)
                {
                    Stage(0.00, 0.02, "Checking your game");
                    Identify(request.GameInput);
                    data = PrepareData(request.GameInput);
                    Stage(0.10, 0.02, "Saving the game files Nebula needs for future updates");
                    CopyGameInputs(data, Path.Combine(staging, "game-inputs"));
                    Stage(0.12, 0.08, "Building the game content package");
                    RunRecomp(new[] { "package-content", data, "--output", Path.Combine(staging, "content") }, null);
                }

                bool reuseModules = current != null && current.ModuleKey == ModuleKey && request.Mode == InstallMode.Update
                    && current.FilesIntact("RMGE01_game.dll", "RMGE01_home_button.dll", "RMGE01_boot_image.bin");
                bool reuseRuntime = reuseModules && current.RuntimeKey == RuntimeKey && current.FilesIntact("NebulaRuntime.exe");
                Log("Reuse compiled game modules: " + reuseModules + "; reuse runtime: " + reuseRuntime);

                string app = Path.Combine(staging, "app");
                Directory.CreateDirectory(app);
                if (!reuseRuntime || !reuseModules)
                {
                    Stage(0.20, 0.17, "Downloading and preparing the compiler toolchain");
                    if (!toolchain.IsReady)
                        Log("Microsoft C++ Build Tools license: " + toolchain.LicenseUrl);
                    toolchain.Ensure(StepProgress, Log, cancel);
                    Stage(0.37, 0.02, "Getting the Nebula source code");
                    string src = source.Ensure(layout.Sources, request.SourceOverride, StepProgress, Log, cancel);
                    Stage(0.39, 0.02, "Recompiling the game code");
                    string inputs = request.GameInput != null ? Path.Combine(staging, "game-inputs") : layout.GameInputs;
                    string gen = Path.Combine(staging, "generated");
                    RunRecomp(new[] { "generate", inputs, "--output", gen }, null);
                    BuildAll(src, gen, app, reuseRuntime ? current : null, reuseModules ? current : null);
                }
                else
                {
                    Stage(0.39, 0.55, "Reusing the compatible compiled game");
                    current.CopyFiles(app, "NebulaRuntime.exe", "RMGE01_game.dll", "RMGE01_home_button.dll", "RMGE01_boot_image.bin");
                    foreach (var dll in current.RuntimeDllNames()) current.CopyFiles(app, dll);
                }

                Stage(0.94, 0.02, "Finishing the installation");
                AssembleApp(app, current);
                ValidateApp(app);
                Stage(0.96, 0.04, "Switching to the new version");
                Commit(app, current);
                Report(1.0, "Nebula " + BuildInfo.Version + " is installed.", 1);
                Log(string.Format("Completed in {0:hh\\:mm\\:ss}.", clock.Elapsed));
            }
            catch (Exception error)
            {
                Log("FAILED: " + error);
                throw;
            }
            finally
            {
                try { FileUtil.DeleteTree(staging); } catch (Exception error) { Log("Could not remove staging: " + error.Message); }
                logFile.Dispose();
            }
        }

        // ---- steps ---------------------------------------------------------------

        private void Identify(string input)
        {
            string json = null;
            var lines = new List<string>();
            int code = RunTool(RecompPath, new[] { "identify", input, "--json" }, null, delegate(string line) { lines.Add(line); });
            json = string.Join("\n", lines);
            Log(json);
            Dictionary<string, object> identity;
            try { identity = Json.Parse(json.Substring(Math.Max(0, json.IndexOf('{')))); }
            catch (Exception) { throw new InvalidOperationException(lines.Count > 0 ? lines[lines.Count - 1] : "The selected file could not be read."); }
            if (code != 0 || !Json.Bool(identity, "supported"))
                throw new InvalidOperationException(Json.Str(identity, "message"));
        }

        private string PrepareData(string input)
        {
            if (Directory.Exists(input))
            {
                Log("Using the extracted folder " + input + " (read only).");
                string nested = Path.Combine(input, "DATA");
                return File.Exists(Path.Combine(nested, "sys", "main.dol")) ? nested : input;
            }
            Stage(0.02, 0.08, "Extracting your game image");
            string output = Path.Combine(staging, "extracted");
            RunRecomp(new[] { "extract", input, "--output", output }, delegate(string line)
            {
                var match = Regex.Match(line, @"^PROGRESS (\d+) (\d+)$");
                if (match.Success)
                {
                    StepProgress("Extracting your game image", double.Parse(match.Groups[1].Value) / Math.Max(1, double.Parse(match.Groups[2].Value)));
                    return true;
                }
                return false;
            });
            return Path.Combine(output, "DATA");
        }

        private static void CopyGameInputs(string data, string target)
        {
            foreach (var relative in GameInputFiles)
            {
                string from = Path.Combine(data, relative.Replace('/', '\\'));
                string to = Path.Combine(target, relative.Replace('/', '\\'));
                Directory.CreateDirectory(Path.GetDirectoryName(to));
                File.Copy(from, to);
            }
            var hashes = new Dictionary<string, object>();
            foreach (var relative in GameInputFiles) hashes[relative] = FileUtil.Sha256File(Path.Combine(target, relative.Replace('/', '\\')));
            Json.Save(Path.Combine(target, "inputs.json"), hashes);
        }

        private bool RetainedInputsValid()
        {
            string manifest = Path.Combine(layout.GameInputs, "inputs.json");
            if (!File.Exists(manifest)) return false;
            try
            {
                var hashes = Json.Load(manifest);
                foreach (var relative in GameInputFiles)
                {
                    string path = Path.Combine(layout.GameInputs, relative.Replace('/', '\\'));
                    if (!File.Exists(path) || FileUtil.Sha256File(path) != Json.Str(hashes, relative)) return false;
                }
                return true;
            }
            catch (Exception) { return false; }
        }

        private bool ContentValid()
        {
            string marker = Path.Combine(layout.Content, "content.json");
            if (!File.Exists(marker) || !File.Exists(Path.Combine(layout.Content, "game.pak"))) return false;
            try { return Json.Long(Json.Load(marker), "formatVersion") == ContentFormatVersion; }
            catch (Exception) { return false; }
        }

        private void BuildAll(string src, string gen, string app, InstalledInfo runtimeFrom, InstalledInfo modulesFrom)
        {
            var env = toolchain.BuildEnvironment();
            int jobs = request.CompileJobs > 0 ? request.CompileJobs : DefaultCompileJobs();
            Log("Compile jobs: " + jobs);
            string build = Path.Combine(staging, "build");
            string rt = Path.Combine(build, "runtime");
            Stage(0.41, 0.08, "Compiling the Nebula runtime");
            var configure = new List<string> { "-S", src, "-B", rt, "-G", "Ninja Multi-Config", "-DGALAXY_NATIVE_ISA=SSE2",
                "-DNEBULA_GENERATED_DSP_SOURCE=" + Path.Combine(gen, "dsp", "rmge01_dsp.cpp").Replace('\\', '/') };
            configure.AddRange(toolchain.CMakeToolArguments());
            CMake(configure, env);
            CMake(new List<string> { "--build", rt, "--config", "Release", "--parallel", jobs.ToString(),
                "--target", "NebulaRuntime", "galaxy_ppc_float", "galaxy_softfloat" }, env);
            File.Copy(Path.Combine(rt, "Release", "NebulaRuntime.exe"), Path.Combine(app, "NebulaRuntime.exe"));

            var common = new List<string> {
                "-DGALAXY_RUNTIME_INCLUDE=" + Path.Combine(src, "runtime", "include").Replace('\\', '/'),
                "-DGALAXY_PPC_FLOAT_LIBRARY=" + Path.Combine(rt, "Release", "galaxy_ppc_float.lib").Replace('\\', '/'),
                "-DGALAXY_SOFTFLOAT_LIBRARY=" + Path.Combine(rt, "Release", "galaxy_softfloat.lib").Replace('\\', '/'),
                "-DGALAXY_NATIVE_ISA=SSE2" };
            common.AddRange(toolchain.CMakeToolArguments());

            if (modulesFrom != null)
            {
                Stage(0.49, 0.45, "Reusing the compatible compiled game modules");
                modulesFrom.CopyFiles(app, "RMGE01_game.dll", "RMGE01_home_button.dll", "RMGE01_boot_image.bin");
                return;
            }
            Stage(0.49, 0.33, "Compiling the recompiled game (the longest step)");
            var game = new List<string> { "-S", Path.Combine(gen, "game"), "-B", Path.Combine(build, "game"), "-G", "Ninja Multi-Config",
                "-DGALAXY_MODULE_COMPILE_JOBS=" + jobs };
            game.AddRange(common);
            CMake(game, env);
            CMake(new List<string> { "--build", Path.Combine(build, "game"), "--config", "Release", "--parallel", jobs.ToString() }, env);
            Stage(0.82, 0.12, "Compiling the Home Menu module");
            var home = new List<string> { "-S", Path.Combine(gen, "home"), "-B", Path.Combine(build, "home"), "-G", "Ninja Multi-Config" };
            home.AddRange(common);
            CMake(home, env);
            CMake(new List<string> { "--build", Path.Combine(build, "home"), "--config", "Release", "--parallel", "1" }, env);
            File.Copy(Path.Combine(build, "game", "Release", "RMGE01_game.dll"), Path.Combine(app, "RMGE01_game.dll"));
            File.Copy(Path.Combine(build, "home", "Release", "RMGE01_home_button.dll"), Path.Combine(app, "RMGE01_home_button.dll"));
            File.Copy(Path.Combine(gen, "RMGE01_boot_image.bin"), Path.Combine(app, "RMGE01_boot_image.bin"));
        }

        private void AssembleApp(string app, InstalledInfo current)
        {
            foreach (var dll in toolchain.IsReady ? toolchain.RuntimeDlls() : Enumerable.Empty<string>())
            {
                string target = Path.Combine(app, Path.GetFileName(dll));
                if (!File.Exists(target)) File.Copy(dll, target);
            }
            if (!File.Exists(Path.Combine(app, "vcruntime140.dll")) && current != null)
                foreach (var dll in current.RuntimeDllNames()) current.CopyFiles(app, dll);
            foreach (var name in new[] { "Nebula.exe", "runtime-env.json", "LICENSE", "THIRD-PARTY-NOTICES.md", "README.md", "nebula-recomp.exe" })
                Payload.Extract(name, Path.Combine(app, name));

            var files = new Dictionary<string, object>();
            foreach (var path in Directory.GetFiles(app)) files[Path.GetFileName(path)] = FileUtil.Sha256File(path);
            Json.Save(Path.Combine(app, "install.json"), new Dictionary<string, object>
            {
                { "schema", "nebula.install.v1" },
                { "version", BuildInfo.Version },
                { "commit", source.Commit },
                { "repository", source.Repository },
                { "moduleKey", ModuleKey },
                { "runtimeKey", RuntimeKey },
                { "toolchain", toolchain.Id },
                { "contentFormatVersion", ContentFormatVersion },
                { "installedUtc", DateTime.UtcNow.ToString("o") },
                { "files", files }
            });
        }

        /// <summary>Load the built module and check its manifest exports before switching.</summary>
        private void ValidateApp(string app)
        {
            foreach (var name in new[] { "NebulaRuntime.exe", "RMGE01_game.dll", "RMGE01_home_button.dll", "RMGE01_boot_image.bin", "Nebula.exe" })
                if (!File.Exists(Path.Combine(app, name))) throw new InvalidOperationException("The new version is missing " + name + ".");
            SetDllDirectory(app);
            IntPtr module = LoadLibraryEx(Path.Combine(app, "RMGE01_game.dll"), IntPtr.Zero, 0x00000008);
            if (module == IntPtr.Zero) throw new InvalidOperationException("The compiled game module could not be loaded (error " + Marshal.GetLastWin32Error() + ").");
            try
            {
                foreach (var export in new[] { "galaxy_module_manifest", "galaxy_module_init", "galaxy_module_entry", "galaxy_lookup_function" })
                    if (GetProcAddress(module, export) == IntPtr.Zero) throw new InvalidOperationException("The compiled game module has no " + export + " export.");
            }
            finally { FreeLibrary(module); SetDllDirectory(null); }
            Log("Validated the compiled game module exports.");
        }

        private void Commit(string app, InstalledInfo current)
        {
            Running.RequireClosed(layout.Root);
            string versionDir = layout.VersionDir(BuildInfo.Version);
            Directory.CreateDirectory(layout.Versions);
            string replaced = null;
            if (Directory.Exists(versionDir))
            {
                replaced = versionDir + ".replaced-" + Guid.NewGuid().ToString("N").Substring(0, 8);
                Directory.Move(versionDir, replaced);
            }
            Directory.Move(app, versionDir);

            string newContent = Path.Combine(staging, "content");
            if (Directory.Exists(newContent))
            {
                Json.Save(Path.Combine(newContent, "content.json"), new Dictionary<string, object> { { "formatVersion", ContentFormatVersion }, { "createdUtc", DateTime.UtcNow.ToString("o") } });
                string oldContent = Path.Combine(staging, "content-old");
                if (Directory.Exists(layout.Content)) Directory.Move(layout.Content, oldContent);
                Directory.Move(newContent, layout.Content);
            }
            string newInputs = Path.Combine(staging, "game-inputs");
            if (Directory.Exists(newInputs))
            {
                if (Directory.Exists(layout.GameInputs)) Directory.Move(layout.GameInputs, Path.Combine(staging, "game-inputs-old"));
                Directory.Move(newInputs, layout.GameInputs);
            }

            string previous = current != null && current.Version != BuildInfo.Version ? current.Version : (current != null ? current.Previous : null);
            Json.Save(layout.Current, new Dictionary<string, object>
            {
                { "version", BuildInfo.Version },
                { "previous", previous }
            });
            if (replaced != null) FileUtil.DeleteTree(replaced);
            // Keep exactly one previous version for rollback.
            foreach (var dir in Directory.GetDirectories(layout.Versions))
            {
                string name = Path.GetFileName(dir);
                if (name != BuildInfo.Version && name != previous) { try { FileUtil.DeleteTree(dir); } catch (IOException) { } }
            }
            string self = Assembly.GetExecutingAssembly().Location;
            if (!string.Equals(Path.GetFullPath(self), layout.SetupCopy, StringComparison.OrdinalIgnoreCase))
                File.Copy(self, layout.SetupCopy, true);
            Integration.Register(layout, BuildInfo.Version, request.DesktopShortcut);
            InstalledVersionDir = versionDir;
            Log("Installed " + BuildInfo.Version + " to " + versionDir + (previous != null ? "; previous version " + previous + " kept for rollback." : "."));
        }

        private void CleanAbandonedStaging()
        {
            foreach (var dir in Directory.GetDirectories(layout.Staging))
            {
                try { FileUtil.DeleteTree(dir); Log("Removed incomplete work from an earlier run: " + dir); }
                catch (Exception) { }
            }
        }

        // ---- process helpers -----------------------------------------------------

        private string RecompPath
        {
            get
            {
                string path = Path.Combine(layout.Staging, "nebula-recomp-" + BuildInfo.Version + ".exe");
                if (!File.Exists(path)) Payload.Extract("nebula-recomp.exe", path);
                return path;
            }
        }

        private void RunRecomp(IList<string> arguments, Func<string, bool> filter)
        {
            int code = RunTool(RecompPath, arguments, null, delegate(string line)
            {
                if (filter != null && filter(line)) return;
                if (line.StartsWith("STEP ")) StepProgress(line.Substring(line.IndexOf(' ', 5) + 1), -1);
                Log(line);
            });
            if (code != 0) throw new InvalidOperationException("nebula-recomp " + arguments[0] + " failed (exit " + code + "). See the setup log for details.");
        }

        private static readonly Regex NinjaProgress = new Regex(@"^\[(\d+)/(\d+)\]");

        private void CMake(IList<string> arguments, Dictionary<string, string> env)
        {
            string last = null;
            int code = RunTool(toolchain.CMake, arguments, env, delegate(string line)
            {
                var match = NinjaProgress.Match(line);
                if (match.Success)
                {
                    double done = double.Parse(match.Groups[1].Value), total = double.Parse(match.Groups[2].Value);
                    StepProgress(null, done / Math.Max(1, total));
                    logFile.WriteLine(line);
                    return;
                }
                if (line.Contains("error") || line.Contains("FAILED")) last = line;
                Log(line);
            });
            if (code != 0) throw new InvalidOperationException("Compilation failed" + (last != null ? ": " + last : ".") + " See the setup log for details.");
        }

        private int RunTool(string file, IList<string> arguments, Dictionary<string, string> env, Action<string> onLine)
        {
            Log("> " + Path.GetFileName(file) + " " + ProcessRunner.JoinArguments(arguments));
            using (var runner = new ProcessRunner())
                return runner.Run(file, arguments, staging, env, onLine, cancel);
        }

        // ---- progress ------------------------------------------------------------

        private string stepText = "";

        private void Stage(double start, double weight, string text)
        {
            cancel.ThrowIfCancellationRequested();
            stageBase = start;
            stageWeight = weight;
            stepText = text;
            Log("== " + text);
            Report(start, text, -1);
        }

        private void StepProgress(string text, double fraction)
        {
            if (text != null) stepText = text;
            Report(stageBase + stageWeight * Math.Max(0, Math.Min(1, fraction)), stepText, fraction);
        }

        private void Report(double overall, string text, double fraction) { progress.Report(overall, text, fraction); }

        private void Log(string line)
        {
            logFile.WriteLine(DateTime.UtcNow.ToString("HH:mm:ss.fff") + " " + line);
            progress.Log(line);
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string path, IntPtr reserved, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll")]
        private static extern bool FreeLibrary(IntPtr module);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern bool SetDllDirectory(string path);
    }
}
