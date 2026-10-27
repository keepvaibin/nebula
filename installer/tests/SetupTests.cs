using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Text;
using System.Threading;

namespace Nebula.Setup
{
    // Setup without a game: where it gets the exact Nebula source (--source, the copy embedded in it, the download),
    // and folder moves while another program holds a file. Run by run-launcher-tests.ps1 with a scratch folder.
    // Downloads come from a local HttpListener on http.sys's Temporary_Listen_Addresses, which needs no administrator.
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

        static SourceTree NewTree()
        {
            var files = new Dictionary<string, object>();
            foreach (var pair in Files) files[pair.Key] = FileUtil.Sha256Text(pair.Value);
            return new SourceTree(new Dictionary<string, object>
            {
                { "commit", Commit }, { "repository", "keepvaibin/nebula" }, { "version", "9.9.9" }, { "files", files },
            });
        }

        // <paramref name="embedded"/>: the zip a release setup carries; null for a setup compiled by hand.
        // Without <paramref name="url"/> any download fails at once (nothing listens on port 9).
        static SourceTree Tree(byte[] embedded, string url = "http://127.0.0.1:9/never")
        {
            SourceTree tree = NewTree();
            tree.EmbeddedArchive = delegate { return embedded == null ? null : new MemoryStream(embedded, false); };
            tree.DownloadUrl = url;
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
                Directory.CreateDirectory(scratch);
                Check(SourceTree.EmbeddedArchiveName == "nebula-source.zip" &&
                      NewTree().DownloadUrl == "https://codeload.github.com/keepvaibin/nebula/zip/" + Commit,
                      "the embedded source has the resource name build-release.ps1 gives it, and the download is GitHub's archive of the commit");

                // A release setup carries its source and needs no download (any would fail here).
                var log = new List<string>();
                string installed = Ensure(Tree(Zip(null)), Path.Combine(scratch, "sources-a"), null, log);
                Check(Installed(installed) && Logged(log, "Using the source embedded in Setup") && !Logged(log, "Downloading") &&
                      Directory.GetFiles(Path.Combine(scratch, "sources-a")).Length == 0,
                      "the source embedded in Setup installs without a download and leaves no file behind");
                log.Clear();
                Check(Ensure(Tree(null), Path.Combine(scratch, "sources-a"), null, log) == installed && Logged(log, "Using the verified source"),
                      "a source verified earlier is reused");

                // --source is used or rejected, never replaced by another source.
                string changed = Path.Combine(scratch, "changed.zip");
                File.WriteAllBytes(changed, Zip("CMakeLists.txt"));
                Expect<InvalidDataException>(() => Ensure(Tree(Zip(null)), Path.Combine(scratch, "sources-b"), changed, log),
                    e => true, "an explicit --source that does not match fails instead of using another source");
                Check(Installed(Ensure(Tree(null), Path.Combine(scratch, "sources-c"), installed, log)), "--source takes a source folder");

                DownloadCases(scratch);
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
                    e => e.Message.EndsWith(FileUtil.HeldAdvice) && Holds(installed, "old") && !Directory.Exists(Path.Combine(root, "aside")),
                    "an installed tree with a held file is left whole, and the failure says what to close");
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

        // A setup without a matching embedded source downloads it. The 1.0.1 test report: GitHub answered 404
        // for an unpushed commit, and Setup retried it, then showed only "(404) Not Found".
        static void DownloadCases(string scratch)
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
                Check(Installed(Ensure(Tree(null, url + "good.zip"), Path.Combine(scratch, "sources-d"), null, log)) &&
                      requests["good.zip"] == 1 && Directory.GetFiles(Path.Combine(scratch, "sources-d")).Length == 0,
                      "a setup without an embedded source downloads it and keeps no zip");
                log.Clear();
                Check(Installed(Ensure(Tree(Zip("CMakeLists.txt"), url + "good.zip"), Path.Combine(scratch, "sources-e"), null, log)) &&
                      Logged(log, "does not match this release") && requests["good.zip"] == 2,
                      "an embedded source that does not match is replaced by the download");
                log.Clear();
                Expect<InvalidOperationException>(
                    () => Ensure(Tree(null, url + "missing.zip"), Path.Combine(scratch, "sources-f"), null, log),
                    e => e.Message.Contains("9.9.9") && e.Message.Contains("404") && e.Message.Contains("Check the internet connection") &&
                         requests["missing.zip"] == 1,
                    "a 404 is asked for once, and the failure names the version and what to do");
            }
            finally { listener.Close(); }
        }
    }
}
