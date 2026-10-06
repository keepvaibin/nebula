using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading;

namespace Nebula.Setup
{
    /// <summary>
    /// The pinned build toolchain: clang-cl and lld-link (from llvm-mingw),
    /// Microsoft's C++ runtime headers and libraries and the Windows SDK,
    /// plus CMake and Ninja. Every download is checked against a pinned
    /// SHA-256 and unpacked per-user without elevation.
    /// </summary>
    internal sealed class Toolchain
    {
        private const int ParallelDownloads = 6;

        private readonly Dictionary<string, object> pins;
        private readonly string root;

        public Toolchain(Dictionary<string, object> pins, string toolchainsDirectory)
        {
            this.pins = pins;
            Id = FileUtil.Sha256Text(Json.Serialize(pins)).Substring(0, 16);
            root = Path.Combine(toolchainsDirectory, Id);
        }

        /// <summary>Identity of the pinned toolchain; part of module compatibility.</summary>
        public string Id { get; private set; }
        public string LicenseUrl { get { return Json.Str(Json.Obj(pins, "visualStudio"), "licenseUrl"); } }

        public long DownloadBytes
        {
            get
            {
                long total = Json.Long(pins, "downloadBytes");
                foreach (var name in new[] { "llvm", "cmake", "ninja" }) total += Json.Long(Json.Obj(pins, name), "size");
                return total;
            }
        }

        private string Marker { get { return Path.Combine(root, "toolchain-ready.json"); } }
        public bool IsReady { get { return File.Exists(Marker) && Locate(false); } }

        public string MsvcDir { get; private set; }
        public string SdkDir { get; private set; }
        public string SdkVersion { get; private set; }
        public string RedistDir { get; private set; }
        public string ClangCl { get { return Path.Combine(root, "llvm", "bin", "clang-cl.exe"); } }
        public string LldLink { get { return Path.Combine(root, "llvm", "bin", "lld-link.exe"); } }
        public string CMake { get { return Path.Combine(root, "cmake", "bin", "cmake.exe"); } }
        public string Ninja { get { return Path.Combine(root, "ninja", "ninja.exe"); } }
        private string Rc { get { return Path.Combine(SdkDir, "bin", SdkVersion, "x64", "rc.exe"); } }
        private string Mt { get { return Path.Combine(SdkDir, "bin", SdkVersion, "x64", "mt.exe"); } }

        private sealed class Download
        {
            public string Url, File, Sha256, Kind;
            public long Size;
            public Dictionary<string, object> Pin;
        }

