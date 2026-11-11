using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using System.Threading;

namespace Nebula.Setup
{
    internal enum InstallMode { Install, Repair, Update }

    internal static class InstallIntent
    {
        // A package newer than the installed version updates it; the same or an older one repairs it.
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
        /// <summary>ISO/RVZ/other image or extracted folder; null to keep the installed game content.</summary>
        public string GameInput;
        public string InstallRoot;
        public bool DesktopShortcut;
        /// <summary>Download NVIDIA DLSS into the user data folder (the player accepted NVIDIA's license).</summary>
        public bool InstallDlss;
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
    /// version is assembled, and the previous one is kept.
    /// </summary>
    internal sealed class InstallEngine
    {
        /// <summary>Bump when nebula-import's content folder changes shape. 2: the game's files cooked (textures as
        /// texture records, archives expanded); a format-1 folder is cooked on update without the disc. 3: the game's data in
        /// host order (B-02); a format-2 folder cannot be cooked again: it needs the disc. 4 to 6: later changes of the store's
        /// shape; only a format-1 folder is cooked from, every other older one needs the disc.</summary>
        public const int ContentFormatVersion = 6;
        /// <summary>The content format nebula-import's cook command converts from; a version of it reads only version 2 saves.</summary>
        internal const int CookableContentFormatVersion = 1;

        /// <summary>The instruction set NebulaNative.exe and smg_game.dll are built for (the module: x86-64-v3, which every
        /// AVX2 CPU runs; native/CMakeLists.txt, NEBULA_CPU_LEVEL).</summary>
        private const string NativeIsa = "AVX2";

        /// <summary>What a version folder holds (each from the package, by its published name).</summary>
        private static readonly string[] NativeFiles =
        {
            "NebulaNative.exe", "smg_game.dll", "nebula_import.dll", "nebula-import.exe", @"dxc\dxc.exe", @"dxc\dxcompiler.dll", @"dxc\dxil.dll", @"dxc\LICENSE-MS.txt", @"dxc\LICENCE-MIT.txt", @"dxc\LICENSE-LLVM.txt", @"D3D12\D3D12Core.dll", @"D3D12\LICENSE.txt", "material-names.tsv", "rt-materials.tsv", "rt-suns.tsv",
            "Nebula.exe", "NebulaSettings.exe", "LICENSE", "THIRD-PARTY-NOTICES.md", "README.md"
        };

        private readonly InstallRequest request;
        private readonly IInstallProgress progress;
        private readonly CancellationToken cancel;
        private readonly InstallLayout layout;
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
            Directory.CreateDirectory(NebulaPaths.SetupLogs);
            string logPath = Path.Combine(NebulaPaths.SetupLogs,
                DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff") + "-" + Guid.NewGuid().ToString("N") + ".log");
            logFile = new StreamWriter(new FileStream(logPath, FileMode.CreateNew, FileAccess.Write, FileShare.Read));
            logFile.AutoFlush = true;
            LogPath = logPath;
        }

        public string LogPath { get; private set; }
        public string InstalledVersionDir { get; private set; }

        /// <summary>Why the requested DLSS download failed; null when it succeeded or was not requested.</summary>
        public string DlssFailure { get; private set; }
        public TimeSpan Elapsed { get { return clock.Elapsed; } }

        /// <summary>The packed game (3.2 GB) and its staging copy for a first install.</summary>
        public const long RequiredFreeBytes = 8L << 30;

