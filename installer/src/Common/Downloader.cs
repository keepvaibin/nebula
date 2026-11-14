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
            // A proxy that asks who is calling gets the signed-in user (WinINet's proxy settings are the default proxy).
            if (WebRequest.DefaultWebProxy != null) WebRequest.DefaultWebProxy.Credentials = CredentialCache.DefaultNetworkCredentials;
        }

        public const string UserAgent = "Nebula";

        /// <summary>
        /// Download <paramref name="url"/> to <paramref name="path"/>. An existing
        /// file is reused; a partial file resumes. A transfer that grows past <paramref name="maxBytes"/> (a server
        /// answering with something else: a captive portal's page, a wrong file) is dropped.
        /// </summary>
        public static void Fetch(string url, string path, Action<long> onBytes, CancellationToken cancel, long maxBytes = long.MaxValue)
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
                    Transfer(url, partial, onBytes, cancel, maxBytes);
                    if (File.Exists(path)) File.Delete(path);
                    File.Move(partial, path);
                    return;
                }
                catch (OperationCanceledByUserException) { throw; }
                catch (WebException error)
                {
                    if (cancel.IsCancellationRequested) throw;
                    // A partial file the server cannot continue (416: it is as long as the file or longer, from a file
                    // replaced since) starts again.
                    var response = error.Response as HttpWebResponse;
                    if (response != null && (int)response.StatusCode == 416 && File.Exists(partial)) File.Delete(partial);
                    // A missing file stays missing; other failures may be a passing network fault.
                    bool missing = IsMissing(error);
                    if (missing || attempt >= 4)
                        throw new WebException("Could not download " + DisplayName(url) + " from " + new Uri(url).Host + ": " +
                            error.Message, error, error.Status, error.Response);
                    Thread.Sleep(2000 * attempt);
                }
                catch (IOException error)
                {
                    // A full disk stays full: no retry.
                    if (attempt >= 4 || cancel.IsCancellationRequested || DiskFull(error)) throw;
                    Thread.Sleep(2000 * attempt);
                }
            }
        }

        private static bool DiskFull(IOException error)
        {
            int code = System.Runtime.InteropServices.Marshal.GetHRForException(error) & 0xFFFF;
            return code == 112 || code == 39;  // ERROR_DISK_FULL, ERROR_HANDLE_DISK_FULL
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

        private static void Transfer(string url, string partial, Action<long> onBytes, CancellationToken cancel, long maxBytes)
        {
            long existing = File.Exists(partial) ? new FileInfo(partial).Length : 0;
            if (existing > maxBytes)
            {
                File.Delete(partial);
                existing = 0;
            }
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
                        total += read;
                        if (total > maxBytes) throw new InvalidDataException("The download of " + DisplayName(url) + " is larger than the release says.");
                        output.Write(buffer, 0, read);
                        if (onBytes != null) onBytes(total);
                    }
                }
            }
        }

        private const int MaxListingChars = 8 << 20;

        public static string GetString(string url, string accept, int timeoutMilliseconds = 30000)
        {
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.UserAgent = UserAgent;
            request.Timeout = timeoutMilliseconds;
            request.ReadWriteTimeout = timeoutMilliseconds;
            if (accept != null) request.Accept = accept;
            using (var response = request.GetResponse())
            using (var reader = new StreamReader(response.GetResponseStream()))
            {
                // A release listing is a few hundred kilobytes; a portal or proxy page that never ends is not one.
                var text = new char[MaxListingChars + 1];
                int length = 0, read;
                while (length <= MaxListingChars && (read = reader.Read(text, length, text.Length - length)) > 0) length += read;
                if (length > MaxListingChars) throw new InvalidDataException("The release list is too large.");
                return new string(text, 0, length);
            }
        }
    }
}