        public void Ensure(Action<string, double> progress, Action<string> log, CancellationToken cancel)
        {
            if (IsReady) { log("Using the installed toolchain " + Id + "."); return; }
            if (Directory.Exists(root)) FileUtil.DeleteTree(root);
            string downloads = Path.Combine(Path.GetDirectoryName(root), "downloads");
            Directory.CreateDirectory(downloads);

            var items = new List<Download>();
            foreach (Dictionary<string, object> payload in Json.List(pins, "payloads"))
                items.Add(new Download
                {
                    Url = Json.Str(payload, "url"), Sha256 = Json.Str(payload, "sha256"), Size = Json.Long(payload, "size"),
                    Kind = Json.Str(payload, "kind"), File = Path.Combine(downloads, Path.GetFileName(Json.Str(payload, "fileName")))
                });
            foreach (var name in new[] { "llvm", "cmake", "ninja" })
            {
                var pin = Json.Obj(pins, name);
                items.Add(new Download
                {
                    Url = Json.Str(pin, "url"), Sha256 = Json.Str(pin, "sha256"), Size = Json.Long(pin, "size"),
                    Kind = name, File = Path.Combine(downloads, name + "-" + Json.Str(pin, "version") + ".zip"), Pin = pin
                });
            }

            DownloadAll(items, progress, cancel);
            log("Downloaded " + items.Count + " toolchain files.");

            string staging = root + ".partial";
            if (Directory.Exists(staging)) FileUtil.DeleteTree(staging);
            Directory.CreateDirectory(staging);
            progress("Unpacking the toolchain", -1);
            // msiexec runs one install at a time, so the SDK unpacks on its own
            // thread while the archives extract alongside it.
            Exception sdkFailure = null;
            var sdk = new Thread(delegate()
            {
                try
                {
                    foreach (var item in items.Where(i => i.Kind == "msi"))
                    {
                        cancel.ThrowIfCancellationRequested();
                        InstallMsi(item.File, Path.Combine(staging, "sdk"), log, cancel);
                    }
                }
                catch (Exception error) { sdkFailure = error; }
            });
            sdk.Start();
            foreach (var item in items)
            {
                cancel.ThrowIfCancellationRequested();
                if (item.Kind == "vsix") ExtractVsix(item.File, staging);
                else if (item.Kind == "llvm") ExtractSelected(item.File, Path.Combine(staging, "llvm"), Json.Obj(item.Pin, "extract"));
                else if (item.Kind == "cmake") ExtractZipStripTop(item.File, Path.Combine(staging, "cmake"));
                else if (item.Kind == "ninja") ZipFile.ExtractToDirectory(item.File, Path.Combine(staging, "ninja"));
            }
            sdk.Join();
            if (sdkFailure != null) throw sdkFailure;
            foreach (var msi in Directory.GetFiles(Path.Combine(staging, "sdk"), "*.msi")) File.Delete(msi);

            Directory.Move(staging, root);
            if (!Locate(true)) throw new InvalidOperationException("The unpacked toolchain is incomplete.");
            Json.Save(Marker, new Dictionary<string, object> { { "id", Id }, { "sdk", SdkVersion }, { "createdUtc", DateTime.UtcNow.ToString("o") } });
            FileUtil.DeleteTree(downloads);
            // Toolchains of earlier versions are never used again.
            foreach (var dir in Directory.GetDirectories(Path.GetDirectoryName(root)))
                if (!string.Equals(dir, root, StringComparison.OrdinalIgnoreCase))
                    try { FileUtil.DeleteTree(dir); } catch (IOException) { } catch (UnauthorizedAccessException) { }
            log("Toolchain " + Id + " is ready.");
        }

        private static void DownloadAll(List<Download> items, Action<string, double> progress, CancellationToken cancel)
        {
            long total = items.Sum(i => i.Size);
            var received = new long[items.Count];
            int next = -1;
            Exception failure = null;
            var workers = new List<Thread>();
            for (int w = 0; w < ParallelDownloads; w++)
            {
                var worker = new Thread(delegate()
                {
                    try
                    {
                        for (int index; (index = Interlocked.Increment(ref next)) < items.Count && failure == null; )
                        {
                            int slot = index;
                            var item = items[slot];
                            Downloader.Fetch(item.Url, item.File, item.Sha256, item.Size, delegate(long bytes)
                            {
                                Interlocked.Exchange(ref received[slot], bytes);
                                progress("Downloading the compiler toolchain", received.Sum() / (double)Math.Max(1, total));
                            }, cancel);
                        }
                    }
                    catch (Exception error) { Interlocked.CompareExchange(ref failure, error, null); }
                });
                worker.Start();
                workers.Add(worker);
            }
            foreach (var worker in workers) worker.Join();
            if (failure != null) throw failure;
        }

        /// <summary>VSIX packages are zip files; their payload is under Contents/.</summary>
        private static void ExtractVsix(string vsix, string target)
        {
            using (var archive = ZipFile.OpenRead(vsix))
            {
                foreach (var entry in archive.Entries)
                {
                    string name = Uri.UnescapeDataString(entry.FullName.Replace('\\', '/'));
                    if (!name.StartsWith("Contents/", StringComparison.OrdinalIgnoreCase) || name.EndsWith("/")) continue;
                    string path = SafeJoin(target, name.Substring("Contents/".Length));
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    entry.ExtractToFile(path, true);
                }
            }
        }

