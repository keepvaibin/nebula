using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;

namespace Nebula.Setup
{
    internal enum InstallMode { Install, Repair, Update }

    internal static class InstallIntent
    {
        // A downloaded release is its own updater. The setup does not need to rediscover the same release,
        // or force a repair rebuild, when the existing hash-verified game modules remain compatible.
        internal static InstallMode ForPackage(string installedVersion, string packageVersion)
        {
            if (installedVersion == null) return InstallMode.Install;
            return Updater.CompareVersions(packageVersion, installedVersion) > 0
                ? InstallMode.Update : InstallMode.Repair;
        }
    }

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
        /// <summary>Shows <paramref name="message"/> and waits; true to check again, false to give up.</summary>
        bool AskToClose(string message);
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

        /// <summary>
        /// Raised when generated module code changes in a way a relink cannot deliver (a relink keeps the
        /// compiled objects); an update recompiles installed modules of a lower generation. 2 (1.0.1): the
        /// site hooks of the text speed (TextStep) and language (Language) settings; fused multiply-adds
        /// without the C runtime (a 1.0.0 SSE2 module calls its soft-float fma for every fused guest
        /// instruction); no LodDistance hook.
        /// </summary>
        public const int ModuleGeneration = 2;

        /// <summary>Installed modules an update must recompile rather than relink.</summary>
        private static bool ModulesNeedRebuild(InstalledInfo current)
        {
            return current.ModuleGeneration < ModuleGeneration;
        }

        /// <summary>Module build recipe; part of the module compatibility key.</summary>
        private static readonly string[] ModuleRecipe =
        {
            "Ninja", "Release", "clang-cl", "GALAXY_NATIVE_ISA=" + NativeIsa,
            "GALAXY_MODULE_USE_PCH=ON", "runtime-isa=" + RuntimeNativeIsa
        };

