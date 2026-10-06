using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Threading;

namespace Nebula.Setup
{
    /// <summary>
    /// The pinned compiler toolchain: Microsoft's MSVC and Windows SDK packages
    /// plus CMake and Ninja, downloaded from their official hosts, verified
    /// against pinned SHA-256 values and unpacked per-user without elevation.
    /// </summary>
    internal sealed class Toolchain
    {
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
        public string Root { get { return root; } }
        public string MsvcVersion { get { return Json.Str(pins, "msvcVersion"); } }
        public string LicenseUrl { get { return Json.Str(Json.Obj(pins, "visualStudio"), "licenseUrl"); } }

        public long DownloadBytes
        {
            get
            {
                return Json.Long(pins, "downloadBytes") + Json.Long(Json.Obj(pins, "cmake"), "size")
                    + 48L * 1024 * 1024;
            }
        }

        private string Marker { get { return Path.Combine(root, "toolchain-ready.json"); } }
        public bool IsReady { get { return File.Exists(Marker) && Locate(false); } }

        public string MsvcDir { get; private set; }
        public string SdkDir { get; private set; }
        public string SdkVersion { get; private set; }
        public string CMake { get { return Path.Combine(root, "cmake", "bin", "cmake.exe"); } }
        public string Ninja { get { return Path.Combine(root, "ninja", "ninja.exe"); } }
        public string RedistDir { get; private set; }

        public void Ensure(Action<string, double> progress, Action<string> log, CancellationToken cancel)
        {
            if (IsReady) { log("Using the installed toolchain " + Id + "."); return; }
            if (Directory.Exists(root)) FileUtil.DeleteTree(root);
            string downloads = Path.Combine(Path.GetDirectoryName(root), "downloads");
            Directory.CreateDirectory(downloads);

            var payloads = new List<Dictionary<string, object>>();
            foreach (var item in Json.List(pins, "payloads")) payloads.Add((Dictionary<string, object>)item);
            var cmake = Json.Obj(pins, "cmake");
            var ninja = Json.Obj(pins, "ninja");
            long total = 0, done = 0;
            foreach (var payload in payloads) total += Json.Long(payload, "size");

            foreach (var payload in payloads)
            {
                cancel.ThrowIfCancellationRequested();
                string file = Path.Combine(downloads, Path.GetFileName(Json.Str(payload, "fileName")));
                long before = done;
                Downloader.Fetch(Json.Str(payload, "url"), file, Json.Str(payload, "sha256"), Json.Long(payload, "size"),
                    delegate(long bytes) { progress("Downloading the Microsoft C++ compiler and Windows SDK", (before + bytes) / (double)Math.Max(1, total)); },
                    cancel);
                done = before + Json.Long(payload, "size");
            }
            string cmakeZip = Path.Combine(downloads, "cmake-" + Json.Str(cmake, "version") + ".zip");
            Downloader.Fetch(Json.Str(cmake, "url"), cmakeZip, Json.Str(cmake, "sha256"), 0,
                delegate(long bytes) { progress("Downloading CMake", 0); }, cancel);
            string ninjaZip = Path.Combine(downloads, "ninja-" + Json.Str(ninja, "version") + ".zip");
            Downloader.Fetch(Json.Str(ninja, "url"), ninjaZip, Json.Str(ninja, "sha256"), 0, null, cancel);

            string staging = root + ".partial";
            if (Directory.Exists(staging)) FileUtil.DeleteTree(staging);
            Directory.CreateDirectory(staging);
            int index = 0;
            foreach (var payload in payloads)
            {
                cancel.ThrowIfCancellationRequested();
                index++;
                progress("Unpacking the compiler toolchain", index / (double)(payloads.Count + 2));
                string file = Path.Combine(downloads, Path.GetFileName(Json.Str(payload, "fileName")));
                string kind = Json.Str(payload, "kind");
                if (kind == "vsix") ExtractVsix(file, staging);
                else if (kind == "msi") InstallMsi(file, Path.Combine(staging, "sdk"), log, cancel);
            }
            ExtractZipStripTop(cmakeZip, Path.Combine(staging, "cmake"));
            ZipFile.ExtractToDirectory(ninjaZip, Path.Combine(staging, "ninja"));
            foreach (var msi in Directory.GetFiles(Path.Combine(staging, "sdk"), "*.msi")) File.Delete(msi);

            Directory.Move(staging, root);
            if (!Locate(true)) throw new InvalidOperationException("The unpacked toolchain is incomplete.");
            Json.Save(Marker, new Dictionary<string, object> { { "id", Id }, { "msvc", MsvcVersion }, { "sdk", SdkVersion }, { "createdUtc", DateTime.UtcNow.ToString("o") } });
            FileUtil.DeleteTree(downloads);
            log("Toolchain " + Id + " is ready.");
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
                    string relative = name.Substring("Contents/".Length);
                    string path = SafeJoin(target, relative);
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    entry.ExtractToFile(path, true);
                }
            }
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
                Path.Combine(MsvcDir, "bin", "Hostx64", "x64", "cl.exe"),
                Path.Combine(MsvcDir, "bin", "Hostx64", "x64", "link.exe"),
                Path.Combine(SdkDir, "bin", SdkVersion, "x64", "rc.exe"),
                Path.Combine(SdkDir, "bin", SdkVersion, "x64", "mt.exe"),
                Path.Combine(SdkDir, "Lib", SdkVersion, "um", "x64", "d3d12.lib"),
                Path.Combine(SdkDir, "Lib", SdkVersion, "ucrt", "x64", "ucrt.lib"),
                CMake, Ninja
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