        /// <summary>
        /// Extract only the pinned entries of a release zip (its top folder
        /// removed). A key ending in "/" maps a whole folder.
        /// </summary>
        private static void ExtractSelected(string zip, string target, Dictionary<string, object> map)
        {
            int found = 0;
            using (var archive = ZipFile.OpenRead(zip))
            {
                foreach (var entry in archive.Entries)
                {
                    string name = entry.FullName.Replace('\\', '/');
                    int slash = name.IndexOf('/');
                    if (slash < 0 || name.EndsWith("/")) continue;
                    string relative = name.Substring(slash + 1);
                    string mapped = null;
                    foreach (var pair in map)
                    {
                        string to = (string)pair.Value;
                        if (pair.Key.EndsWith("/") ? relative.StartsWith(pair.Key, StringComparison.Ordinal) : relative == pair.Key)
                            mapped = to + relative.Substring(pair.Key.Length);
                    }
                    if (mapped == null) continue;
                    string path = SafeJoin(target, mapped);
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    entry.ExtractToFile(path, true);
                    found++;
                }
            }
            if (found < map.Count) throw new InvalidDataException(Path.GetFileName(zip) + " is missing pinned files.");
        }

        /// <summary>An administrative MSI install only unpacks files; it needs no elevation.</summary>
        private static void InstallMsi(string msi, string target, Action<string> log, CancellationToken cancel)
        {
            Directory.CreateDirectory(target);
            using (var runner = new ProcessRunner())
            {
                string system = Environment.GetFolderPath(Environment.SpecialFolder.System);
                // msiexec parses its own command line: PROPERTY="value" quoting.
                int code = runner.RunCommandLine(Path.Combine(system, "msiexec.exe"),
                    "/a \"" + msi + "\" /qn TARGETDIR=\"" + target + "\"",
                    Path.GetDirectoryName(msi), null, log, cancel);
                if (code != 0) throw new InvalidOperationException("Unpacking " + Path.GetFileName(msi) + " failed with code " + code + ".");
            }
        }

        private static void ExtractZipStripTop(string zip, string target)
        {
            using (var archive = ZipFile.OpenRead(zip))
            {
                foreach (var entry in archive.Entries)
                {
                    string name = entry.FullName.Replace('\\', '/');
                    int slash = name.IndexOf('/');
                    if (slash < 0 || name.EndsWith("/")) continue;
                    string path = SafeJoin(target, name.Substring(slash + 1));
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    entry.ExtractToFile(path, true);
                }
            }
        }