        /// <summary>
        /// The native ISA of NebulaRuntime.exe and of the galaxy_ppc_float, galaxy_softfloat and
        /// galaxy_dsp_alu libraries this install uses. build-release records CMake's effective-target
        /// line, so the value is what reached the compiler. A "SSE2+AVX2" package carries both builds
        /// (AVX2 under "avx2/") and installs the AVX2 one when this CPU exposes AVX2. Packages made
        /// before the record existed are SSE2.
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
                    dualRuntime = value == "SSE2+AVX2";
                    runtimeNativeIsa = dualRuntime ? (HostSupportsAvx2() ? "AVX2" : "SSE2")
                        : value == "AVX2" ? "AVX2" : "SSE2";
                }
                return runtimeNativeIsa;
            }
        }

        /// <summary>Payload name of a runtime file for the selected ISA.</summary>
        private static string RuntimePayload(string name)
        {
            return RuntimeNativeIsa == "AVX2" && dualRuntime ? "avx2/" + name : name;
        }

        private static bool dualRuntime;

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
        private bool preservingModules;
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
        public string ModuleUpdateNotice { get; private set; }
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
            // The prebuilt runtime and the prebuilt float and DSP libraries are each compiled for exactly one
            // ISA. A package built for AVX2 must not be installed on a host that cannot run it. Otherwise the
            // failure is an illegal-instruction fault inside the game at an arbitrary later time, not a setup
            // error. This check is the only place where that failure can be attributed.
            if (RuntimeNativeIsa == "AVX2" && !HostSupportsAvx2())
                problems.Add("This package's runtime was built for AVX2, which this CPU does not expose. Use a package built with -NativeIsa SSE2, or install on a CPU with AVX2.");
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

        /// <summary>
        /// Less RAM than Nebula is designed for does not stop an install: the compile runs one job at a time
        /// (DefaultCompileJobs). Setup asks first, with this text; null when the PC has enough.
        /// </summary>
        /// <summary>
        /// The Microsoft C++ Build Tools license is asked for only when this install downloads the compiler: not
        /// when the install folder has it already, and not for an update (its first install accepted it).
        /// </summary>
        public static bool BuildToolsLicenseRequired(InstallMode mode, Toolchain toolchain)
        {
            return mode != InstallMode.Update && !toolchain.IsReady;
        }

        public static string LowMemoryWarning()
        {
            ulong memory = NativeMethods.TotalPhysicalMemory();
            if (memory >= MinimumMemory) return null;
            return "This PC has " + FileUtil.FormatBytes((long)memory) + " of RAM. Nebula is designed for 8 GB or more.\r\n\r\n" +
                "Installing with less is experimental: compiling the game takes much longer, and the game may stutter, " +
                "run slowly or close unexpectedly.\r\n\r\n" +
                "If you continue, you do so at your own risk. Nebula's developer is not responsible for anything that " +
                "happens while the game runs on this PC.";
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
            if (LowMemoryWarning() != null)
                Log("RAM: " + FileUtil.FormatBytes((long)NativeMethods.TotalPhysicalMemory()) + ", below the 8 GB Nebula is designed for; installing as experimental.");
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
                preservingModules = current != null && request.Mode == InstallMode.Update;
                if (preservingModules)
                {
                    preservingModules = request.GameInput == null && current.FilesIntact(ModuleFiles) && ContentValid();
                    if (preservingModules)
                    {
                        try { ModuleCompatibility.Validate(current.Directory, Json.Parse(Payload.ReadText("module-compatibility.json"))); }
                        catch (InvalidDataException error) { preservingModules = false; Log("Module validation requires rebuild fallback: " + error.Message); }
                        catch (InvalidOperationException error) { preservingModules = false; Log("Module ABI requires rebuild fallback: " + error.Message); }
                    }
                    if (preservingModules && ModulesNeedRebuild(current))
                    {
                        preservingModules = false;
                        Log("The installed " + current.ModuleIsa + " game modules are generation " + current.ModuleGeneration +
                            "; this release needs " + ModuleGeneration + " (a relink cannot update their code). Rebuilding them.");
                    }
                    if (preservingModules && current.ModuleKey != ModuleKey &&
                        !File.Exists(Path.Combine(current.Directory, "relink-cache", "manifest.json")))
                    {
                        preservingModules = false;
                        Log("No retained objects for changed module dependencies. Rebuilding once as the last resort; this install will retain objects for later relinks.");
                    }
                }
                bool needInput = !preservingModules && (request.GameInput != null || !RetainedInputsValid() || !ContentValid());
                if (needInput && request.GameInput == null)
                    throw new InvalidOperationException("Choose your Super Mario Galaxy ISO, RVZ or extracted folder.");

                bool reuseModules = preservingModules;
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
                    string patchLibraries = Path.Combine(staging, "runtime-libs");
                    foreach (var lib in RuntimeLibraries) Payload.Extract(RuntimePayload(lib), Path.Combine(patchLibraries, lib));
                    try
                    {
                    bool linked = RelinkCache.Update(current.Directory, app, patchLibraries, toolchain.Id,
                        delegate(IList<string> args) {
                            if (!toolchain.IsReady) toolchain.Ensure(BackgroundProgress, Log, cancel);
                            return RunTool(toolchain.LldLink, args, toolchain.BuildEnvironment(), Log);
                        }, Log);
                    if (!linked)
                    {
                        ModuleUpdateNotice = "Nebula was updated. The game modules from the previous version were kept.";
                        Log(ModuleUpdateNotice);
                    }
                    ModuleCompatibility.Validate(app, Json.Parse(Payload.ReadText("module-compatibility.json")));
                    }
                    catch (InvalidDataException error) { Log("Retained object validation failed: " + error.Message); RebuildForUpdate(app); }
                    catch (InvalidOperationException error) { Log("Relink path unavailable: " + error.Message); RebuildForUpdate(app); }
                }
                else
                {
                    Stage(0.18, 0.04, "Recompiling the game code");
                    string inputs = request.GameInput != null ? Path.Combine(staging, "game-inputs") : layout.GameInputs;
                    string gen = Path.Combine(staging, "generated");
                    RunRecomp(new[] { "generate", inputs, "--output", gen }, null);

                    Stage(0.22, 0.08, "Preparing the compiler");
                    waitingForDownloads = true;
                    prepare.Join();
                    waitingForDownloads = false;
                    if (prepareFailure != null) ExceptionDispatchInfo.Capture(prepareFailure).Throw();
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

        private void RebuildForUpdate(string app)
        {
            cancel.ThrowIfCancellationRequested();
            if (!RetainedInputsValid() || !ContentValid())
                throw new InvalidOperationException("The game modules must be rebuilt for this update. Run Setup again and select your game.");
            preservingModules = false;
            ModuleUpdateNotice = null;
            Stage(0.20, 0.70, "Rebuilding the game modules");
            string expected = Path.GetFullPath(Path.Combine(staging, "app"));
            if (!string.Equals(Path.GetFullPath(app), expected, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Invalid update staging path.");
            FileUtil.DeleteTree(app);
            Directory.CreateDirectory(app);
            toolchain.Ensure(BackgroundProgress, Log, cancel);
            string sourceDir = source.Ensure(layout.Sources, request.SourceOverride, BackgroundProgress, Log, cancel);
            string gen = Path.Combine(staging, "fallback-generated");
            RunRecomp(new[] {"generate", layout.GameInputs, "--output", gen}, null);
            Build(sourceDir, gen, app);
        }

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

        /// <summary>The game files an install keeps (its inputs and packed content) are intact, so an update or repair needs no game.</summary>
        public static bool RetainedGameValid(InstallLayout layout)
        {
            return RetainedInputsValid(layout) && ContentValid(layout);
        }

        private bool RetainedInputsValid() { return RetainedInputsValid(layout); }

        private bool ContentValid() { return ContentValid(layout); }

        private static bool RetainedInputsValid(InstallLayout layout)
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

        private static bool ContentValid(InstallLayout layout)
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
            foreach (var lib in RuntimeLibraries) Payload.Extract(RuntimePayload(lib), Path.Combine(libs, lib));
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
            RelinkCache.Capture(build, app, libs, toolchain.Id);
        }

        /// <summary>
        /// Seeds the DXBC and PSO caches, so a user's first launch is warmed instead of paying the full
        /// cold-start compile.
        ///
        /// Only these two files are seeded. The runtime's loaders explain why: load_disk_cache checks magic
        /// and version, then checksums and reflects every record, with no adapter check, so shaders.bin is
        /// portable across GPUs. load_pso_key_cache is likewise adapter-free. pipelines.bin is not seeded:
        /// load_pipeline_library compares the adapter LUID and discards the file on any mismatch, so a foreign
        /// copy would always be rejected.
        ///
        /// Every failure degrades to the current behaviour rather than a wedge. A version mismatch sets the
        /// runtime's repair flag and the file is ignored, so the user pays the same cold start as without the
        /// seed. The File.Exists guard means a cache this machine already built is never replaced.
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
                    // Absent from the payload only when the release omits it on purpose (see build-release.ps1's version
                    // guard for psos.bin). That is not an error.
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
            Payload.Extract(RuntimePayload("NebulaRuntime.exe"), Path.Combine(app, "NebulaRuntime.exe"));
            foreach (var name in new[] { "Nebula.exe", "NebulaSettings.exe", "runtime-env.json", "LICENSE", "THIRD-PARTY-NOTICES.md", "README.md", "nebula-recomp.exe" })
                Payload.Extract(name, Path.Combine(app, name));
            File.WriteAllText(Path.Combine(app, "runtime-isa.txt"), RuntimeNativeIsa);

            var files = new Dictionary<string, object>();
            foreach (var path in Directory.GetFiles(app)) files[Path.GetFileName(path)] = FileUtil.Sha256File(path);
            Json.Save(Path.Combine(app, "install.json"), new Dictionary<string, object>
            {
                { "schema", "nebula.install.v1" },
                { "version", BuildInfo.Version },
                { "commit", source.Commit },
                { "repository", source.Repository },
                { "moduleKey", preservingModules ? current.ModuleKey : ModuleKey },
                { "moduleSourceCommit", preservingModules ? current.ModuleSourceCommit : source.Commit },
                { "toolchain", toolchain.Id },
                { "contentFormatVersion", ContentFormatVersion },
                // The CPU target of the prebuilt runtime and the prebuilt float and DSP libraries, and separately of the
                // modules compiled on this host. Installs that share a version label but differ here do not run the same
                // machine code, so a recorded session must show which pair it measured instead of inferring it from file hashes.
                { "runtimeIsa", RuntimeNativeIsa },
                { "moduleIsa", preservingModules ? current.ModuleIsa : NativeIsa },
                { "moduleGeneration", preservingModules ? current.ModuleGeneration : ModuleGeneration },
                { "installedUtc", DateTime.UtcNow.ToString("o") },
                { "files", files }
            });
        }

        /// <summary>Load each compiled module and check its exports before switching.</summary>
        private void ValidateApp(string app)
        {
            foreach (var name in ModuleFiles.Concat(new[] { "NebulaRuntime.exe", "Nebula.exe", "NebulaSettings.exe" }))
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
            string versionDir = layout.VersionDir(BuildInfo.Version);
            WaitUntilClosed(Directory.Exists(versionDir),
                Directory.Exists(Path.Combine(staging, "content")) || Directory.Exists(Path.Combine(staging, "game-inputs")));
            Directory.CreateDirectory(layout.Versions);
            string replaced = versionDir + ".replaced-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            Swap(app, versionDir, replaced, Log);

            string newContent = Path.Combine(staging, "content");
            if (Directory.Exists(newContent))
            {
                Json.Save(Path.Combine(newContent, "content.json"), new Dictionary<string, object> { { "formatVersion", ContentFormatVersion }, { "createdUtc", DateTime.UtcNow.ToString("o") } });
                Swap(newContent, layout.Content, Path.Combine(staging, "content-old"), Log);
            }
            string newInputs = Path.Combine(staging, "game-inputs");
            if (Directory.Exists(newInputs))
                Swap(newInputs, layout.GameInputs, Path.Combine(staging, "game-inputs-old"), Log);

            string previous = current != null && current.Version != BuildInfo.Version ? current.Version : (current != null ? current.Previous : null);
            Json.Save(layout.Current, new Dictionary<string, object>
            {
                { "version", BuildInfo.Version },
                { "previous", previous }
            });
            // Nebula is installed from here on: what follows is tidying and shortcuts, and does not fail the install.
            // Keep exactly one previous version for rollback; a folder still held is removed by a later install.
            foreach (var dir in Directory.GetDirectories(layout.Versions))
            {
                string name = Path.GetFileName(dir);
                if (name != BuildInfo.Version && name != previous)
                {
                    try { FileUtil.DeleteTree(dir); }
                    catch (IOException error) { Log("Could not remove " + dir + ": " + error.Message); }
                    catch (UnauthorizedAccessException error) { Log("Could not remove " + dir + ": " + error.Message); }
                }
            }
            string self = Assembly.GetExecutingAssembly().Location;
            if (!string.Equals(Path.GetFullPath(self), layout.SetupCopy, StringComparison.OrdinalIgnoreCase))
                CopySetup(self);
            foreach (var failure in Integration.Register(layout, BuildInfo.Version, request.DesktopShortcut))
                Log("Could not create the shortcut " + failure + "; Nebula.exe is in " + versionDir + ".");
            InstalledVersionDir = versionDir;
            Log("Installed " + BuildInfo.Version + " to " + versionDir + (previous != null ? "; kept " + previous + " for rollback." : "."));
        }

        /// <summary>
        /// An update installs beside the running version, so the launcher it was started from (or the game) may stay
        /// open. Nebula has to close only when this install replaces the folder it runs from (a repair), or the game
        /// files the game reads. Setup then asks and waits; unattended, it fails.
        /// </summary>
        private void WaitUntilClosed(bool replacingVersion, bool replacingGameFiles)
        {
            while (true)
            {
                string running = replacingVersion ? Running.Find(layout.VersionDir(BuildInfo.Version), Running.Programs) : null;
                if (running == null && replacingGameFiles) running = Running.Find(layout.Root, "NebulaRuntime");
                if (running == null) return;
                Log("Waiting for " + running + " to close.");
                if (!progress.AskToClose("Nebula is running (" + running + "). Close the game, the Nebula launcher and Nebula " +
                        "Settings, then choose Retry to finish installing Nebula " + BuildInfo.Version + "."))
                    throw new InvalidOperationException("Nebula is running. Close the game, the Nebula launcher and Nebula Settings, then try again.");
            }
        }

        /// <summary>
        /// Puts <paramref name="fresh"/>, built in staging, at <paramref name="destination"/>. What is there is
        /// renamed to <paramref name="aside"/> first, never copied (a copy would then delete it file by file, and
        /// stop half way at a file another program holds), and renamed back if the new tree cannot be moved in.
        /// </summary>
        internal static void Swap(string fresh, string destination, string aside, Action<string> log)
        {
            bool existed = Directory.Exists(destination);
            if (existed) FileUtil.MoveDirectoryInPlace(destination, aside);
            try { FileUtil.MoveDirectory(fresh, destination, log); }
            catch (Exception)
            {
                if (existed)
                {
                    try { FileUtil.MoveDirectoryInPlace(aside, destination); }
                    catch (Exception error) { log("Could not put " + destination + " back from " + aside + ": " + error.Message); }
                }
                throw;
            }
        }

        /// <summary>The Start menu's Update and Setup shortcuts run this copy. A held file is retried, then logged.</summary>
        private void CopySetup(string self)
        {
            for (int attempt = 0; ; attempt++)
            {
                try { File.Copy(self, layout.SetupCopy, true); return; }
                catch (Exception error)
                {
                    if (!(error is IOException) && !(error is UnauthorizedAccessException)) throw;
                    if (attempt >= 20) { Log("Could not copy Setup to " + layout.SetupCopy + ": " + error.Message); return; }
                    Thread.Sleep(500);
                }
            }
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
