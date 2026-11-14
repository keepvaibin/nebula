using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;

namespace Nebula.Setup
{
    /// <summary>
    /// The update path on a local release server (a stand-in for GitHub's API and assets, with the failures a network
    /// and a release can have), signed with a key made for the run: the check and its notes, offline and slow
    /// answers, the download and its signature, corrupt, truncated, cancelled and missing downloads, a release
    /// published again, then switching to the previous version and back on an install of the test's own. Nothing
    /// touches the player's install, saves or settings (%LOCALAPPDATA% and every folder are the test's).
    /// </summary>
    internal static class UpdateFlowTests
    {
        static int checks;
        static string scratch;
        static RSACryptoServiceProvider key;

        // What the server answers, set per case.
        static volatile string releasesJson = "[]";
        static volatile byte[] setupBytes = new byte[0];
        static volatile string signatureText = "";
        static volatile string setupMode = "ok";      // ok, truncated, slow, missing
        static volatile int releasesDelayMs;
        static int setupRequests;
        static int releasesRequests;
        static int port;
        // Setup stand-ins built by the run script, each carrying a product version (setup-<version>.exe).
        static string stubs;

        static void Check(bool condition, string what)
        {
            if (!condition) throw new Exception("FAILED: " + what);
            checks++;
            Console.WriteLine("PASS: " + what);
        }

        static T Throws<T>(Action action, string what) where T : Exception
        {
            try { action(); }
            catch (T error) { checks++; Console.WriteLine("PASS: " + what + " (" + error.GetType().Name + ")"); return error; }
            catch (Exception error) { throw new Exception("FAILED: " + what + ": " + error.GetType().Name + ": " + error.Message); }
            throw new Exception("FAILED: " + what + ": nothing was thrown");
        }

        static string Sign(byte[] data)
        {
            return Convert.ToBase64String(key.SignData(data, CryptoConfig.MapNameToOID("SHA256")));
        }

        static string Release(string version, string notes)
        {
            string asset = Updater.IsBetaVersion(version) ? "Nebula-Beta-Setup.exe" : "Nebula-Setup.exe";
            var assets = new List<object>
            {
                new Dictionary<string, object> { { "name", asset }, { "size", setupBytes.Length }, { "browser_download_url", "http://127.0.0.1:" + port + "/setup" } },
                new Dictionary<string, object> { { "name", asset + ".sig" }, { "browser_download_url", "http://127.0.0.1:" + port + "/setup.sig" } }
            };
            var release = new Dictionary<string, object>
            {
                { "tag_name", "v" + version }, { "draft", false }, { "prerelease", true }, { "body", notes }, { "assets", assets }
            };
            return new JavaScriptSerializer().Serialize(new object[] { release });
        }

        // A one-request-a-connection HTTP/1.1 server on 127.0.0.1 (no URL reservation needed, unlike HttpListener).
        static void Serve(TcpListener listener)
        {
            while (true)
            {
                TcpClient client;
                try { client = listener.AcceptTcpClient(); }
                catch (SocketException) { return; }
                new Thread(() => Answer(client)) { IsBackground = true }.Start();
            }
        }

        static void Answer(TcpClient client)
        {
            using (client)
            using (var stream = client.GetStream())
            {
                try
                {
                    var reader = new StreamReader(stream, Encoding.ASCII);
                    string requestLine = reader.ReadLine() ?? "";
                    long rangeFrom = 0;
                    string line;
                    while (!string.IsNullOrEmpty(line = reader.ReadLine()))
                    {
                        if (line.StartsWith("Range: bytes=", StringComparison.OrdinalIgnoreCase))
                            long.TryParse(line.Substring(13).TrimEnd('-'), out rangeFrom);
                    }
                    string path = requestLine.Split(' ').Length > 1 ? requestLine.Split(' ')[1] : "/";
                    if (path.StartsWith("/releases"))
                    {
                        Interlocked.Increment(ref releasesRequests);
                        if (releasesDelayMs > 0) Thread.Sleep(releasesDelayMs);
                        Reply(stream, 200, Encoding.UTF8.GetBytes(releasesJson), 0, -1);
                    }
                    else if (path == "/setup.sig")
                    {
                        if (signatureText == null) Reply(stream, 404, new byte[0], 0, -1);
                        else Reply(stream, 200, Encoding.ASCII.GetBytes(signatureText), 0, -1);
                    }
                    else if (path == "/setup")
                    {
                        Interlocked.Increment(ref setupRequests);
                        byte[] body = setupBytes;
                        if (setupMode == "missing") Reply(stream, 404, new byte[0], 0, -1);
                        else if (rangeFrom >= body.Length) Reply(stream, 416, new byte[0], 0, -1);
                        else if (setupMode == "truncated") Reply(stream, rangeFrom > 0 ? 206 : 200, body, rangeFrom, 20000);
                        else if (setupMode == "slow") Reply(stream, 200, body, rangeFrom, -1, 40);
                        else Reply(stream, rangeFrom > 0 ? 206 : 200, body, rangeFrom, -1);
                    }
                    else Reply(stream, 404, new byte[0], 0, -1);
                }
                catch (IOException) { }
                catch (SocketException) { }
            }
        }

