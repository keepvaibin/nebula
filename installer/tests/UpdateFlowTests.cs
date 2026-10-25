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
    /// touches the player's install, saves or settings (NEBULA_DATA_ROOT and every folder are the test's).
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
        static int port;

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
            head.Append("HTTP/1.1 " + status + (status == 404 ? " Not Found" : status == 206 ? " Partial Content" : " OK") + "\r\n");
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
            setupBytes = Payload(300000, 1);
            signatureText = Sign(setupBytes);
            releasesJson = Release("9.9.9-beta.1", "What's new: everything.");
            var found = Updater.FindNewer(3000);
            Check(found != null && found.Version == "9.9.9-beta.1" && found.Notes == "What's new: everything." &&
                  found.SetupSize == setupBytes.Length, "the check finds the newer beta with its notes");
            releasesJson = Release("2.0.7-beta.1", "old");
            Check(Updater.FindNewer(3000) == null, "no newer release: nothing is offered");

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
            byte[] republished = Payload(300000, 3);
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
        }

        static bool FileEquals(string path, byte[] bytes)
        {
            byte[] file = File.ReadAllBytes(path);
            if (file.Length != bytes.Length) return false;
            for (int i = 0; i < file.Length; i++) if (file[i] != bytes[i]) return false;
            return true;
        }

        static void WriteVersion(InstallLayout layout, string version)
        {
            string dir = layout.VersionDir(version);
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "install.json"), "{\"schema\":\"nebula.install.v1\",\"version\":\"" + version + "\",\"files\":{}}");
        }

        static void CheckRollback()
        {
            var layout = new InstallLayout(Path.Combine(scratch, "install"));
            WriteVersion(layout, "2.0.7-beta.1");
            WriteVersion(layout, "2.0.8-beta.1");
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
        }

        public static int Main(string[] args)
        {
            scratch = args[0];
            key = new RSACryptoServiceProvider();
            key.PersistKeyInCsp = false;
            key.FromXmlString(File.ReadAllText(args[1]));
            Environment.SetEnvironmentVariable("NEBULA_DATA_ROOT", Path.Combine(scratch, "data"));
            var listener = new TcpListener(IPAddress.Loopback, 0);
            listener.Start();
            port = ((IPEndPoint)listener.LocalEndpoint).Port;
            new Thread(() => Serve(listener)) { IsBackground = true }.Start();
            Updater.ReleasesUrl = "http://127.0.0.1:" + port + "/releases";
            try
            {
                CheckDownloads();
                CheckRollback();
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
