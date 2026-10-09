using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Nebula.Setup
{
    // Private compiled game objects stay on the user's machine. They allow
    // public arithmetic-library updates without translating or compiling Wii code.
    internal static class RelinkCache
    {
        private static readonly string[] Targets = { "game", "home_button", "dsp" };
        private static readonly string[] Libraries = { "galaxy_ppc_float.lib", "galaxy_softfloat.lib", "galaxy_dsp_alu.lib" };
        private static readonly string[] SystemLibraries = { "kernel32.lib", "user32.lib", "gdi32.lib", "winspool.lib", "shell32.lib", "ole32.lib", "oleaut32.lib", "uuid.lib", "comdlg32.lib", "advapi32.lib" };

        internal static string CheckedPath(string directory, string relative)
        {
            if (string.IsNullOrEmpty(relative) || Path.IsPathRooted(relative) || relative.IndexOf(':') >= 0 ||
                !relative.EndsWith(".obj", StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Invalid retained object path.");
            string root = Path.GetFullPath(directory).TrimEnd('\\') + "\\";
            string full = Path.GetFullPath(Path.Combine(root, relative));
            if (!full.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Invalid retained object path.");
            return full;
        }

        internal static void Capture(string build, string app, string libraries, string toolchain)
        {
            string cache = Path.Combine(app, "relink-cache");
            Directory.CreateDirectory(cache);
            var objects = new Dictionary<string, object>();
            var lines = File.ReadAllLines(Path.Combine(build, "build.ninja"));
            foreach (var target in Targets)
            {
                string subdir = target == "home_button" ? "home" : target;
                string marker = subdir + "\\RMGE01_" + target + ".dll ";
                string line = lines.Single(x => x.StartsWith("build ") && x.Replace('/', '\\').Substring(6).StartsWith(marker));
                string inputs = line.Substring(line.IndexOf(": ", StringComparison.Ordinal) + 2);
                inputs = inputs.Substring(inputs.IndexOf(' ') + 1).Split('|')[0];
                var entries = new List<object>();
                foreach (string relative in inputs.Split(new[] {' '}, StringSplitOptions.RemoveEmptyEntries))
                {
                    string from = CheckedPath(build, relative), to = CheckedPath(cache, relative);
                    if ((File.GetAttributes(from) & FileAttributes.ReparsePoint) != 0) throw new IOException("Linked object file rejected.");
                    Directory.CreateDirectory(Path.GetDirectoryName(to));
                    File.Copy(from, to);
                    entries.Add(new Dictionary<string, object> { {"path", relative}, {"sha256", FileUtil.Sha256File(to)} });
                }
                if (entries.Count == 0) throw new InvalidDataException("No compiled objects retained for " + target);
                objects[target] = entries;
            }
            var manifest = new Dictionary<string, object> { {"schema", "nebula.relink.v1"}, {"toolchain", toolchain}, {"objects", objects} };
            RecordOutputs(manifest, app, libraries);
            Json.Save(Path.Combine(cache, "manifest.json"), manifest);
        }

        private static void RecordOutputs(Dictionary<string, object> manifest, string app, string libraries)
        {
            manifest["modules"] = Targets.ToDictionary(t => "RMGE01_" + t + ".dll", t => (object)FileUtil.Sha256File(Path.Combine(app, "RMGE01_" + t + ".dll")));
            manifest["libraries"] = Libraries.ToDictionary(n => n, n => (object)FileUtil.Sha256File(Path.Combine(libraries, n)));
        }

        internal static bool Update(string current, string app, string libraries, string toolchain,
            Func<IList<string>, int> link, Action<string> log)
        {
            string oldCache = Path.Combine(current, "relink-cache");
            string manifestPath = Path.Combine(oldCache, "manifest.json");
            if (!File.Exists(manifestPath)) return false;
            var manifest = Json.Load(manifestPath);
            if (Json.Str(manifest, "schema") != "nebula.relink.v1" || Json.Str(manifest, "toolchain") != toolchain)
                throw new InvalidDataException("The retained objects require their original linker toolchain. No game source was recompiled.");
            foreach (var target in Targets)
            {
                string name = "RMGE01_" + target + ".dll";
                if (FileUtil.Sha256File(Path.Combine(current, name)) != Json.Str(Json.Obj(manifest, "modules"), name))
                    throw new InvalidDataException("Retained objects do not belong to the installed " + name);
                foreach (Dictionary<string, object> entry in Json.List(Json.Obj(manifest, "objects"), target))
                    if (FileUtil.Sha256File(CheckedPath(oldCache, Json.Str(entry, "path"))) != Json.Str(entry, "sha256"))
                        throw new InvalidDataException("A retained compiled object changed. Nothing was rebuilt.");
            }
            string cache = Path.Combine(app, "relink-cache");
            FileUtil.CopyTreeWithStreams(oldCache, cache, null);
            bool changed = Libraries.Any(n => FileUtil.Sha256File(Path.Combine(libraries, n)) != Json.Str(Json.Obj(manifest, "libraries"), n));
            if (changed)
            {
                foreach (var target in Targets)
                {
                    var response = new List<string>();
                    foreach (Dictionary<string, object> entry in Json.List(Json.Obj(manifest, "objects"), target))
                        response.Add(CheckedPath(cache, Json.Str(entry, "path")));
                    foreach (var lib in target == "dsp" ? new[] {Libraries[2]} : new[] {Libraries[0], Libraries[1]})
                        response.Add(Path.Combine(libraries, lib));
                    response.AddRange(SystemLibraries);
                    string rsp = Path.Combine(app, target + "-relink.rsp");
                    File.WriteAllLines(rsp, response.Select(ProcessRunner.Quote));
                    var args = new List<string> {"/nologo", "@" + rsp, "/dll", "/machine:x64", "/INCREMENTAL:NO", "/version:0.0",
                        "/out:" + Path.Combine(app, "RMGE01_" + target + ".dll"), "/implib:" + Path.Combine(app, target + "-relink.lib")};
                    if (target == "dsp")
                        foreach (string export in new[] {"entry", "entry_expected_iram_start_address", "entry_expected_iram_byte_len", "entry_expected_iram_sha1", "entry_generated_probe_contract_version", "entry_generated_probe_capabilities"})
                            args.Add("/EXPORT:galaxy_rmge01_dsp_" + export);
                    if (link(args) != 0) throw new InvalidOperationException("Relinking " + target + " failed. The installed version was kept.");
                    File.Delete(rsp);
                    File.Delete(Path.Combine(app, target + "-relink.lib"));
                    File.Delete(Path.Combine(app, target + "-relink.exp"));
                }
                log("Relinked preserved compiled game objects against the new public libraries; no game source compiled.");
            }
            else log("Public libraries unchanged; compiled game DLLs retained byte-for-byte.");
            RecordOutputs(manifest, app, libraries);
            Json.Save(Path.Combine(cache, "manifest.json"), manifest);
            return true;
        }
    }
}