        // `body` from `from`; `cut` >= 0 closes the connection after that many bytes (the length promised in full);
        // `pauseMs` waits between 4 KB pieces.
        static void Reply(Stream stream, int status, byte[] body, long from, long cut, int pauseMs = 0)
        {
            long length = body.Length - from;
            var head = new StringBuilder();
            head.Append("HTTP/1.1 " + status + (status == 404 ? " Not Found" : status == 206 ? " Partial Content" : status == 416 ? " Range Not Satisfiable" : " OK") + "\r\n");
            head.Append("Content-Length: " + length + "\r\n");
            if (status == 206) head.Append("Content-Range: bytes " + from + "-" + (body.Length - 1) + "/" + body.Length + "\r\n");
            head.Append("Connection: close\r\n\r\n");
            byte[] headBytes = Encoding.ASCII.GetBytes(head.ToString());
            stream.Write(headBytes, 0, headBytes.Length);
            long sent = 0;
            for (long at = from; at < body.Length; at += 4096)
            {
                int n = (int)Math.Min(4096, body.Length - at);
                if (cut >= 0 && sent + n > cut) return;
                stream.Write(body, (int)at, n);
                sent += n;
                if (pauseMs > 0) Thread.Sleep(pauseMs);
            }
        }

        // A setup of `version`: its stand-in executable, then `bytes` of noise (another build of the same version).
        static byte[] SetupPayload(string version, int bytes, int seed)
        {
            byte[] exe = File.ReadAllBytes(Path.Combine(stubs, "setup-" + version + ".exe"));
            byte[] noise = Payload(bytes, seed);
            var data = new byte[exe.Length + noise.Length];
            exe.CopyTo(data, 0);
            noise.CopyTo(data, exe.Length);
            return data;
        }

        static byte[] Payload(int bytes, int seed)
        {
            var data = new byte[bytes];
            new Random(seed).NextBytes(data);
            return data;
        }

