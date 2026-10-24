using System;
using System.IO;
using System.Net;
using System.Threading;

namespace Nebula
{
    /// <summary>HTTPS downloads that resume interrupted transfers.</summary>
    internal static class Downloader
    {
        static Downloader()
        {
            ServicePointManager.SecurityProtocol = SecurityProtocolType.Tls12 | (SecurityProtocolType)12288; // TLS 1.3 when available
            ServicePointManager.DefaultConnectionLimit = 8;
        }

        public const string UserAgent = "Nebula";

        /// <summary>
        /// Download <paramref name="url"/> to <paramref name="path"/>. An existing
        /// file is reused; a partial file resumes.
        /// </summary>
        public static void Fetch(string url, string path, Action<long> onBytes, CancellationToken cancel)
        {
            if (File.Exists(path))
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
                    Transfer(url, partial, onBytes, cancel);
                    if (File.Exists(path)) File.Delete(path);
                    File.Move(partial, path);
                    return;
                }
                catch (OperationCanceledByUserException) { throw; }
                catch (WebException error)
                {
                    if (cancel.IsCancellationRequested) throw;
                    // A missing file stays missing; other failures may be a passing network fault.
                    bool missing = IsMissing(error);
                    if (missing || attempt >= 4)
                        throw new WebException("Could not download " + DisplayName(url) + " from " + new Uri(url).Host + ": " +
                            error.Message, error, error.Status, error.Response);
                    Thread.Sleep(2000 * attempt);
                }
                catch (IOException)
                {
                    if (attempt >= 4 || cancel.IsCancellationRequested) throw;
                    Thread.Sleep(2000 * attempt);
                }
            }
        }

        private static bool IsMissing(WebException error)
        {
            var response = error.Response as HttpWebResponse;
            return response != null && (response.StatusCode == HttpStatusCode.NotFound || response.StatusCode == HttpStatusCode.Gone);
        }

        private static string DisplayName(string url)
        {
            var segments = new Uri(url).Segments;
            return Uri.UnescapeDataString(segments[segments.Length - 1].TrimEnd('/'));
        }

        private static void Transfer(string url, string partial, Action<long> onBytes, CancellationToken cancel)
        {
            long existing = File.Exists(partial) ? new FileInfo(partial).Length : 0;
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

        public static string GetString(string url, string accept, int timeoutMilliseconds = 30000)
        {
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.UserAgent = UserAgent;
            request.Timeout = timeoutMilliseconds;
            request.ReadWriteTimeout = timeoutMilliseconds;
            if (accept != null) request.Accept = accept;
            using (var response = request.GetResponse())
            using (var reader = new StreamReader(response.GetResponseStream()))
                return reader.ReadToEnd();
        }
    }
}