        public static List<string> CheckRequirements(string installRoot)
        {
            var problems = new List<string>();
            if (!Environment.Is64BitOperatingSystem) problems.Add("Nebula needs 64-bit Windows 10 or 11.");
            if (Environment.OSVersion.Version.Major < 10) problems.Add("Nebula needs Windows 10 or 11.");
            // Without AVX2 the game would stop at its first AVX2 instruction (an illegal-instruction fault), not here.
            if (!HostSupportsAvx2())
                problems.Add(NebulaText.Of("This package's runtime was built for AVX2, which this CPU does not expose."));
            try
            {
                string fs = FileUtil.FileSystemName(installRoot);
                if (!string.Equals(fs, "NTFS", StringComparison.OrdinalIgnoreCase))
                    problems.Add(NebulaText.Of("The install folder must be on an NTFS drive (this one is {0}).", fs));
                long free = FileUtil.FreeBytes(installRoot);
                if (free < RequiredFreeBytes)
                    problems.Add(NebulaText.Of("Setup needs {0} free; the drive has {1}.", FileUtil.FormatBytes(RequiredFreeBytes), FileUtil.FormatBytes(free)));
            }
            catch (Exception error) { problems.Add(NebulaText.Of("Cannot inspect the install folder: {0}", error.Message)); }
            return problems;
        }

        /// <summary>PF_AVX2_INSTRUCTIONS_AVAILABLE: the CPU and the OS support AVX2.</summary>
        private static bool HostSupportsAvx2()
        {
            try { return IsProcessorFeaturePresent(40); }
            catch (Exception) { return false; }
        }