        public string Cl { get { return Path.Combine(MsvcDir, "bin", "Hostx64", "x64", "cl.exe"); } }
        public string Rc { get { return Path.Combine(SdkDir, "bin", SdkVersion, "x64", "rc.exe"); } }
        public string Mt { get { return Path.Combine(SdkDir, "bin", SdkVersion, "x64", "mt.exe"); } }

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
            string sdkBin = Path.Combine(SdkDir, "bin", SdkVersion, "x64");
            env["PATH"] = string.Join(";", new[]
            {
                Path.Combine(MsvcDir, "bin", "Hostx64", "x64"), sdkBin,
                Path.GetDirectoryName(CMake), Path.GetDirectoryName(Ninja),
                system, windows, Path.Combine(system, "Wbem")
            });
            string inc = Path.Combine(SdkDir, "Include", SdkVersion);
            env["INCLUDE"] = string.Join(";", new[]
            {
                Path.Combine(MsvcDir, "include"), Path.Combine(inc, "ucrt"), Path.Combine(inc, "shared"),
                Path.Combine(inc, "um"), Path.Combine(inc, "winrt"), Path.Combine(inc, "cppwinrt")
            });
            string lib = Path.Combine(SdkDir, "Lib", SdkVersion);
            env["LIB"] = string.Join(";", new[]
            {
                Path.Combine(MsvcDir, "lib", "x64"), Path.Combine(lib, "ucrt", "x64"), Path.Combine(lib, "um", "x64")
            });
            env["LIBPATH"] = Path.Combine(MsvcDir, "lib", "x64");
            return env;
        }

        /// <summary>CMake cache arguments that pin every tool to this toolchain.</summary>
        public List<string> CMakeToolArguments()
        {
            return new List<string>
            {
                "-DCMAKE_C_COMPILER=" + Cl.Replace('\\', '/'),
                "-DCMAKE_CXX_COMPILER=" + Cl.Replace('\\', '/'),
                "-DCMAKE_RC_COMPILER=" + Rc.Replace('\\', '/'),
                "-DCMAKE_MT=" + Mt.Replace('\\', '/'),
                "-DCMAKE_MAKE_PROGRAM=" + Ninja.Replace('\\', '/')
            };
        }

        /// <summary>App-local C++ runtime DLLs for the locally built binaries.</summary>
        public IEnumerable<string> RuntimeDlls()
        {
            Locate(true);
            return Directory.GetFiles(RedistDir, "*.dll");
        }
    }
}
