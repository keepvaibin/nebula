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

        /// <summary>Module build recipe; part of the module compatibility key.</summary>
        private static readonly string[] ModuleRecipe =
        {
            "Ninja", "Release", "clang-cl", "GALAXY_NATIVE_ISA=" + NativeIsa,
            "GALAXY_MODULE_USE_PCH=ON", "runtime-isa=" + RuntimeNativeIsa
        };

        /// <summary>
        /// The native ISA the embedded NebulaRuntime.exe and the embedded
        /// galaxy_ppc_float / galaxy_softfloat / galaxy_dsp_alu libraries were
        /// compiled for. build-release records CMake's own effective-target line,
        /// so this is the value that reached the compiler and not merely the one
        /// that was requested. Packages produced before that record existed were
        /// always SSE2, which is the safe default here.
        /// </summary>
        public static string RuntimeNativeIsa
        {
            get
            {
                if (runtimeNativeIsa == null)
                {
                    string value = "SSE2";
                    try { value = Payload.ReadText("runtime-isa.txt").Trim().ToUpperInvariant(); }
                    catch (Exception) { /* Pre-record packages are SSE2. */ }
                    runtimeNativeIsa = value == "AVX2" ? "AVX2" : "SSE2";
                }
                return runtimeNativeIsa;
            }
        }

        private static string runtimeNativeIsa;

        /// <summary>
        /// PF_AVX2_INSTRUCTIONS_AVAILABLE. The module build already gates AVX2
        /// codegen on this bit; the prebuilt runtime needs the same answer.
        /// </summary>
        public static bool HostSupportsAvx2()
        {
            if (!hostSupportsAvx2.HasValue)
            {
                bool supported = false;
                try { supported = IsProcessorFeaturePresent(40); }
                catch (Exception) { supported = false; }
                hostSupportsAvx2 = supported;
            }
            return hostSupportsAvx2.Value;
        }

        private static bool? hostSupportsAvx2;

        /// <summary>
        /// The translated modules are compiled on the machine that runs them, so AVX2 code
        /// generation is safe whenever this CPU and OS expose it (PF_AVX2_INSTRUCTIONS_AVAILABLE).
        /// NEBULA_MODULE_ISA=SSE2 forces the portable baseline.
        /// </summary>
        private static string NativeIsa
        {
            get
            {
                string forced = Environment.GetEnvironmentVariable("NEBULA_MODULE_ISA");
                if (string.Equals(forced, "SSE2", StringComparison.OrdinalIgnoreCase)) return "SSE2";
                try { return HostSupportsAvx2() ? "AVX2" : "SSE2"; }
                catch (Exception) { return "SSE2"; }
            }
        }

        /// <summary>Files Setup compiles from the user's game.</summary>
        private static readonly string[] ModuleFiles = { "RMGE01_game.dll", "RMGE01_home_button.dll", "RMGE01_dsp.dll", "RMGE01_boot_image.bin" };

        /// <summary>Prebuilt runtime libraries the modules link against (Setup payload).</summary>
        private static readonly string[] RuntimeLibraries = { "galaxy_ppc_float.lib", "galaxy_softfloat.lib", "galaxy_dsp_alu.lib" };

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
        private readonly object logLock = new object();
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

        /// <summary>Everything compiled modules depend on: generator, runtime, toolchain and recipe.</summary>
        public string ModuleKey
        {
            get
            {
                string inputs = source.SubsetSha256("crates/", "metadata/", "third_party/", "runtime/", "Cargo.lock", "Cargo.toml", "CMakeLists.txt");
                return FileUtil.Sha256Text(string.Join("\n", new[] { inputs, toolchain.Id }.Concat(ModuleRecipe)));
            }
        }

        // ---- requirements --------------------------------------------------------

        public const long RequiredFreeBytes = 16L << 30;
        public const ulong MinimumMemory = 7UL << 30;

        public static List<string> CheckRequirements(string installRoot)
        {
            var problems = new List<string>();
            if (!Environment.Is64BitOperatingSystem) problems.Add("Nebula needs 64-bit Windows 10 or 11.");
            if (Environment.OSVersion.Version.Major < 10) problems.Add("Nebula needs Windows 10 or 11.");
            // The prebuilt runtime and the prebuilt float/DSP libraries that every
            // module links against are compiled for exactly one ISA. A package
            // built for AVX2 must not be installed on a host that cannot execute
            // it: the symptom would be an illegal-instruction fault inside the
            // game at an arbitrary later moment, not a diagnosable setup error.
            // Refusing here is the only place that failure can be attributed.
            if (RuntimeNativeIsa == "AVX2" && !HostSupportsAvx2())
                problems.Add("This package's runtime was built for AVX2, which this CPU does not expose. Use a package built with -NativeIsa SSE2, or install on a CPU with AVX2.");
            ulong memory = NativeMethods.TotalPhysicalMemory();
            if (memory < MinimumMemory)
                problems.Add("Nebula needs 8 GB of RAM (this PC has " + FileUtil.FormatBytes((long)memory) + ").");
            try
            {
                string fs = FileUtil.FileSystemName(installRoot);
                if (!string.Equals(fs, "NTFS", StringComparison.OrdinalIgnoreCase))
                    problems.Add("The install folder must be on an NTFS drive (this one is " + fs + ").");
                long free = FileUtil.FreeBytes(installRoot);
                if (free < RequiredFreeBytes)
                    problems.Add("Setup needs " + FileUtil.FormatBytes(RequiredFreeBytes) + " free; the drive has " + FileUtil.FormatBytes(free) + ".");
            }
            catch (Exception error) { problems.Add("Cannot inspect the install folder: " + error.Message); }
            return problems;
        }

        /// <summary>One compiler per thread, limited to about 1 GB of RAM each beyond 3 GB for Windows.</summary>
        public static int DefaultCompileJobs()
        {
            double gigabytes = NativeMethods.TotalPhysicalMemory() / (double)(1UL << 30);
            int byMemory = (int)Math.Floor(gigabytes - 3);
            return Math.Max(1, Math.Min(Math.Min(Environment.ProcessorCount, byMemory), 64));
        }

        // ---- main entry ------------------------------------------------------------

        public void Run()
        {
            Log("Nebula Setup " + BuildInfo.Version + " (" + BuildInfo.Commit + "), " + request.Mode);
            Log("Install folder: " + layout.Root);
            Directory.CreateDirectory(layout.Root);
            Directory.CreateDirectory(layout.Staging);
            CleanAbandonedStaging();
            staging = Path.Combine(layout.Staging, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
            Directory.CreateDirectory(staging);
            Thread prepare = null;
            Exception prepareFailure = null;
            var prepareCancel = CancellationTokenSource.CreateLinkedTokenSource(cancel);
            string src = null;
            try
            {
                var current = InstalledInfo.Read(layout);
                bool needInput = request.GameInput != null || !RetainedInputsValid() || !ContentValid();
                if (needInput && request.GameInput == null)
                    throw new InvalidOperationException("Choose your Super Mario Galaxy ISO, RVZ or extracted folder.");

                bool reuseModules = current != null && current.ModuleKey == ModuleKey && request.Mode == InstallMode.Update
                    && current.FilesIntact(ModuleFiles);
                Log("Reuse compiled modules: " + reuseModules);

                // Downloads run while the game is extracted and recompiled.
                if (!reuseModules)
                {
                    if (!toolchain.IsReady) Log("Microsoft C++ Build Tools license: " + toolchain.LicenseUrl);
                    prepare = new Thread(delegate()
                    {
                        try
                        {
                            toolchain.Ensure(BackgroundProgress, Log, prepareCancel.Token);
                            src = source.Ensure(layout.Sources, request.SourceOverride, BackgroundProgress, Log, prepareCancel.Token);
                        }
                        catch (Exception error) { prepareFailure = error; }
                    });
                    prepare.IsBackground = true;
                    prepare.Start();
                }

                string data = null;
                if (request.GameInput != null)
                {
                    Stage(0.00, 0.02, "Checking your game");
                    Identify(request.GameInput);
                    data = PrepareData(request.GameInput);
                    CopyGameInputs(data, Path.Combine(staging, "game-inputs"));
                    Stage(0.10, 0.08, "Packing the game files");
                    RunRecomp(new[] { "package-content", data, "--output", Path.Combine(staging, "content") }, null);
                }

                string app = Path.Combine(staging, "app");
                Directory.CreateDirectory(app);
                if (reuseModules)
                {
                    Stage(0.20, 0.70, "Reusing the compiled game");
                    current.CopyFiles(app, ModuleFiles);
                    foreach (var dll in current.RuntimeDllNames()) current.CopyFiles(app, dll);
                }
                else
                {
                    Stage(0.18, 0.04, "Recompiling the game code");
                    string inputs = request.GameInput != null ? Path.Combine(staging, "game-inputs") : layout.GameInputs;
                    string gen = Path.Combine(staging, "generated");
                    RunRecomp(new[] { "generate", inputs, "--output", gen }, null);

                    Stage(0.22, 0.08, "Downloading the compiler");
                    waitingForDownloads = true;
                    prepare.Join();
                    waitingForDownloads = false;
                    if (prepareFailure != null) throw prepareFailure;
                    Build(src, gen, app);
                }

                Stage(0.94, 0.02, "Finishing");
                AssembleApp(app, current);
                SeedShaderCache();
                ValidateApp(app);
                Stage(0.96, 0.04, "Switching to the new version");
                Commit(app, current);
                Report(1.0, "Nebula " + BuildInfo.Version + " is installed.", 1);
                Log(string.Format("Completed in {0:mm\\:ss}.", clock.Elapsed));
            }
            catch (Exception error)
            {
                prepareCancel.Cancel();
                Log("FAILED: " + error);
                throw;
            }
            finally
            {
                if (prepare != null) prepare.Join();
                prepareCancel.Dispose();
                try { FileUtil.DeleteTree(staging); } catch (Exception error) { Log("Could not remove staging: " + error.Message); }
                logFile.Dispose();
            }
        }

        // ---- steps ---------------------------------------------------------------

        private void Identify(string input)
        {
            var lines = new List<string>();
            int code = RunTool(RecompPath, new[] { "identify", input, "--json" }, null, delegate(string line) { lines.Add(line); });
            string json = string.Join("\n", lines);
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
                string nested = Path.Combine(input, "DATA");
                return File.Exists(Path.Combine(nested, "sys", "main.dol")) ? nested : input;
            }
            Stage(0.02, 0.08, "Extracting your game");
            string output = Path.Combine(staging, "extracted");
            RunRecomp(new[] { "extract", input, "--output", output }, delegate(string line)
            {
                var match = Regex.Match(line, @"^PROGRESS (\d+) (\d+)$");
                if (!match.Success) return false;
                StepProgress(null, double.Parse(match.Groups[1].Value) / Math.Max(1, double.Parse(match.Groups[2].Value)));
                return true;
            });
            return Path.Combine(output, "DATA");
        }

        private static void CopyGameInputs(string data, string target)
        {
            var hashes = new Dictionary<string, object>();
            foreach (var relative in GameInputFiles)
            {
                string to = Path.Combine(target, relative.Replace('/', '\\'));
                Directory.CreateDirectory(Path.GetDirectoryName(to));
                File.Copy(Path.Combine(data, relative.Replace('/', '\\')), to);
                hashes[relative] = FileUtil.Sha256File(to);
            }
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

        /// <summary>One clang-cl build of the game, Home Menu and DSP modules.</summary>
        private void Build(string src, string gen, string app)
        {
            var env = toolchain.BuildEnvironment();
            int jobs = request.CompileJobs > 0 ? request.CompileJobs : DefaultCompileJobs();
            string libs = Path.Combine(staging, "runtime-libs");
            foreach (var lib in RuntimeLibraries) Payload.Extract(lib, Path.Combine(libs, lib));
            Func<string, string> cmakePath = delegate(string path) { return path.Replace('\\', '/'); };

            Stage(0.30, 0.64, "Compiling the game (" + jobs + " at a time)");
            string build = Path.Combine(staging, "build");
            var configure = new List<string> { "-S", gen, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                "-DGALAXY_RUNTIME_INCLUDE=" + cmakePath(Path.Combine(src, "runtime", "include")),
                "-DGALAXY_PPC_FLOAT_LIBRARY=" + cmakePath(Path.Combine(libs, "galaxy_ppc_float.lib")),
                "-DGALAXY_SOFTFLOAT_LIBRARY=" + cmakePath(Path.Combine(libs, "galaxy_softfloat.lib")),
                "-DGALAXY_DSP_ALU_LIBRARY=" + cmakePath(Path.Combine(libs, "galaxy_dsp_alu.lib")),
                "-DGALAXY_NATIVE_ISA=" + NativeIsa, "-DGALAXY_MODULE_COMPILE_JOBS=" + jobs };
            configure.AddRange(toolchain.CMakeToolArguments());
            CMake(configure, env);
            CMake(new List<string> { "--build", build, "--parallel", jobs.ToString() }, env);

            File.Copy(Path.Combine(build, "game", "RMGE01_game.dll"), Path.Combine(app, "RMGE01_game.dll"));
            File.Copy(Path.Combine(build, "home", "RMGE01_home_button.dll"), Path.Combine(app, "RMGE01_home_button.dll"));
            File.Copy(Path.Combine(build, "dsp", "RMGE01_dsp.dll"), Path.Combine(app, "RMGE01_dsp.dll"));
            File.Copy(Path.Combine(gen, "RMGE01_boot_image.bin"), Path.Combine(app, "RMGE01_boot_image.bin"));
        }

        /// <summary>
        /// Seeds the DXBC/PSO caches so a user's first launch is warmed instead of
        /// paying the full cold-start compile.
        ///
        /// Only these two files are seeded, and the runtime's own loaders are why.
        /// `load_disk_cache` validates magic + version and then checksums and
        /// reflects every record, with NO adapter check anywhere — so `shaders.bin`
        /// is portable across GPUs. `load_pso_key_cache` is likewise adapter-free.
        /// `pipelines.bin` is deliberately NOT seeded: `load_pipeline_library`
        /// compares the adapter LUID and discards the file on any mismatch, so a
        /// foreign copy would only ever be rejected.
        ///
        /// Every failure mode here degrades to today's behaviour rather than to a
        /// wedge: a version mismatch sets the runtime's repair flag and the file is
        /// ignored, so the user pays the same cold start they pay without the seed.
        /// The `File.Exists` guard means a cache this machine already built is
        /// never replaced.
        /// </summary>
        private void SeedShaderCache()
        {
            // NebulaPaths.ShaderCache is the shadercache root; the runtime keys its
            // per-game caches by title id, matching the seed directory name.
            string cacheRoot = Path.Combine(NebulaPaths.ShaderCache, "RMGE01");
            foreach (var name in new[] { "shaders.bin", "psos.bin" })
            {
                try
                {
                    string destination = Path.Combine(cacheRoot, name);
                    if (File.Exists(destination))
                    {
                        Log("Shader-cache seed: kept the existing " + name + ".");
                        continue;
                    }
                    // Absent from the payload only if the release deliberately
                    // omitted it (see build-release.ps1's version guard for
                    // psos.bin). That is not an error.
                    Payload.Extract("shadercache-seed." + name, destination);
                    Log("Shader-cache seed: wrote " + name + ".");
                }
                catch (FileNotFoundException)
                {
                    Log("Shader-cache seed: " + name + " is not in this package.");
                }
            }
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
            foreach (var name in new[] { "NebulaRuntime.exe", "Nebula.exe", "runtime-env.json", "LICENSE", "THIRD-PARTY-NOTICES.md", "README.md", "nebula-recomp.exe", "runtime-isa.txt" })
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
                { "toolchain", toolchain.Id },
                { "contentFormatVersion", ContentFormatVersion },
                // The CPU target of the prebuilt runtime and of the prebuilt
                // float/DSP libraries, and (separately) of the modules compiled
                // on this host. Two installs that share a version label but
                // differ here do not run the same machine code, so a recorded
                // session must be able to prove which pair it measured instead
                // of inferring it from file hashes alone.
                { "runtimeIsa", RuntimeNativeIsa },
                { "moduleIsa", NativeIsa },
                { "installedUtc", DateTime.UtcNow.ToString("o") },
                { "files", files }
            });
        }

        /// <summary>Load each compiled module and check its exports before switching.</summary>
        private void ValidateApp(string app)
        {
            foreach (var name in ModuleFiles.Concat(new[] { "NebulaRuntime.exe", "Nebula.exe" }))
                if (!File.Exists(Path.Combine(app, name))) throw new InvalidOperationException("The new version is missing " + name + ".");
            var exports = new Dictionary<string, string[]>
            {
                { "RMGE01_game.dll", new[] { "galaxy_module_manifest", "galaxy_module_init", "galaxy_module_entry", "galaxy_lookup_function" } },
                { "RMGE01_home_button.dll", new[] { "galaxy_home_button_rso_manifest", "galaxy_home_button_rso_try_call", "galaxy_home_button_rso_resume" } },
                { "RMGE01_dsp.dll", new[] { "galaxy_rmge01_dsp_entry", "galaxy_rmge01_dsp_entry_expected_iram_sha1" } }
            };
            SetDllDirectory(app);
            try
            {
                foreach (var pair in exports)
                {
                    IntPtr module = LoadLibraryEx(Path.Combine(app, pair.Key), IntPtr.Zero, 0x00000008);
                    if (module == IntPtr.Zero) throw new InvalidOperationException(pair.Key + " could not be loaded (error " + Marshal.GetLastWin32Error() + ").");
                    try
                    {
                        foreach (var export in pair.Value)
                            if (GetProcAddress(module, export) == IntPtr.Zero) throw new InvalidOperationException(pair.Key + " has no " + export + " export.");
                    }
                    finally { FreeLibrary(module); }
                }
            }
            finally { SetDllDirectory(null); }
            Log("Validated the compiled modules.");
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
                if (Directory.Exists(layout.Content)) Directory.Move(layout.Content, Path.Combine(staging, "content-old"));
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
            Log("Installed " + BuildInfo.Version + " to " + versionDir + (previous != null ? "; kept " + previous + " for rollback." : "."));
        }

        private void CleanAbandonedStaging()
        {
            foreach (var dir in Directory.GetDirectories(layout.Staging))
            {
                try { FileUtil.DeleteTree(dir); Log("Removed unfinished work: " + dir); }
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
            if (code != 0) throw new InvalidOperationException("nebula-recomp " + arguments[0] + " failed (exit " + code + "). See the setup log.");
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
                    WriteLog(line);
                    return;
                }
                if (line.Contains("error") || line.Contains("FAILED")) last = line;
                Log(line);
            });
            if (code != 0) throw new InvalidOperationException("Compiling failed" + (last != null ? ": " + last : ".") + " See the setup log.");
        }

        private int RunTool(string file, IList<string> arguments, Dictionary<string, string> env, Action<string> onLine)
        {
            Log("> " + Path.GetFileName(file) + " " + ProcessRunner.JoinArguments(arguments));
            using (var runner = new ProcessRunner())
                return runner.Run(file, arguments, staging, env, onLine, cancel);
        }

        // ---- progress ------------------------------------------------------------

        private string stepText = "";
        private volatile bool waitingForDownloads;

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

        /// <summary>Background downloads show progress only once Setup is waiting for them.</summary>
        private void BackgroundProgress(string text, double fraction)
        {
            if (waitingForDownloads) StepProgress(text, fraction);
        }

        private void Report(double overall, string text, double fraction) { progress.Report(overall, text, fraction); }

        private void WriteLog(string line)
        {
            lock (logLock) logFile.WriteLine(DateTime.UtcNow.ToString("HH:mm:ss.fff") + " " + line);
        }

        private void Log(string line)
        {
            WriteLog(line);
            progress.Log(line);
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string path, IntPtr reserved, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        [DllImport("kernel32.dll")]
        private static extern bool FreeLibrary(IntPtr module);

        [DllImport("kernel32.dll")]
        private static extern bool IsProcessorFeaturePresent(uint feature);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern bool SetDllDirectory(string path);
    }
}
