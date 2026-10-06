using System;
using System.IO;
using System.Net;
using System.Security.Cryptography;
using System.Threading;

namespace Nebula.Setup
{
    /// <summary>
    /// HTTPS downloads that resume interrupted transfers and are accepted only
    /// when their SHA-256 matches the pinned value.
    /// </summary>
    internal static class Downloader
    {
        static Downloader()
        {
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12 | (SecurityProtocolType)12288; // TLS 1.3 when available
            ServicePointManager.DefaultConnectionLimit = 8;
        }

        public const string UserAgent = "Nebula-Setup";

        /// <summary>
        /// Download <paramref name="url"/> to <paramref name="path"/>. An existing
        /// complete file with the right hash is reused; a partial file resumes.
        /// </summary>
        public static void Fetch(string url, string path, string sha256, long expectedSize,
            Action<long> onBytes, CancellationToken cancel)
        {
            if (File.Exists(path) && (sha256 == null || FileUtil.Sha256File(path) == sha256))
            {
                if (onBytes != null) onBytes(new FileInfo(path).Length);
                return;
            }
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string partial = path + ".partial";
            for (int attempt = 1; ; attempt++)
            {
                try
                {
                    Transfer(url, partial, expectedSize, onBytes, cancel);
                    if (sha256 != null)
                    {
                        string actual = FileUtil.Sha256File(partial);
                        if (actual != sha256)
                        {
                            File.Delete(partial);
                            throw new InvalidDataException(string.Format(
                                "{0} failed its integrity check (expected SHA-256 {1}, received {2}).",
                                Path.GetFileName(path), sha256, actual));
                        }
                    }
                    if (File.Exists(path)) File.Delete(path);
                    File.Move(partial, path);
                    return;
                }
                catch (OperationCanceledByUserException) { throw; }
                catch (InvalidDataException)
                {
                    // A corrupt transfer was discarded; fetch it once more from the start.
                    if (attempt >= 2) throw;
                }
                catch (WebException)
                {
                    if (attempt >= 4 || cancel.IsCancellationRequested) throw;
                    Thread.Sleep(2000 * attempt);
                }
                catch (IOException)
                {
                    if (attempt >= 4 || cancel.IsCancellationRequested) throw;
                    Thread.Sleep(2000 * attempt);
                }
            }
        }

        private static void Transfer(string url, string partial, long expectedSize, Action<long> onBytes, CancellationToken cancel)
        {
            long existing = File.Exists(partial) ? new FileInfo(partial).Length : 0;
            if (expectedSize > 0 && existing > expectedSize) { File.Delete(partial); existing = 0; }
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.UserAgent = UserAgent;
            request.AllowAutoRedirect = true;
            request.Timeout = 60000;
            request.ReadWriteTimeout = 60000;
            if (existing > 0) request.AddRange(existing);
            using (var response = (HttpWebResponse)request.GetResponse())
            {
                bool resumed = existing > 0 && response.StatusCode == HttpStatusCode.PartialContent;
                using (var output = new FileStream(partial, resumed ? FileMode.Append : FileMode.Create, FileAccess.Write))
                using (var input = response.GetResponseStream())
                {
                    long total = resumed ? existing : 0;
                    if (onBytes != null) onBytes(total);
                    var buffer = new byte[1 << 20];
                    int read;
                    while ((read = input.Read(buffer, 0, buffer.Length)) > 0)
                    {
                        if (cancel.IsCancellationRequested) throw new OperationCanceledByUserException();
                        output.Write(buffer, 0, read);
                        total += read;
                        if (onBytes != null) onBytes(total);
                    }
                }
            }
        }

        public static string GetString(string url, string accept)
        {
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.UserAgent = UserAgent;
            request.Timeout = 30000;
            if (accept != null) request.Accept = accept;
            using (var response = request.GetResponse())
            using (var reader = new StreamReader(response.GetResponseStream()))
                return reader.ReadToEnd();
        }
    }
}
