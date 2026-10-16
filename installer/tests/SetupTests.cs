using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Text;
using System.Threading;

namespace Nebula.Setup
{
    // Setup without a game: where it gets the exact Nebula source (--source, the zip beside Nebula-Setup.exe, then each
    // download address in turn), and folder moves while another program holds a file. Run by run-launcher-tests.ps1
    // with a scratch folder. Downloads come from a local HttpListener on http.sys's Temporary_Listen_Addresses, which
    // needs no administrator.
    internal static class SetupTests
    {
        const string Commit = "0123456789abcdef0123456789abcdef01234567";
        static readonly Dictionary<string, string> Files = new Dictionary<string, string>
        {
            { "CMakeLists.txt", "project(nebula_test)\n" },
            { "runtime/include/galaxy/test.h", "#pragma once\n" },
        };

        static void Check(bool ok, string what)
        {
            if (!ok) throw new Exception("FAIL: " + what);
            Console.WriteLine("PASS: " + what);
        }

        static SourceTree Tree(string setupDirectory, params string[] urls)
        {
            var files = new Dictionary<string, object>();
            foreach (var pair in Files) files[pair.Key] = FileUtil.Sha256Text(pair.Value);
            var tree = new SourceTree(new Dictionary<string, object>
            {
                { "commit", Commit }, { "repository", "keepvaibin/nebula" }, { "version", "9.9.9" }, { "files", files },
            });
            tree.SetupDirectory = setupDirectory;
            tree.DownloadUrls = urls;
            return tree;
        }

        static byte[] Zip(string changedFile)
        {
            using (var memory = new MemoryStream())
            {
                using (var archive = new ZipArchive(memory, ZipArchiveMode.Create, true))
                {
                    foreach (var pair in Files)
                    {
                        var entry = archive.CreateEntry("nebula-" + Commit + "/" + pair.Key);
                        byte[] bytes = Encoding.UTF8.GetBytes(pair.Key == changedFile ? pair.Value + "// changed\n" : pair.Value);
                        using (var stream = entry.Open()) stream.Write(bytes, 0, bytes.Length);
                    }
                }
                return memory.ToArray();
            }
        }

        static string Ensure(SourceTree tree, string sources, string overridePath, List<string> log)
        {
            return tree.Ensure(sources, overridePath, delegate(string text, double fraction) { }, log.Add, CancellationToken.None);
        }

        static bool Logged(List<string> log, string text) { return log.Exists(line => line.Contains(text)); }

        static bool Installed(string folder)
        {
            foreach (var pair in Files)
            {
                string path = Path.Combine(folder, pair.Key.Replace('/', '\\'));
                if (!File.Exists(path) || File.ReadAllText(path) != pair.Value) return false;
            }
            return true;
        }

        static void Expect<T>(Action action, Func<T, bool> matches, string what) where T : Exception
        {
            try { action(); }
            catch (T error) { Check(matches(error), what); return; }
            throw new Exception("FAIL: " + what + " (no " + typeof(T).Name + ")");
        }

        public static int Main(string[] args)
        {
            try
            {
                string scratch = Path.Combine(args[0], "source-tree");
                string setupDir = Path.Combine(scratch, "setup");
                string emptyDir = Path.Combine(scratch, "empty");
                Directory.CreateDirectory(setupDir);
                Directory.CreateDirectory(emptyDir);
                var probe = Tree(setupDir);
                Check(probe.ArchiveName == "nebula-source-0123456789ab.zip" &&
                      probe.ReleaseArchiveUrl == "https://github.com/keepvaibin/nebula/releases/download/v9.9.9/nebula-source-0123456789ab.zip" &&
                      new SourceTree(new Dictionary<string, object> { { "commit", Commit }, { "version", "9.9.9" }, { "repository", "keepvaibin/nebula" },
                          { "files", new Dictionary<string, object> { { "a", "b" } } } }).DownloadUrls[0] == probe.ReleaseArchiveUrl,
                      "the source zip is named as build-release.ps1 names it, and the release's copy is tried first");

                // A setup built before a push: the zip beside it is the source.
                File.WriteAllBytes(Path.Combine(setupDir, probe.ArchiveName), Zip(null));
                var log = new List<string>();
                string installed = Ensure(Tree(setupDir), Path.Combine(scratch, "sources-a"), null, log);
                Check(Installed(installed) && Logged(log, "Using the source archive"), "the source zip beside Setup is used without a download");
                log.Clear();
                Check(Ensure(Tree(setupDir), Path.Combine(scratch, "sources-a"), null, log) == installed && Logged(log, "Using the verified source"),
                      "a verified source is reused");

                // A zip beside Setup that does not match is not used, and with nowhere to download from the
                // message says what to do.
                File.WriteAllBytes(Path.Combine(setupDir, probe.ArchiveName), Zip("CMakeLists.txt"));
                log.Clear();
                string sourcesB = Path.Combine(scratch, "sources-b");
                Expect<InvalidOperationException>(() => Ensure(Tree(setupDir), sourcesB, null, log),
                    e => e.Message.Contains("9.9.9") && e.Message.Contains(probe.ArchiveName) && Logged(log, "is not this release's source") &&
                         !Directory.Exists(Path.Combine(sourcesB, "0123456789ab")),
                    "a changed zip beside Setup is rejected, and the failure names the zip to download");
                Expect<InvalidDataException>(() => Ensure(Tree(emptyDir), sourcesB, Path.Combine(setupDir, probe.ArchiveName), log),
                    e => true, "an explicit --source that does not match fails instead of downloading");
                Check(Installed(Ensure(Tree(emptyDir), Path.Combine(scratch, "sources-c"), installed, log)), "--source takes a source folder");

                DownloadCases(scratch, emptyDir);
                MoveCases(Path.Combine(args[0], "moves"));
                return 0;
            }
            catch (Exception error)
            {
                Console.WriteLine(error.Message.StartsWith("FAIL:") ? error.Message : "FAIL: " + error);
                return 1;
            }
        }