        public static string SafeJoin(string root, string relative)
        {
            string full = Path.GetFullPath(Path.Combine(root, relative));
            string prefix = Path.GetFullPath(root).TrimEnd('\\') + "\\";
            if (!full.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Archive entry escapes its folder: " + relative);
            return full;
        }

        private bool Locate(bool strict)
        {
            string msvcRoot = Path.Combine(root, "VC", "Tools", "MSVC");
            string sdkRoot = Path.Combine(root, "sdk", "Windows Kits", "10");
            if (!Directory.Exists(msvcRoot) || !Directory.Exists(Path.Combine(sdkRoot, "Include"))) return false;
            MsvcDir = Directory.GetDirectories(msvcRoot).OrderBy(d => d).LastOrDefault();
            string include = Directory.GetDirectories(Path.Combine(sdkRoot, "Include")).OrderBy(d => d).LastOrDefault();
            if (MsvcDir == null || include == null) return false;
            SdkDir = sdkRoot;
            SdkVersion = Path.GetFileName(include);
            string redist = Path.Combine(root, "VC", "Redist", "MSVC");
            RedistDir = Directory.Exists(redist)
                ? Directory.GetDirectories(redist).Where(d => File.Exists(Path.Combine(d, "x64", "Microsoft.VC143.CRT", "vcruntime140.dll")))
                    .Select(d => Path.Combine(d, "x64", "Microsoft.VC143.CRT")).FirstOrDefault()
                : null;
            string[] required =
            {
                ClangCl, LldLink, Rc, Mt, CMake, Ninja,
                Path.Combine(MsvcDir, "include", "vcruntime.h"),
                Path.Combine(MsvcDir, "lib", "x64", "msvcrt.lib"),
                Path.Combine(SdkDir, "Lib", SdkVersion, "um", "x64", "kernel32.lib"),
                Path.Combine(SdkDir, "Lib", SdkVersion, "ucrt", "x64", "ucrt.lib")
            };
            foreach (var path in required)
            {
                if (!File.Exists(path))
                {
                    if (strict) throw new FileNotFoundException("The toolchain is missing " + path);
                    return false;
                }
            }
            if (RedistDir == null)
            {
                if (strict) throw new FileNotFoundException("The toolchain has no C++ runtime redistributable files.");
                return false;
            }
            return true;
        }

        /// <summary>
        /// A complete, isolated build environment. Nothing from an installed
        /// Visual Studio or the user's PATH is inherited.
        /// </summary>
        public Dictionary<string, string> BuildEnvironment()
        {
            Locate(true);
            var env = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (string name in new[] { "SystemRoot", "windir", "SystemDrive", "TEMP", "TMP", "USERPROFILE",
                "LOCALAPPDATA", "APPDATA", "COMSPEC", "PATHEXT", "OS", "NUMBER_OF_PROCESSORS",
                "PROCESSOR_ARCHITECTURE", "PROCESSOR_IDENTIFIER", "USERNAME", "HOMEDRIVE", "HOMEPATH" })
            {
                string value = Environment.GetEnvironmentVariable(name);
                if (value != null) env[name] = value;
            }
            string system = Environment.GetFolderPath(Environment.SpecialFolder.System);
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            env["PATH"] = string.Join(";", new[]
            {
                Path.GetDirectoryName(ClangCl), Path.Combine(SdkDir, "bin", SdkVersion, "x64"),
                Path.GetDirectoryName(CMake), Path.GetDirectoryName(Ninja),
                system, windows, Path.Combine(system, "Wbem")
            });
            string inc = Path.Combine(SdkDir, "Include", SdkVersion);
            env["INCLUDE"] = string.Join(";", new[]
            {
                Path.Combine(MsvcDir, "include"), Path.Combine(inc, "ucrt"), Path.Combine(inc, "shared"), Path.Combine(inc, "um")
            });
            string lib = Path.Combine(SdkDir, "Lib", SdkVersion);
            env["LIB"] = string.Join(";", new[]
            {
                Path.Combine(MsvcDir, "lib", "x64"), Path.Combine(lib, "ucrt", "x64"), Path.Combine(lib, "um", "x64")
            });
            return env;
        }

        /// <summary>CMake cache arguments that pin every tool to this toolchain.</summary>
        public List<string> CMakeToolArguments()
        {
            return new List<string>
            {
                "-DCMAKE_CXX_COMPILER=" + ClangCl.Replace('\\', '/'),
                "-DCMAKE_LINKER=" + LldLink.Replace('\\', '/'),
                "-DCMAKE_RC_COMPILER=" + Rc.Replace('\\', '/'),
                "-DCMAKE_MT=" + Mt.Replace('\\', '/'),
                "-DCMAKE_MAKE_PROGRAM=" + Ninja.Replace('\\', '/')
            };
        }

        /// <summary>App-local C++ runtime DLLs for the runtime and modules.</summary>
        public IEnumerable<string> RuntimeDlls()
        {
            Locate(true);
            return Directory.GetFiles(RedistDir, "*.dll");
        }
    }
}