        static void CheckDownloads()
        {
            string staging = Path.Combine(scratch, "staging");
            string exe = Path.Combine(staging, "update-9.9.9-beta.1", "Nebula-Setup.exe");

            // The check: the newer beta, its notes and its setup.
            setupBytes = SetupPayload("9.9.9-beta.1", 300000, 1);
            signatureText = Sign(setupBytes);
            releasesJson = Release("9.9.9-beta.1", "What's new: everything.");
            var found = Updater.FindNewer(3000);
            Check(found != null && found.Version == "9.9.9-beta.1" && found.Notes == "What's new: everything." &&
                  found.SetupSize == setupBytes.Length, "the check finds the newer beta with its notes");
            releasesJson = Release("2.0.7-beta.1", "old");
            Check(Updater.FindNewer(3000) == null, "no newer release: nothing is offered");

            // Malformed listings: items that are not releases are passed over; an answer that is not a list (a rate
            // limit's message, a portal's page) is no answer.
            releasesJson = "[1, \"text\", null, {\"tag_name\": 5, \"assets\": [7, null]}]";
            Check(Updater.FindNewer(3000) == null, "a listing of things that are not releases offers nothing");
            releasesJson = "{\"message\": \"API rate limit exceeded\"}";
            Throws<InvalidDataException>(() => Updater.FindNewer(3000), "a rate limit's message is no listing");
            releasesJson = "<html>Sign in to the network</html>";
            Throws<Exception>(() => Updater.FindNewer(3000), "a captive portal's page is no listing");
            releasesJson = Release("9.9.9-beta.1", "plain http").Replace("http://127.0.0.1:" + port, "http://example.org");
            Check(Updater.FindNewer(3000) == null, "a release whose setup is not served over HTTPS is not offered");

            // Offline and slow: the check gives up within its time, so the game starts as it would.
            string answering = Updater.ReleasesUrl;
            var closed = new TcpListener(IPAddress.Loopback, 0);
            closed.Start();
            int closedPort = ((IPEndPoint)closed.LocalEndpoint).Port;
            closed.Stop();
            Updater.ReleasesUrl = "http://127.0.0.1:" + closedPort + "/releases";
            var clock = Stopwatch.StartNew();
            Throws<WebException>(() => Updater.FindNewer(3000), "offline: the check fails");
            Check(clock.ElapsedMilliseconds < 3500, "offline: the check fails within its 3 s (" + clock.ElapsedMilliseconds + " ms)");
            Updater.ReleasesUrl = answering;
            releasesJson = Release("9.9.9-beta.1", "slow");
            releasesDelayMs = 8000;
            clock.Restart();
            Throws<WebException>(() => Updater.FindNewer(3000), "a server that does not answer: the check fails");
            Check(clock.ElapsedMilliseconds < 4500, "it gives up after its 3 s (" + clock.ElapsedMilliseconds + " ms)");
            releasesDelayMs = 0;

            // A good download: the setup, its signature verified.
            releasesJson = Release("9.9.9-beta.1", "notes");
            var release = Updater.FindNewer(3000);
            long progress = 0;
            string path = Updater.Download(release, staging, bytes => progress = bytes, CancellationToken.None);
            Check(path == exe && File.Exists(exe) && FileEquals(exe, setupBytes) && progress == setupBytes.Length,
                  "the download is the release's setup, its signature verified, its progress reported");

            // Corrupt: one byte changed on the way; wrongly signed; signature missing.
            byte[] good = setupBytes;
            byte[] corrupt = (byte[])good.Clone();
            corrupt[corrupt.Length / 3] ^= 0x40;
            setupBytes = corrupt;
            Throws<InvalidDataException>(() => Updater.Download(release, staging, null, CancellationToken.None),
                                         "a corrupt download is refused");
            Check(!File.Exists(exe) && !File.Exists(exe + ".partial"), "a refused setup is deleted, never run");
            setupBytes = good;
            signatureText = Sign(Payload(1000, 2));
            Throws<InvalidDataException>(() => Updater.Download(release, staging, null, CancellationToken.None),
                                         "a setup with another file's signature is refused");
            Check(!File.Exists(exe), "and deleted");
            signatureText = "not base64 !";
            Throws<InvalidDataException>(() => Updater.Download(release, staging, null, CancellationToken.None),
                                         "a malformed signature is refused");
            signatureText = null;
            Throws<WebException>(() => Updater.Download(release, staging, null, CancellationToken.None),
                                 "a release without its signature downloads nothing to run");
            Check(!File.Exists(exe), "no setup is left to run");
            signatureText = Sign(good);

            // Missing: the setup gone from the release.
            setupMode = "missing";
            Throws<WebException>(() => Updater.Download(release, staging, null, CancellationToken.None),
                                 "a setup missing from the release fails at once");
            setupMode = "ok";

            // Cancelled mid-download: nothing runs; the next try resumes and verifies.
            setupMode = "slow";
            using (var cancel = new CancellationTokenSource())
            {
                cancel.CancelAfter(300);
                Throws<OperationCanceledByUserException>(() => Updater.Download(release, staging, null, cancel.Token),
                                                         "a download cancelled half way stops");
            }
            Check(!File.Exists(exe) && File.Exists(exe + ".partial") && new FileInfo(exe + ".partial").Length < good.Length,
                  "a cancelled download leaves no setup to run, only its partial file");
            setupMode = "ok";
            path = Updater.Download(release, staging, null, CancellationToken.None);
            Check(FileEquals(path, good), "the next try resumes it and verifies the whole setup");

            // A release published again under its version (a new setup and signature): the old signature and a partial
            // download of the old setup are not used.
            byte[] republished = SetupPayload("9.9.9-beta.1", 300000, 3);
            setupBytes = republished;
            signatureText = Sign(republished);
            File.Delete(exe);
            var oldStart = new byte[100000];
            Array.Copy(good, oldStart, oldStart.Length);
            File.WriteAllBytes(exe + ".partial", oldStart);
            File.WriteAllText(exe + ".sig", Sign(good));
            path = Updater.Download(release, staging, null, CancellationToken.None);
            Check(FileEquals(path, republished), "a release published again: its new setup, verified with its new signature");

            // Truncated: the connection drops half way, every time; it fails without a setup to run.
            setupMode = "truncated";
            File.Delete(exe);
            if (File.Exists(exe + ".partial")) File.Delete(exe + ".partial");
            Throws<Exception>(() => Updater.Download(release, staging, null, CancellationToken.None),
                              "a download cut off on every try fails");
            Check(!File.Exists(exe), "a cut-off download leaves no setup to run");
            setupMode = "ok";

            // A signed setup of another version offered under this release's tag (an old setup published again, a
            // downgrade): refused like an unsigned one.
            setupBytes = SetupPayload("2.0.1-beta.1", 300000, 4);
            signatureText = Sign(setupBytes);
            releasesJson = Release("9.9.9-beta.1", "replayed");
            release = Updater.FindNewer(3000);
            Throws<InvalidDataException>(() => Updater.Download(release, staging, null, CancellationToken.None),
                                         "a signed setup of 2.0.1-beta.1 offered as 9.9.9-beta.1 is refused");
            Check(!File.Exists(exe), "and deleted, never run");

            // Larger than the release says (a server sending something else): stopped at the listed size.
            setupBytes = good;
            signatureText = Sign(good);
            releasesJson = Release("9.9.9-beta.1", "notes");
            release = Updater.FindNewer(3000);
            var smaller = new ReleaseInfo { Version = release.Version, Tag = release.Tag, SetupUrl = release.SetupUrl, SignatureUrl = release.SignatureUrl, SetupSize = 100000 };
            Throws<InvalidDataException>(() => Updater.Download(smaller, staging, null, CancellationToken.None),
                                         "a download larger than its listed size is stopped");
            Check(!File.Exists(exe) && (!File.Exists(exe + ".partial") || new FileInfo(exe + ".partial").Length <= 100000),
                  "nothing beyond the listed size is kept");

            // A partial file as long as the setup (a download that ended before its rename): the server answers 416,
            // and the download starts again from the beginning.
            File.WriteAllBytes(exe + ".partial", Payload(good.Length, 5));
            path = Updater.Download(release, staging, null, CancellationToken.None);
            Check(FileEquals(path, good), "a partial file the server cannot continue is downloaded again whole");
        }