        public void Run()
        {
            Log("Nebula Setup " + BuildInfo.Version + " (" + BuildInfo.Commit + "), " + request.Mode);
            Log("Install folder: " + layout.Root);
            Directory.CreateDirectory(layout.Root);
            Directory.CreateDirectory(layout.Staging);
            CleanAbandonedStaging();
            staging = Path.Combine(layout.Staging, DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
            Directory.CreateDirectory(staging);
            var dlssCancel = CancellationTokenSource.CreateLinkedTokenSource(cancel);
            // DLSS lives in the user data folder, apart from the version being assembled, and
            // downloads while the game is packed. A failed download does not fail the install.
            Thread dlss = null;
            if (request.InstallDlss)
            {
                dlss = new Thread(delegate()
                {
                    try
                    {
                        DlssPackage.Install((done, total) => { if (waitingForDlss) StepProgress(null, total > 0 ? (double)done / total : -1); }, dlssCancel.Token);
                        Log("NVIDIA DLSS installed in " + DlssPackage.Folder);
                    }
                    catch (OperationCanceledException) { }
                    catch (Exception error)
                    {
                        DlssFailure = error.Message;
                        Log("NVIDIA DLSS download failed: " + error);
                    }
                });
                dlss.IsBackground = true;
                dlss.Start();
            }
            try
            {
                Install(InstalledInfo.Read(layout), dlss);
            }
            catch (Exception error)
            {
                dlssCancel.Cancel();
                Log("FAILED: " + error);
                throw;
            }
            finally
            {
                if (dlss != null) dlss.Join();
                dlssCancel.Dispose();
                try { FileUtil.DeleteTree(staging); } catch (Exception error) { Log("Could not remove staging: " + error.Message); }
                logFile.Dispose();
            }
        }

        /// <summary>
        /// Installs this package's version. The packed game (content\game.pak) is kept unless a game was chosen; the
        /// saves and the settings are in the user data folder and stay. Afterwards, what only a recompiled game used goes.
        /// </summary>
        private void Install(InstalledInfo current, Thread dlss)
        {
            bool reuseContent = request.GameInput == null && ContentValid(layout);
            string keptContent = request.GameInput == null && !reuseContent ? KeptCurrentContent(layout) : null;
            string cookSource = request.GameInput == null && !reuseContent && keptContent == null ? CookSource(layout) : null;
            // A packed game of an older store cannot be read by this version and cannot be cooked again (the cook reads the raw
            // files, which an import never keeps): it is packed again from the image it was made from, when that is still where
            // it was, so an update needs no question. Otherwise the disc is asked for, as before.
            string remembered = request.GameInput == null && !reuseContent && keptContent == null && cookSource == null ? RememberedSource(layout) : null;
            string gameInput = request.GameInput ?? remembered;
            if (!reuseContent && keptContent == null && cookSource == null && gameInput == null)
                throw new InvalidOperationException(NebulaText.Of("Choose your Super Mario Galaxy ISO, RVZ or extracted folder."));
            Log("Reuse the packed game: " + reuseContent + "; kept for this version: " + keptContent + "; cook from: " + cookSource + "; pack again from: " + remembered);
            bool imported = false;
            if (cookSource != null)
            {
                Stage(0.00, 0.70, NebulaText.Of("Converting the game files"));
                Cook(cookSource, Path.Combine(staging, "content"));
            }
            else if (!reuseContent && keptContent == null)
            {
                Stage(0.00, 0.02, NebulaText.Of("Checking your game"));
                Identify(gameInput);
                Stage(0.02, 0.68, NebulaText.Of("Packing the game files"));
                Import(gameInput, Path.Combine(staging, "content"));
                imported = true;
            }

            Stage(0.75, 0.15, NebulaText.Of("Installing Nebula {0}", BuildInfo.Version));
            string app = Path.Combine(staging, "app");
            Directory.CreateDirectory(app);
            foreach (var name in NativeFiles) Payload.Extract(name, Path.Combine(app, name));
            File.WriteAllText(Path.Combine(app, "runtime-isa.txt"), NativeIsa);
            var files = new Dictionary<string, object>();
            foreach (var path in Directory.GetFiles(app, "*", SearchOption.AllDirectories))
                files[path.Substring(app.Length).TrimStart(Path.DirectorySeparatorChar)] = FileUtil.Sha256File(path);
            Json.Save(Path.Combine(app, "install.json"), new Dictionary<string, object>
            {
                { "schema", "nebula.install.v1" },
                { "version", BuildInfo.Version },
                { "commit", BuildInfo.Commit },
                { "repository", BuildInfo.Repository },
                { "game", "native" },
                { "contentFormatVersion", ContentFormatVersion },
                { "runtimeIsa", NativeIsa },
                { "installedUtc", DateTime.UtcNow.ToString("o") },
                { "files", files }
            });
            foreach (var name in NativeFiles)
                if (new FileInfo(Path.Combine(app, name)).Length == 0) throw new InvalidOperationException("The new version's " + name + " is empty.");

            if (dlss != null && dlss.IsAlive)
            {
                Stage(0.90, 0.04, NebulaText.Of("Downloading NVIDIA DLSS"));
                waitingForDlss = true;
                while (!dlss.Join(200)) cancel.ThrowIfCancellationRequested();
                waitingForDlss = false;
            }
            Stage(0.94, 0.06, NebulaText.Of("Switching to the new version"));
            // The versions it replaces are removed: nothing may run from this install meanwhile.
            while (true)
            {
                string running = Running.Find(layout.Root, Running.Programs);
                if (running == null) break;
                Log("Waiting for " + running + " to close.");
                if (!progress.AskToClose(NebulaText.Of("Nebula is running ({0}). Close the game, the Nebula launcher and Nebula Settings, then choose Retry to finish installing Nebula {1}.", running, BuildInfo.Version)))
                    throw new InvalidOperationException(NebulaText.Of("Nebula is running. Close the game, the Nebula launcher and Nebula Settings, then try again."));
            }
            RemoveOtherVersions(Commit(app, current, keptContent));
            if (imported) RememberSource(gameInput);
            RemoveRecompiledGame();
            var installed = InstalledInfo.Read(layout);
            PruneKeptContent(layout, installed != null ? installed.Previous : null, Log);
            Report(1.0, NebulaText.Of("Nebula {0} is installed.", BuildInfo.Version), 1);
            Log(string.Format("Completed in {0:mm\\:ss}.", clock.Elapsed));
        }

        /// <summary>
        /// After an install: the other versions (those that hold a recompiled game: NebulaRuntime.exe and the RMGE01
        /// modules), the compiler and the source those installs downloaded, and their game importer go. A file still held
        /// (a launcher left open) is logged and removed by a later install.
        /// </summary>
        private void RemoveRecompiledGame()
        {
            // The previous native version stays for rollback; a recompiled one cannot run this content, so it goes.
            var current = InstalledInfo.Read(layout);
            string previous = current != null ? current.Previous : null;
            bool previousRemoved = false;
            foreach (var dir in Directory.GetDirectories(layout.Versions))
            {
                string name = Path.GetFileName(dir);
                if (string.Equals(name, BuildInfo.Version, StringComparison.OrdinalIgnoreCase) || !HoldsRecompiledGame(dir)) continue;
                TryDeleteTree(dir);
                previousRemoved = previousRemoved || string.Equals(name, previous, StringComparison.OrdinalIgnoreCase);
            }
            TryDeleteTree(layout.Toolchains);
            TryDeleteTree(layout.Sources);
            foreach (var file in Directory.GetFiles(layout.Staging, "nebula-recomp-*.exe"))
            {
                try { File.Delete(file); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            }
            if (previousRemoved)
                Json.Save(layout.Current, new Dictionary<string, object> { { "version", BuildInfo.Version }, { "previous", null } });
        }

        /// <summary>A version folder of the recompiled game (1.x): its runtime or its recompiled modules.</summary>
        internal static bool HoldsRecompiledGame(string dir)
        {
            return File.Exists(Path.Combine(dir, "NebulaRuntime.exe")) || Directory.GetFiles(dir, "RMGE01_*.dll").Length != 0;
        }

        private void TryDeleteTree(string dir)
        {
            if (!Directory.Exists(dir)) return;
            try { FileUtil.DeleteTree(dir); Log("Removed " + dir); }
            catch (IOException error) { Log("Could not remove " + dir + ": " + error.Message); }
            catch (UnauthorizedAccessException error) { Log("Could not remove " + dir + ": " + error.Message); }
        }

        private void Identify(string input)
        {
            var lines = new List<string>();
            int code = RunTool(ImporterPath, new[] { "identify", input }, delegate(string line) { lines.Add(line); });
            string json = string.Join("\n", lines);
            Log(json);
            Dictionary<string, object> identity;
            try { identity = Json.Parse(json.Substring(Math.Max(0, json.IndexOf('{')))); }
            catch (Exception) { throw new InvalidOperationException(lines.Count > 0 ? lines[lines.Count - 1] : NebulaText.Of("The selected file could not be read.")); }
            if (code != 0 || !Json.Bool(identity, "supported"))
                throw new InvalidOperationException(Json.Str(identity, "message"));
        }

        private static readonly Regex ImportProgress = new Regex(@"^PROGRESS (\d+) (\d+)$");

        /// <summary>The content folder (game.pak and the files beside it) from the player's disc image or extracted folder.</summary>
        private void Import(string input, string content)
        {
            int code = RunTool(ImporterPath, new[] { "import", input, "--output", content }, delegate(string line)
            {
                var match = ImportProgress.Match(line);
                if (match.Success)
                {
                    StepProgress(null, double.Parse(match.Groups[1].Value) / Math.Max(1, double.Parse(match.Groups[2].Value)));
                    return;
                }
                if (line.StartsWith("STEP ")) StepProgress(line.Substring(line.IndexOf(' ', 5) + 1), -1);
                Log(line);
            });
            if (code != 0) throw new InvalidOperationException(NebulaText.Of("Reading the game failed (exit {0}). See the setup log.", code));
        }

        /// <summary>A format-1 content folder cooked into this version's format (D-07): the same output lines as an import.</summary>
        private void Cook(string content, string output)
        {
            int code = RunTool(ImporterPath, new[] { "cook", content, "--output", output }, delegate(string line)
            {
                var match = ImportProgress.Match(line);
                if (match.Success)
                {
                    StepProgress(null, double.Parse(match.Groups[1].Value) / Math.Max(1, double.Parse(match.Groups[2].Value)));
                    return;
                }
                if (line.StartsWith("STEP ")) StepProgress(line.Substring(line.IndexOf(' ', 5) + 1), -1);
                Log(line);
            });
            if (code != 0) throw new InvalidOperationException(NebulaText.Of("Packing the game failed (exit {0}). See the setup log.", code));
        }

        /// <summary>The packed game an install keeps is intact, kept aside or can be cooked from, so an update or repair needs no game.</summary>
        public static bool RetainedGameValid(InstallLayout layout)
        {
            return ContentValid(layout) || KeptCurrentContent(layout) != null || CookSource(layout) != null || RememberedSource(layout) != null;
        }

        private static string GameSourcePath(InstallLayout layout) { return Path.Combine(layout.Root, "game-source.json"); }

        /// <summary>
        /// The disc image or folder the packed game was last made from, when it is still there as it was (a file of the same
        /// size); null otherwise. Kept in the install root beside current.json, not in the data folder: it belongs to this install.
        /// </summary>
        internal static string RememberedSource(InstallLayout layout)
        {
            try
            {
                string file = GameSourcePath(layout);
                if (!File.Exists(file)) return null;
                var map = Json.Load(file);
                string path = Json.Str(map, "path");
                if (string.IsNullOrEmpty(path)) return null;
                if (Json.Bool(map, "folder")) return Directory.Exists(path) ? path : null;
                return File.Exists(path) && new FileInfo(path).Length == Json.Long(map, "size") ? path : null;
            }
            catch (Exception) { return null; }
        }

        /// <summary>Notes where the packed game came from, for the next update that has to pack it again (a failure only costs that question).</summary>
        private void RememberSource(string input)
        {
            try
            {
                bool folder = Directory.Exists(input);
                Json.Save(GameSourcePath(layout), new Dictionary<string, object>
                {
                    { "path", Path.GetFullPath(input) },
                    { "folder", folder },
                    { "size", folder ? 0L : new FileInfo(input).Length },
                    { "storeVersion", (long)BuildInfo.StoreVersion },
                    { "recordedUtc", DateTime.UtcNow.ToString("o") }
                });
            }
            catch (Exception error) { Log("Could not note where the game came from: " + error.Message); }
        }

        /// <summary>A content folder's format; -1 without its marker or its game.pak.</summary>
        internal static long ContentFormatOf(string content)
        {
            string marker = Path.Combine(content, "content.json");
            if (!File.Exists(marker) || !File.Exists(Path.Combine(content, "game.pak"))) return -1;
            try { return Json.Long(Json.Load(marker), "formatVersion"); }
            catch (Exception) { return -1; }
        }

        /// <summary>The content format a version folder reads; 0 when it does not say, -1 when it cannot be read.</summary>
        internal static long VersionContentFormat(string versionDir)
        {
            try { return Json.Long(Json.Load(Path.Combine(versionDir, "install.json")), "contentFormatVersion"); }
            catch (Exception) { return -1; }
        }

        private static bool CurrentStoreValid(string content)
        {
            try
            {
                using (var reader = new BinaryReader(File.OpenRead(Path.Combine(content, "game.pak"))))
                    return System.Text.Encoding.ASCII.GetString(reader.ReadBytes(8)) == "NBSTORE\0" && reader.ReadUInt32() == BuildInfo.StoreVersion;
            }
            catch (Exception) { return false; }
        }
        private static bool ContentValid(InstallLayout layout)
        {
            return ContentFormatOf(layout.Content) == ContentFormatVersion && CurrentStoreValid(layout.Content);
        }

        /// <summary>This version's content, set aside by a rollback; null when there is none.</summary>
        internal static string KeptCurrentContent(InstallLayout layout)
        {
            string kept = layout.KeptContent(ContentFormatVersion);
            return ContentFormatOf(kept) == ContentFormatVersion && CurrentStoreValid(kept) ? kept : null;
        }

        /// <summary>The format-1 content to cook from: the installed one, or the one an earlier cook kept. Null: the disc is needed.</summary>
        internal static string CookSource(InstallLayout layout)
        {
            foreach (var content in new[] { layout.Content, layout.KeptContent(CookableContentFormatVersion) })
                if (ContentFormatOf(content) == CookableContentFormatVersion) return content;
            return null;
        }

        /// <summary>The content an update replaced, by its format, beside the new.</summary>
        internal static void KeepReplacedContent(InstallLayout layout, string old, Action<string> log)
        {
            long format = ContentFormatOf(old);
            if (format <= 0 || format == ContentFormatVersion) return;
            string kept = layout.KeptContent(format);
            try
            {
                FileUtil.DeleteTree(kept);
                FileUtil.MoveDirectoryInPlace(old, kept);
                log("Kept the previous content for rollback in " + kept);
            }
            catch (Exception error) { log("Could not keep " + old + ": " + error.Message); }
        }

        /// <summary>Of the kept content, format 1 (cooked from) and the format the previous version reads stay.</summary>
        internal static void PruneKeptContent(InstallLayout layout, string previous, Action<string> log)
        {
            long reads = previous != null ? VersionContentFormat(layout.VersionDir(previous)) : -1;
            foreach (var dir in Directory.GetDirectories(layout.Root, InstallLayout.KeptContentPrefix + "*"))
            {
                long format;
                if (!long.TryParse(Path.GetFileName(dir).Substring(InstallLayout.KeptContentPrefix.Length), out format) ||
                    format == CookableContentFormatVersion || format == reads) continue;
                try { FileUtil.DeleteTree(dir); log("Removed " + dir); }
                catch (IOException error) { log("Could not remove " + dir + ": " + error.Message); }
                catch (UnauthorizedAccessException error) { log("Could not remove " + dir + ": " + error.Message); }
            }
        }

        /// <summary>Switches to the new version and returns the one kept for rollback. Nothing old is deleted here.</summary>
        private string Commit(string app, InstalledInfo current, string keptContent)
        {
            string versionDir = layout.VersionDir(BuildInfo.Version);
            string newContent = keptContent ?? Path.Combine(staging, "content");
            bool swapContent = Directory.Exists(newContent);
            if (swapContent && keptContent == null)
            {
                // The new packed game is stamped and has to open before anything installed is touched: one that does not stays in staging.
                Json.Save(Path.Combine(newContent, "content.json"), new Dictionary<string, object> { { "formatVersion", ContentFormatVersion }, { "createdUtc", DateTime.UtcNow.ToString("o") } });
                if (!StoreOpens(newContent))
                    throw new InvalidOperationException(NebulaText.Of("The packed game did not come out whole. Nothing installed was changed."));
            }
            WaitUntilClosed(Directory.Exists(versionDir), swapContent);
            Directory.CreateDirectory(layout.Versions);
            // The saves, the settings and the mods' small files stay where they are (the data folder); a copy is taken once, before
            // a new version can convert anything of them.
            UserDataSnapshot.Take(current != null ? current.Version : null, Log);
            string previous = current != null && current.Version != BuildInfo.Version ? current.Version : (current != null ? current.Previous : null);
            string replaced = versionDir + ".replaced-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            string oldContent = Path.Combine(staging, "content-old");
            Swap(app, versionDir, replaced, Log);
            bool contentSwapped = false;
            try
            {
                if (swapContent)
                {
                    Swap(newContent, layout.Content, oldContent, Log);
                    contentSwapped = true;
                }
                // The previous version and its packed game stay beside the new ones until the new ones are seen whole in place.
                if (!File.Exists(Path.Combine(versionDir, "NebulaNative.exe")) || !StoreOpens(layout.Content))
                    throw new InvalidOperationException(NebulaText.Of("The new version or its packed game did not come out whole. The previous version is back."));
                Json.Save(layout.Current, new Dictionary<string, object>
                {
                    { "version", BuildInfo.Version },
                    { "previous", previous }
                });
            }
            catch (Exception error)
            {
                Log("Putting the previous version back: " + error.Message);
                UndoCommit(versionDir, replaced, contentSwapped, newContent, oldContent);
                throw;
            }
            if (swapContent) KeepReplacedContent(layout, oldContent, Log);
            // Nebula is installed from here on: what follows is shortcuts, and does not fail the install.
            string self = Assembly.GetExecutingAssembly().Location;
            if (!string.Equals(Path.GetFullPath(self), layout.SetupCopy, StringComparison.OrdinalIgnoreCase))
                CopySetup(self);
            foreach (var failure in Integration.Register(layout, BuildInfo.Version, request.DesktopShortcut))
                Log("Could not create the shortcut " + failure + "; Nebula.exe is in " + versionDir + ".");
            InstalledVersionDir = versionDir;
            Log("Installed " + BuildInfo.Version + " to " + versionDir + (previous != null ? "; kept " + previous + " for rollback." : "."));
            return previous;
        }

        /// <summary>A packed game that opens: its marker says this format, its game.pak starts with this build's store version, and it is not an empty shell.</summary>
        private static bool StoreOpens(string content)
        {
            try
            {
                return ContentFormatOf(content) == ContentFormatVersion && CurrentStoreValid(content) &&
                       new FileInfo(Path.Combine(content, "game.pak")).Length > (1L << 20);
            }
            catch (Exception) { return false; }
        }

        /// <summary>
        /// A switch that did not take, undone: the new packed game goes back to where it came from (staging, or the folder kept
        /// for this version) and the previous one into place, the new version's folder goes and the previous version's returns.
        /// Each step is logged if it fails; the install stays as whole as it can be.
        /// </summary>
        private void UndoCommit(string versionDir, string replaced, bool contentSwapped, string newContent, string oldContent)
        {
            if (contentSwapped)
            {
                try
                {
                    FileUtil.MoveDirectoryInPlace(layout.Content, newContent);
                    if (Directory.Exists(oldContent)) FileUtil.MoveDirectoryInPlace(oldContent, layout.Content);
                }
                catch (Exception error) { Log("Could not put the previous game files back: " + error.Message); }
            }
            try
            {
                FileUtil.DeleteTree(versionDir);
                if (Directory.Exists(replaced)) FileUtil.MoveDirectoryInPlace(replaced, versionDir);
            }
            catch (Exception error) { Log("Could not put the previous version back: " + error.Message); }
        }

        /// <summary>Once the new version is in place: exactly one previous version stays for rollback; a folder still held is removed by a later install.</summary>
        private void RemoveOtherVersions(string previous)
        {
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
                if (running == null && replacingGameFiles) running = Running.Find(layout.Root, "NebulaRuntime", "NebulaNative");
                if (running == null) return;
                Log("Waiting for " + running + " to close.");
                if (!progress.AskToClose(NebulaText.Of("Nebula is running ({0}). Close the game, the Nebula launcher and Nebula Settings, then choose Retry to finish installing Nebula {1}.", running, BuildInfo.Version)))
                    throw new InvalidOperationException(NebulaText.Of("Nebula is running. Close the game, the Nebula launcher and Nebula Settings, then try again."));
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

        /// <summary>nebula-import.exe from this package, in this run's staging folder.</summary>
        private string ImporterPath
        {
            get
            {
                string path = Path.Combine(staging, "nebula-import.exe");
                if (!File.Exists(path)) Payload.Extract("nebula-import.exe", path);
                return path;
            }
        }

        private int RunTool(string file, IList<string> arguments, Action<string> onLine)
        {
            Log("> " + Path.GetFileName(file) + " " + ProcessRunner.JoinArguments(arguments));
            using (var runner = new ProcessRunner())
                return runner.Run(file, arguments, staging, onLine, cancel);
        }

        private string stepText = "";
        private volatile bool waitingForDlss;

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

        private void WriteLog(string line)
        {
            lock (logLock) logFile.WriteLine(DateTime.UtcNow.ToString("HH:mm:ss.fff") + " " + line);
        }

        private void Log(string line)
        {
            WriteLog(line);
            progress.Log(line);
        }

        [DllImport("kernel32.dll")]
        private static extern bool IsProcessorFeaturePresent(uint feature);
    }
}