        static string MakeTree(string root, string text)
        {
            Directory.CreateDirectory(Path.Combine(root, "sub"));
            File.WriteAllText(Path.Combine(root, "a.dll"), text);
            File.WriteAllText(Path.Combine(root, "sub", "b.txt"), text);
            return root;
        }

        static bool Holds(string root, string text)
        {
            return File.Exists(Path.Combine(root, "a.dll")) && File.ReadAllText(Path.Combine(root, "a.dll")) == text &&
                   File.ReadAllText(Path.Combine(root, "sub", "b.txt")) == text;
        }

        // Issue #4: a program reading a fresh file (a virus scanner) refused the move of a new version into place. A
        // tree Setup built is copied instead; a tree that is installed already is only ever renamed, so a held file
        // leaves it whole; and a swap whose new tree cannot move in puts the old one back.
        static void MoveCases(string root)
        {
            var log = new List<string>();
            string built = MakeTree(Path.Combine(root, "built"), "new");
            string target = Path.Combine(root, "versions", "1.0.1");
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            using (new FileStream(Path.Combine(built, "a.dll"), FileMode.Open, FileAccess.Read, FileShare.Read))
                FileUtil.MoveDirectory(built, target, log.Add);
            Check(Holds(target, "new") && Logged(log, "copying it instead"), "a built tree with a file held for reading is copied into place");

            string installed = MakeTree(Path.Combine(root, "installed"), "old");
            using (new FileStream(Path.Combine(installed, "sub", "b.txt"), FileMode.Open, FileAccess.Read, FileShare.Read))
                Expect<IOException>(() => FileUtil.MoveDirectoryInPlace(installed, Path.Combine(root, "aside")),
                    e => e.Message.Contains("In use by") && Holds(installed, "old") && !Directory.Exists(Path.Combine(root, "aside")),
                    "an installed tree with a held file is left whole, and the failure names the program holding it");
            Expect<IOException>(() => FileUtil.MoveDirectory(MakeTree(Path.Combine(root, "other"), "x"), installed, log.Add),
                e => e.Message.Contains("already exists") && Holds(installed, "old"), "a tree is never moved into an existing folder");

            log.Clear();
            string fresh = MakeTree(Path.Combine(root, "fresh"), "new");
            using (new FileStream(Path.Combine(fresh, "a.dll"), FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                Expect<IOException>(() => InstallEngine.Swap(fresh, installed, Path.Combine(root, "installed-old"), log.Add),
                    e => Holds(installed, "old") && !Directory.Exists(Path.Combine(root, "installed-old")),
                    "a swap whose new tree cannot be moved in puts the installed tree back");
            InstallEngine.Swap(fresh, installed, Path.Combine(root, "installed-old"), log.Add);
            Check(Holds(installed, "new") && Holds(Path.Combine(root, "installed-old"), "old"), "a swap keeps the replaced tree aside");
        }

        // The 1.0.1 test report: GitHub answered 404 for the commit, Setup retried it and then showed only
        // "(404) Not Found". A missing file is asked for once, the next address is tried, and the last
        // failure names the source zip.
        static void DownloadCases(string scratch, string emptyDir)
        {
            WebRequest.DefaultWebProxy = null;
            string path = "/Temporary_Listen_Addresses/nebula-" + Guid.NewGuid().ToString("N") + "/";
            var listener = new HttpListener();
            listener.Prefixes.Add("http://+:80" + path);
            try { listener.Start(); }
            catch (HttpListenerException error)
            {
                Console.WriteLine("SKIP: download fallback cases (no local listener: " + error.Message + ")");
                return;
            }
            var requests = new Dictionary<string, int>();
            byte[] good = Zip(null);
            var server = new Thread(delegate()
            {
                while (listener.IsListening)
                {
                    HttpListenerContext context;
                    try { context = listener.GetContext(); }
                    catch (HttpListenerException) { return; }
                    catch (ObjectDisposedException) { return; }
                    string name = context.Request.Url.AbsolutePath.Substring(path.Length);
                    lock (requests) requests[name] = (requests.ContainsKey(name) ? requests[name] : 0) + 1;
                    if (name == "good.zip")
                    {
                        context.Response.ContentLength64 = good.Length;
                        context.Response.OutputStream.Write(good, 0, good.Length);
                    }
                    else context.Response.StatusCode = 404;
                    context.Response.Close();
                }
            });
            server.IsBackground = true;
            server.Start();
            try
            {
                string url = "http://127.0.0.1:80" + path;
                var log = new List<string>();
                string installed = Ensure(Tree(emptyDir, url + "missing.zip", url + "good.zip"), Path.Combine(scratch, "sources-d"), null, log);
                Check(Installed(installed) && requests["missing.zip"] == 1 && requests["good.zip"] == 1,
                      "a 404 is asked for once and the next address supplies the source");
                log.Clear();
                Expect<InvalidOperationException>(
                    () => Ensure(Tree(emptyDir, url + "missing.zip", url + "gone.zip"), Path.Combine(scratch, "sources-e"), null, log),
                    e => e.Message.Contains("404") && e.Message.Contains("nebula-source-0123456789ab.zip") && requests["missing.zip"] == 2 &&
                         requests["gone.zip"] == 1,
                    "when every address answers 404 Setup stops at once and says to put the source zip beside it");
            }
            finally { listener.Close(); }
        }
    }
}