        static bool FileEquals(string path, byte[] bytes)
        {
            byte[] file = File.ReadAllBytes(path);
            if (file.Length != bytes.Length) return false;
            for (int i = 0; i < file.Length; i++) if (file[i] != bytes[i]) return false;
            return true;
        }

        static void WriteVersion(InstallLayout layout, string version, int format = InstallEngine.ContentFormatVersion)
        {
            string dir = layout.VersionDir(version);
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "install.json"), "{\"schema\":\"nebula.install.v1\",\"version\":\"" + version + "\",\"contentFormatVersion\":" + format + ",\"files\":{}}");
        }

        // This version's format holds a store of this store version (the header Setup reads), then the text; older formats
        // hold the text alone.
        static void WriteContent(string folder, int format, string text)
        {
            Directory.CreateDirectory(folder);
            File.WriteAllText(Path.Combine(folder, "content.json"), "{\"formatVersion\":" + format + "}");
            using (var writer = new BinaryWriter(File.Create(Path.Combine(folder, "game.pak"))))
            {
                if (format == InstallEngine.ContentFormatVersion)
                {
                    writer.Write(System.Text.Encoding.ASCII.GetBytes(StoreMagic));
                    writer.Write(BuildInfo.StoreVersion);
                }
                writer.Write(System.Text.Encoding.UTF8.GetBytes(text));
            }
        }

        const string StoreMagic = "NBSTORE\0";

        // The text WriteContent put in a game.pak, past a store header.
        static string Pak(string folder)
        {
            byte[] bytes = File.ReadAllBytes(Path.Combine(folder, "game.pak"));
            int skip = bytes.Length >= 12 && System.Text.Encoding.ASCII.GetString(bytes, 0, 8) == StoreMagic ? 12 : 0;
            return System.Text.Encoding.UTF8.GetString(bytes, skip, bytes.Length - skip);
        }


        static void CheckRollback()
        {
            var layout = new InstallLayout(Path.Combine(scratch, "install"));
            WriteVersion(layout, "2.0.7-beta.1");
            WriteVersion(layout, "2.0.8-beta.1");
            WriteContent(layout.Content, InstallEngine.ContentFormatVersion, "cooked");
            File.WriteAllText(layout.Current, "{\"version\":\"2.0.8-beta.1\",\"previous\":\"2.0.7-beta.1\"}");
            Check(Integration.Rollback(layout) == "2.0.7-beta.1", "rollback switches to the previous version");
            var info = InstalledInfo.Read(layout);
            Check(info.Version == "2.0.7-beta.1" && info.Previous == "2.0.8-beta.1", "and keeps the newer one to go back to");
            Check(Integration.Rollback(layout) == "2.0.8-beta.1" && InstalledInfo.Read(layout).Version == "2.0.8-beta.1",
                  "rolling back again returns to the newer version");

            File.WriteAllText(layout.Current, "{\"version\":\"2.0.8-beta.1\",\"previous\":null}");
            Throws<InvalidOperationException>(() => Integration.Rollback(layout), "no previous version: rollback says so");
            File.WriteAllText(layout.Current, "{\"version\":\"2.0.8-beta.1\",\"previous\":\"2.0.6-beta.1\"}");
            Throws<InvalidOperationException>(() => Integration.Rollback(layout), "a previous version whose folder is gone: the same");
            Check(InstalledInfo.Read(layout).Version == "2.0.8-beta.1", "a refused rollback changes nothing");

            // Nebula running from the install: rollback waits for it to close.
            File.WriteAllText(layout.Current, "{\"version\":\"2.0.8-beta.1\",\"previous\":\"2.0.7-beta.1\"}");
            string fake = Path.Combine(layout.VersionDir("2.0.8-beta.1"), "NebulaNative.exe");
            File.Copy(Path.Combine(Environment.SystemDirectory, "cmd.exe"), fake, true);
            using (var running = Process.Start(new ProcessStartInfo(fake, "/c ping -n 6 127.0.0.1 > nul") { CreateNoWindow = true, UseShellExecute = false }))
            {
                Thread.Sleep(300);
                Throws<InvalidOperationException>(() => Integration.Rollback(layout), "with Nebula running, rollback refuses");
                running.Kill();
                running.WaitForExit();
            }
            Check(InstalledInfo.Read(layout).Version == "2.0.8-beta.1", "and changes nothing");

            // After an update, only the recompiled game's versions go: the previous native one stays for rollback.
            string recompiled = layout.VersionDir("1.0.1");
            Directory.CreateDirectory(recompiled);
            File.WriteAllText(Path.Combine(recompiled, "NebulaRuntime.exe"), "v1");
            string modules = layout.VersionDir("1.0.0");
            Directory.CreateDirectory(modules);
            File.WriteAllText(Path.Combine(modules, "RMGE01_game.dll"), "v1");
            Check(InstallEngine.HoldsRecompiledGame(recompiled) && InstallEngine.HoldsRecompiledGame(modules) &&
                  !InstallEngine.HoldsRecompiledGame(layout.VersionDir("2.0.7-beta.1")),
                  "the recompiled game's versions are told from native ones");

            // 1.x cannot come back: an update removes it, and one still there is refused.
            File.WriteAllText(layout.Current, "{\"version\":\"2.0.8-beta.1\",\"previous\":\"1.0.1\"}");
            Throws<InvalidOperationException>(() => Integration.Rollback(layout), "a recompiled previous version: rollback refuses");
            Check(InstalledInfo.Read(layout).Version == "2.0.8-beta.1" && Pak(layout.Content) == "cooked", "and changes nothing");
        }

        // Rollback between content formats: the game files each version reads; the saves stay.
        static void CheckFormatRollback()
        {
            int newest = InstallEngine.ContentFormatVersion;
            var layout = new InstallLayout(Path.Combine(scratch, "install-formats"));
            WriteVersion(layout, "2.0.0-beta.1", 1);
            WriteVersion(layout, "2.0.8-beta.1", newest);
            WriteContent(layout.Content, newest, "cooked");
            string keptOld = layout.KeptContent(1), keptNew = layout.KeptContent(newest);
            WriteContent(keptOld, 1, "raw");
            File.WriteAllText(layout.Current, "{\"version\":\"2.0.8-beta.1\",\"previous\":\"2.0.0-beta.1\"}");
            // The saves are the player's: a rollback never touches them (each version reads its own file).
            string saveFolder = Path.Combine(NebulaPaths.Saves, "RMGE01");
            Directory.CreateDirectory(saveFolder);
            string save = Path.Combine(saveFolder, "Nebula.sav");
            byte[] saveBytes = Payload(64, 11);
            File.WriteAllBytes(save, saveBytes);

            Check(Integration.Rollback(layout) == "2.0.0-beta.1" && Pak(layout.Content) == "raw" && Pak(keptNew) == "cooked" && !Directory.Exists(keptOld),
                  "rollback to a format-1 version swaps its game files in and keeps the newer ones");
            Check(FileEquals(save, saveBytes), "its saves are as they were");
            Check(Integration.Rollback(layout) == "2.0.8-beta.1" && Pak(layout.Content) == "cooked" && Pak(keptOld) == "raw" && !Directory.Exists(keptNew),
                  "forward again swaps them back");
            Check(FileEquals(save, saveBytes), "and the saves are still as they were");

            // Back on the older version, this version's folder is reused by an update.
            Integration.Rollback(layout);
            Check(InstallEngine.KeptCurrentContent(layout) == keptNew && InstallEngine.RetainedGameValid(layout), "this version's game files are found aside");
            Integration.Rollback(layout);

            // A file held in the kept folder: the content returns and nothing has changed.
            using (new FileStream(Path.Combine(keptOld, "game.pak"), FileMode.Open, FileAccess.Read, FileShare.Read))
                Throws<IOException>(() => Integration.Rollback(layout), "kept game files that cannot move in: rollback fails");
            Check(InstalledInfo.Read(layout).Version == "2.0.8-beta.1" && Pak(layout.Content) == "cooked" && Pak(keptOld) == "raw" &&
                  !Directory.Exists(keptNew) && FileEquals(save, saveBytes), "and everything is as it was");

            // The kept game files gone: refused, nothing changed.
            FileUtil.DeleteTree(keptOld);
            Throws<InvalidOperationException>(() => Integration.Rollback(layout), "the previous version's game files not kept: rollback refuses");
            Check(InstalledInfo.Read(layout).Version == "2.0.8-beta.1" && Pak(layout.Content) == "cooked" && FileEquals(save, saveBytes), "and changes nothing");

            // A version that does not say what it reads: refused.
            string dir = layout.VersionDir("2.0.0-beta.1");
            File.WriteAllText(Path.Combine(dir, "install.json"), "{\"version\":\"2.0.0-beta.1\",\"files\":{}}");
            Throws<InvalidOperationException>(() => Integration.Rollback(layout), "a previous version without its content format: rollback refuses");

            // What an update reads its game from, without the disc.
            var update = new InstallLayout(Path.Combine(scratch, "install-update"));
            WriteContent(update.Content, 3, "format 3");
            Check(InstallEngine.CookSource(update) == null && !InstallEngine.RetainedGameValid(update), "format 3 alone: the disc is needed");
            WriteContent(update.KeptContent(1), 1, "raw");
            Check(InstallEngine.CookSource(update) == update.KeptContent(1) && InstallEngine.RetainedGameValid(update), "format 3 with format 1 kept: cooked from the kept folder");
            FileUtil.DeleteTree(update.Content);
            WriteContent(update.Content, 1, "raw 2");
            Check(InstallEngine.CookSource(update) == update.Content, "format 1 installed: cooked from it");
            FileUtil.DeleteTree(update.Content);
            WriteContent(update.Content, newest, "current");
            Check(InstallEngine.RetainedGameValid(update), "the current format: reused");

            // What an update keeps of what it replaced.
            var logged = new List<string>();
            string old = Path.Combine(scratch, "content-old");
            WriteContent(old, 3, "format 3");
            InstallEngine.KeepReplacedContent(update, old, logged.Add);
            Check(Pak(update.KeptContent(3)) == "format 3" && !Directory.Exists(old), "replaced content is kept by its format");
            WriteContent(old, newest, "same");
            InstallEngine.KeepReplacedContent(update, old, logged.Add);
            Check(!Directory.Exists(update.KeptContent(newest)) && Directory.Exists(old), "content of this version's own format is not");
            WriteContent(update.KeptContent(4), 4, "format 4");
            WriteVersion(update, "2.0.3-beta.1", 3);
            InstallEngine.PruneKeptContent(update, "2.0.3-beta.1", logged.Add);
            Check(Directory.Exists(update.KeptContent(1)) && Directory.Exists(update.KeptContent(3)) && !Directory.Exists(update.KeptContent(4)),
                  "format 1 and what the previous version reads stay; other formats go");
            InstallEngine.PruneKeptContent(update, null, logged.Add);
            Check(Directory.Exists(update.KeptContent(1)) && !Directory.Exists(update.KeptContent(3)), "with no previous version only format 1 stays");
        }

        // An update of a packed game of an older store packs it again from the image it was made from, when that is still there;
        // the saves, settings and the mods' small files are copied aside once per version.
        static void CheckRememberedSourceAndSnapshot()
        {
            var layout = new InstallLayout(Path.Combine(scratch, "install-remember"));
            Directory.CreateDirectory(layout.Root);
            Check(InstallEngine.RememberedSource(layout) == null && !InstallEngine.RetainedGameValid(layout), "no note of where the game came from: the disc is asked for");
            string image = Path.Combine(scratch, "game-image.iso");
            File.WriteAllBytes(image, new byte[1000]);
            string note = Path.Combine(layout.Root, "game-source.json");
            Json.Save(note, new Dictionary<string, object> { { "path", image }, { "folder", false }, { "size", 1000L } });
            Check(InstallEngine.RememberedSource(layout) == image && InstallEngine.RetainedGameValid(layout), "the image is still there as it was: an update packs the game again from it");
            File.WriteAllBytes(image, new byte[1001]);
            Check(InstallEngine.RememberedSource(layout) == null, "the image changed size: the disc is asked for");
            File.Delete(image);
            Check(InstallEngine.RememberedSource(layout) == null, "the image is gone: the disc is asked for");
            string folder = Path.Combine(scratch, "game-folder");
            Directory.CreateDirectory(folder);
            Json.Save(note, new Dictionary<string, object> { { "path", folder }, { "folder", true }, { "size", 0L } });
            Check(InstallEngine.RememberedSource(layout) == folder, "an extracted folder is found while it is there");
            File.WriteAllText(note, "not json");
            Check(InstallEngine.RememberedSource(layout) == null, "a damaged note is no note");

            string data = NebulaPaths.DataRoot;
            string save = Path.Combine(data, "saves", "RMGE01", "Nebula.sav");
            Directory.CreateDirectory(Path.GetDirectoryName(save));
            File.WriteAllBytes(save, new byte[] { 1, 2, 3 });
            File.WriteAllText(Path.Combine(data, "runtime_settings.ini"), "focus_loss=keep\n");
            Directory.CreateDirectory(Path.Combine(data, "mods", "a"));
            File.WriteAllText(Path.Combine(data, "mods", "a", "mod.json"), "{}");
            File.WriteAllBytes(Path.Combine(data, "mods", "a", "big.bin"), new byte[(1 << 20) + 1]);
            var log = new List<string>();
            string copy = UserDataSnapshot.Take("2.0.1-beta.1", log.Add);
            Check(copy != null && File.Exists(Path.Combine(copy, "saves", "RMGE01", "Nebula.sav")) && File.Exists(Path.Combine(copy, "runtime_settings.ini")) &&
                  File.Exists(Path.Combine(copy, "mods", "a", "mod.json")) && !File.Exists(Path.Combine(copy, "mods", "a", "big.bin")),
                  "the saves, settings and the mods' small files are copied aside, the large mod file is not");
            File.WriteAllBytes(save, new byte[] { 9 });
            Check(UserDataSnapshot.Take("2.0.1-beta.1", log.Add) == copy && File.ReadAllBytes(Path.Combine(copy, "saves", "RMGE01", "Nebula.sav")).Length == 3, "one copy per version: it is not taken again");
            Check(File.ReadAllBytes(save).Length == 1, "and the player's save is where it was");
            Check(UserDataSnapshot.Take(null, log.Add) == null, "no version to name a copy by: none");
            Thread.Sleep(50);
            UserDataSnapshot.Take("2.0.2-beta.1", log.Add);
            Thread.Sleep(50);
            UserDataSnapshot.Take("2.0.3-beta.1", log.Add);
            Check(Directory.GetDirectories(UserDataSnapshot.Folder).Length == 2 && Directory.Exists(Path.Combine(UserDataSnapshot.Folder, "2.0.3-beta.1")),
                  "the newest two copies stay");
        }

        // UpdateService on the server: the check begun as the launcher starts, taken before the game starts within
        // FreshBudgetMilliseconds, else the kept answer; a late answer kept for the next start; offline costing no wait;
        // no request within the hour after an answer; a failed check keeping the last answer and its time.
        static void CheckService()
        {
            int budget = UpdateService.FreshBudgetMilliseconds;
            setupBytes = SetupPayload("9.9.9-beta.1", 1000, 7);
            signatureText = Sign(setupBytes);
            releasesJson = Release("9.9.9-beta.1", "fresh");
            releasesDelayMs = 0;
            if (File.Exists(UpdateService.CachePath)) File.Delete(UpdateService.CachePath);

            UpdateService.Reset();
            int before = releasesRequests;
            var clock = Stopwatch.StartNew();
            UpdateService.Begin();
            ReleaseInfo offered = UpdateService.Offer(budget, null);
            long waited = clock.ElapsedMilliseconds;
            Check(offered != null && offered.Version == "9.9.9-beta.1" && offered.Notes == "fresh" && waited < budget,
                  "a fresh answer inside the budget is offered at this start (" + waited + " ms of " + budget + ")");
            UpdateService.Finish();
            Check(releasesRequests == before + 1, "one start, one request");
            Check(UpdateService.Offer(budget, "9.9.9-beta.1") == null, "a skipped version is not offered");

            UpdateService.Reset();
            clock.Restart();
            UpdateService.Begin();
            offered = UpdateService.Offer(budget, null);
            waited = clock.ElapsedMilliseconds;
            Check(UpdateService.Running == null && releasesRequests == before + 1 && offered != null && offered.Notes == "fresh" && waited < 100,
                  "within the hour after an answer a start makes no request and offers the kept answer at once (" + waited + " ms)");

            // The last answer two hours old and nothing newer; the server answers after the budget.
            UpdateService.WriteCache(null, DateTime.UtcNow.AddHours(-2));
            releasesJson = Release("9.9.9-beta.1", "late");
            releasesDelayMs = 1500;
            UpdateService.Reset();
            clock.Restart();
            UpdateService.Begin();
            offered = UpdateService.Offer(budget, null);
            waited = clock.ElapsedMilliseconds;
            Check(offered == null && waited >= budget - 20 && waited < budget + 250,
                  "an answer after the budget is waited for no longer than the budget; the kept answer (nothing newer) stands (" + waited + " ms)");
            UpdateService.Finish();
            releasesDelayMs = 0;
            int afterLate = releasesRequests;
            UpdateService.Reset();
            clock.Restart();
            UpdateService.Begin();
            offered = UpdateService.Offer(budget, null);
            waited = clock.ElapsedMilliseconds;
            Check(offered != null && offered.Notes == "late" && UpdateService.Running == null && releasesRequests == afterLate && waited < 100,
                  "the late answer, kept once the game started, is offered at the next start with no request (" + waited + " ms)");

            // Offline: a port nothing listens on (Windows tries the request's connection again for two seconds; the
            // probe's ends the wait), a name that does not resolve. The budget is not spent; the kept answer is offered
            // and keeps its time.
            ReleaseInfo kept;
            DateTime keptUtc;
            UpdateService.ReadCache(out kept, out keptUtc);
            DateTime stale = DateTime.UtcNow.AddHours(-2);
            var closed = new TcpListener(IPAddress.Loopback, 0);
            closed.Start();
            int closedPort = ((IPEndPoint)closed.LocalEndpoint).Port;
            closed.Stop();
            string listing = Updater.ReleasesUrl;
            foreach (string offline in new[] { "http://127.0.0.1:" + closedPort + "/releases", "http://nebula-update-check.invalid/releases" })
            {
                UpdateService.WriteCache(kept, stale);
                Updater.ReleasesUrl = offline;
                UpdateService.Reset();
                clock.Restart();
                UpdateService.Begin();
                offered = UpdateService.Offer(budget, null);
                waited = clock.ElapsedMilliseconds;
                Check(UpdateService.Running != null && offered != null && offered.Notes == "late" && waited < budget,
                      "offline (" + offline + "): the wait ends with the failure, not the budget; the kept answer is offered (" + waited + " ms)");
                UpdateService.Finish();
                ReleaseInfo after;
                DateTime afterUtc;
                Check(UpdateService.ReadCache(out after, out afterUtc) && after != null && after.Notes == "late" && Math.Abs((afterUtc - stale).TotalSeconds) < 1,
                      "a failed check keeps the last answer and its time, so the next start checks again");
            }
            Updater.ReleasesUrl = listing;

            // A last answer dated in the future (the clock was wrong then, or is now): checked again.
            UpdateService.WriteCache(kept, DateTime.UtcNow.AddDays(3));
            UpdateService.Reset();
            int beforeFuture = releasesRequests;
            UpdateService.Begin();
            UpdateService.Finish();
            Check(releasesRequests == beforeFuture + 1, "a last answer dated after now is checked again");
            UpdateService.Reset();
            File.Delete(UpdateService.CachePath);
        }

        public static int Main(string[] args)
        {
            scratch = args[0];
            key = new RSACryptoServiceProvider();
            key.PersistKeyInCsp = false;
            key.FromXmlString(File.ReadAllText(args[1]));
            stubs = args[2];
            Environment.SetEnvironmentVariable("LOCALAPPDATA", Path.Combine(scratch, "localappdata"));
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            port = ((IPEndPoint)listener.LocalEndpoint).Port;
            new Thread(() => Serve(listener)) { IsBackground = true }.Start();
            Updater.ReleasesUrl = "http://127.0.0.1:" + port + "/releases";
            try
            {
                CheckDownloads();
                CheckService();
                CheckRollback();
                CheckFormatRollback();
                CheckRememberedSourceAndSnapshot();
            }
            catch (Exception error)
            {
                Console.WriteLine(error.Message);
                return 1;
            }
            Console.WriteLine("PASS: " + checks + " update checks; the player's install, saves and settings were not touched.");
            return 0;
        }
    }
}
