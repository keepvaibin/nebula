using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Text;
using System.Threading;
using Microsoft.Win32;

namespace Nebula
{
    /// <summary>
    /// NVIDIA DLSS and DLSS Frame Generation, installed on request from NVIDIA's
    /// Streamline 2.14.1 release on GitHub. Only the files the runtime loads are
    /// read out of the release zip (ranged requests: 36 MB of its 276 MB), and
    /// each must match its SHA-256 below. Nebula ships none of NVIDIA's files.
    /// </summary>
    internal static class DlssPackage
    {
        public const string ArchiveUrl = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.14.1/streamline-sdk-v2.14.1.zip";
        private const string ArchiveSha256 = "92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b";
        private const long ArchiveSize = 275994000;
        public const long DownloadBytes = 36367463;
        private const string LicenseEntry = "bin/x64/nvngx_dlss.license.txt";

        static DlssPackage()
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12 | (SecurityProtocolType)12288; // TLS 1.3 when available
        }

        private sealed class Part
        {
            public readonly string Entry, File, Sha256;
            public readonly long Size, Compressed;
            public Part(string entry, string file, string sha256, long size, long compressed)
            {
                Entry = entry; File = file; Sha256 = sha256; Size = size; Compressed = compressed;
            }
        }

        // Streamline's interposer and plug-ins, NVIDIA's DLSS and DLSS Frame Generation libraries,
        // Reflex (Frame Generation needs it) and the three licenses.
        private static readonly Part[] Parts =
        {
            new Part("bin/x64/sl.interposer.dll", "sl.interposer.dll", "8c87c9499461da561edd529aa9bf7831d67d7b94ebb1c1a5ed54ef4934e1ea4c", 652928, 284550),
            new Part("bin/x64/sl.common.dll", "sl.common.dll", "82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d", 843392, 390298),
            new Part("bin/x64/sl.dlss.dll", "sl.dlss.dll", "73bf52c0cfaa5900a8f3f4a91306e4625e7cca696dfb305aae44c9f97b582e1f", 422016, 194964),
            new Part("bin/x64/nvngx_dlss.dll", "nvngx_dlss.dll", "3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983", 58956912, 31123319),
            new Part(LicenseEntry, "nvngx_dlss.license.txt", "3027f23ca5a46dd9cb8183fbd522983a86f64d7daac5982912bf9f214671f294", 26727, 9256),
            new Part("license.txt", "streamline.license.txt", "7b6f23e7d6f3ad6292f9308d2b42cdc3d82ae4e9b2abb55f230279d83bedd43d", 1591, 883),
            new Part("bin/x64/sl.dlss_g.dll", "sl.dlss_g.dll", "f4a6b2b14dcc0b1485989e430d3b4e3a44ac1800b92ba1ad74f476e64fb2b09c", 636032, 290880),
            new Part("bin/x64/nvngx_dlssg.dll", "nvngx_dlssg.dll", "ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82", 7460976, 3717448),
            new Part("bin/x64/sl.reflex.dll", "sl.reflex.dll", "0ce9725e3e03ea9e7f81d008b57f33ee365973d2e349131c8b1c3e3378fe2db0", 388736, 180983),
            new Part("bin/x64/sl.pcl.dll", "sl.pcl.dll", "f13d51cfa05f4cd514df2026049e2db8adf359221713170ad386fd499915b582", 360064, 167265),
            new Part("bin/x64/reflex.license.txt", "reflex.license.txt", "ebf83c07fb3b2939908c3795d887afde3161c89a28ba391724efc784ce1bdabe", 20504, 7617),
        };

        /// <summary>The runtime loads Streamline from here (GALAXY_STREAMLINE_DIR).</summary>
        public static string Folder { get { return Path.Combine(NebulaPaths.DataRoot, "vendor", "streamline-2.14.1"); } }

        public static bool Installed()
        {
            foreach (var part in Parts)
            {
                var file = new FileInfo(Path.Combine(Folder, part.File));
                if (!file.Exists || file.Length != part.Size) return false;
            }
            return true;
        }

        /// <summary>
        /// An NVIDIA RTX adapter has a driver installed. Read from the display
        /// adapter class in the registry, which also lists laptop GPUs that drive
        /// no display. The runtime makes the final check through Streamline.
        /// </summary>
        public static bool RtxAdapterPresent()
        {
            try
            {
                using (var adapters = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"))
                {
                    if (adapters == null) return false;
                    foreach (string name in adapters.GetSubKeyNames())
                    {
                        if (name.Length != 4) continue;
                        try
                        {
                            using (var adapter = adapters.OpenSubKey(name))
                            {
                                string id = adapter == null ? null : adapter.GetValue("MatchingDeviceId") as string;
                                string description = adapter == null ? null : adapter.GetValue("DriverDesc") as string;
                                if (id != null && description != null &&
                                    id.IndexOf("ven_10de", StringComparison.OrdinalIgnoreCase) >= 0 &&
                                    description.IndexOf("RTX", StringComparison.OrdinalIgnoreCase) >= 0) return true;
                            }
                        }
                        catch (System.Security.SecurityException) { }
                        catch (UnauthorizedAccessException) { }
                    }
                }
            }
            catch (System.Security.SecurityException) { }
            catch (UnauthorizedAccessException) { }
            return false;
        }

        /// <summary>NVIDIA's DLSS license (NVIDIA RTX SDKs license), from the install or the release zip.</summary>
        public static string LicenseText(CancellationToken cancel)
        {
            string local = Path.Combine(Folder, "nvngx_dlss.license.txt");
            if (File.Exists(local)) return File.ReadAllText(local);
            var zip = new RemoteZip(ArchiveUrl, ArchiveSize);
            Entry entry;
            if (!zip.Directory(cancel).TryGetValue(LicenseEntry, out entry)) throw new InvalidDataException("The Streamline release has no DLSS license.");
            using (var buffer = new MemoryStream())
            {
                zip.Extract(entry, buffer, null, cancel);
                return Encoding.UTF8.GetString(buffer.ToArray());
            }
        }

        /// <summary>
        /// Downloads the missing files into <see cref="Folder"/>. progress gets
        /// (bytes read, bytes to read). Files already in place are kept.
        /// </summary>
        public static void Install(Action<long, long> progress, CancellationToken cancel)
        {
            Directory.CreateDirectory(Folder);
            var missing = new List<Part>();
            long total = 0;
            foreach (var part in Parts)
            {
                string path = Path.Combine(Folder, part.File);
                if (File.Exists(path) && new FileInfo(path).Length == part.Size && FileUtil.Sha256File(path) == part.Sha256) continue;
                missing.Add(part);
                total += part.Compressed;
            }
            if (missing.Count == 0) return;
            if (progress != null) progress(0, total);
            var zip = new RemoteZip(ArchiveUrl, ArchiveSize);
            Dictionary<string, Entry> entries;
            try { entries = zip.Directory(cancel); }
            catch (RangesUnsupportedException) { InstallFromArchive(progress, cancel); return; }
            long done = 0;
            foreach (var part in missing)
            {
                Entry entry;
                if (!entries.TryGetValue(part.Entry, out entry) || entry.Size != part.Size)
                    throw new InvalidDataException("The Streamline release on GitHub changed: " + part.Entry + " is missing or has another size.");
                long before = done;
                string target = Path.Combine(Folder, part.File), partial = target + ".partial";
                try
                {
                    using (var output = new FileStream(partial, FileMode.Create, FileAccess.Write))
                        zip.Extract(entry, output, read => { if (progress != null) progress(before + read, total); }, cancel);
                    Accept(partial, target, part);
                }
                finally { if (File.Exists(partial)) File.Delete(partial); }
                done = before + part.Compressed;
            }
        }

        // For connections that drop range requests: the whole release zip.
        private static void InstallFromArchive(Action<long, long> progress, CancellationToken cancel)
        {
            string archive = Path.Combine(Folder, "streamline-sdk-v2.14.1.zip.partial");
            try
            {
                using (var output = new FileStream(archive, FileMode.Create, FileAccess.Write))
                    RemoteZip.Download(ArchiveUrl, output, read => { if (progress != null) progress(read, ArchiveSize); }, cancel);
                if (FileUtil.Sha256File(archive) != ArchiveSha256)
                    throw new InvalidDataException("The Streamline release zip failed its integrity check.");
                using (var zip = ZipFile.OpenRead(archive))
                    foreach (var part in Parts)
                    {
                        var entry = zip.GetEntry(part.Entry);
                        if (entry == null) throw new InvalidDataException("The Streamline release has no " + part.Entry + ".");
                        string target = Path.Combine(Folder, part.File), partial = target + ".partial";
                        try
                        {
                            using (var input = entry.Open())
                            using (var output = new FileStream(partial, FileMode.Create, FileAccess.Write))
                                input.CopyTo(output);
                            Accept(partial, target, part);
                        }
                        finally { if (File.Exists(partial)) File.Delete(partial); }
                    }
            }
            finally { if (File.Exists(archive)) File.Delete(archive); }
        }

        private static void Accept(string partial, string target, Part part)
        {
            string actual = FileUtil.Sha256File(partial);
            if (actual != part.Sha256)
                throw new InvalidDataException(part.File + " failed its integrity check (expected SHA-256 " + part.Sha256 + ", received " + actual + ").");
            if (File.Exists(target)) File.Delete(target);
            File.Move(partial, target);
        }

        private sealed class Entry
        {
            public string Name;
            public int Method;
            public long Compressed, Size, Offset;
        }

        private sealed class RangesUnsupportedException : Exception { }

        /// <summary>Reads parts of a zip on a web server with HTTP range requests.</summary>
        private sealed class RemoteZip
        {
            private const string UserAgent = "Nebula";
            private readonly string url;
            private readonly long size;
            private string resolved;

            public RemoteZip(string url, long size) { this.url = url; this.size = size; }

            public Dictionary<string, Entry> Directory(CancellationToken cancel)
            {
                int tailLength = (int)Math.Min(size, 65536 + 22);
                byte[] tail = Read(size - tailLength, tailLength, cancel);
                int end = -1;
                for (int i = tail.Length - 22; i >= 0; i--)
                    if (U32(tail, i) == 0x06054b50u) { end = i; break; }
                if (end < 0) throw new InvalidDataException("The Streamline release zip has no directory.");
                long directorySize = U32(tail, end + 12), directoryOffset = U32(tail, end + 16);
                if (directoryOffset + directorySize > size) throw new InvalidDataException("The Streamline release zip directory is invalid.");
                byte[] directory = Read(directoryOffset, (int)directorySize, cancel);
                var entries = new Dictionary<string, Entry>(StringComparer.Ordinal);
                for (int p = 0; p + 46 <= directory.Length && U32(directory, p) == 0x02014b50u; )
                {
                    int nameLength = U16(directory, p + 28);
                    var entry = new Entry
                    {
                        Method = U16(directory, p + 10),
                        Compressed = U32(directory, p + 20),
                        Size = U32(directory, p + 24),
                        Offset = U32(directory, p + 42),
                        Name = Encoding.UTF8.GetString(directory, p + 46, nameLength)
                    };
                    entries[entry.Name] = entry;
                    p += 46 + nameLength + U16(directory, p + 30) + U16(directory, p + 32);
                }
                return entries;
            }

            /// <summary>Writes the entry's uncompressed bytes; onRead gets the compressed bytes read so far.</summary>
            public void Extract(Entry entry, Stream output, Action<long> onRead, CancellationToken cancel)
            {
                if (entry.Method != 0 && entry.Method != 8) throw new InvalidDataException(entry.Name + " uses an unsupported compression method.");
                byte[] header = Read(entry.Offset, 30, cancel);
                if (U32(header, 0) != 0x04034b50u) throw new InvalidDataException(entry.Name + " has no local header.");
                long data = entry.Offset + 30 + U16(header, 26) + U16(header, 28);
                if (entry.Compressed == 0) return;
                long start = output.Position;
                for (int attempt = 1; ; attempt++)
                {
                    try
                    {
                        using (var response = Open(data, entry.Compressed))
                        using (var body = new CountingStream(response.GetResponseStream(), onRead, cancel))
                        {
                            if (entry.Method == 8)
                                using (var inflate = new DeflateStream(body, CompressionMode.Decompress, true)) inflate.CopyTo(output);
                            else body.CopyTo(output);
                            if (body.Count != entry.Compressed) throw new IOException(entry.Name + " was cut short.");
                        }
                        return;
                    }
                    catch (OperationCanceledException) { throw; }
                    catch (Exception error)
                    {
                        if (!(error is WebException || error is IOException || error is InvalidDataException) || attempt >= 3 || cancel.IsCancellationRequested) throw;
                        output.SetLength(start);
                        output.Position = start;
                        resolved = null;
                        Thread.Sleep(1500 * attempt);
                    }
                }
            }

            public byte[] Read(long offset, int count, CancellationToken cancel)
            {
                for (int attempt = 1; ; attempt++)
                {
                    try
                    {
                        using (var response = Open(offset, count))
                        using (var body = response.GetResponseStream())
                        {
                            var buffer = new byte[count];
                            int filled = 0, read;
                            while (filled < count && (read = body.Read(buffer, filled, count - filled)) > 0)
                            {
                                cancel.ThrowIfCancellationRequested();
                                filled += read;
                            }
                            if (filled != count) throw new IOException("The download was cut short.");
                            return buffer;
                        }
                    }
                    catch (OperationCanceledException) { throw; }
                    catch (RangesUnsupportedException) { throw; }
                    catch (Exception error)
                    {
                        if (!(error is WebException || error is IOException) || attempt >= 3 || cancel.IsCancellationRequested) throw;
                        resolved = null;
                        Thread.Sleep(1500 * attempt);
                    }
                }
            }

            private HttpWebResponse Open(long offset, long count)
            {
                if (resolved == null) resolved = Resolve(url);
                var request = (HttpWebRequest)WebRequest.Create(resolved);
                request.UserAgent = UserAgent;
                request.Timeout = 30000;
                request.ReadWriteTimeout = 30000;
                request.AddRange(offset, offset + count - 1);
                var response = (HttpWebResponse)request.GetResponse();
                if (response.StatusCode != HttpStatusCode.PartialContent)
                {
                    response.Dispose();
                    throw new RangesUnsupportedException();
                }
                return response;
            }

            // GitHub redirects to a signed storage URL; the ranges go there directly.
            private static string Resolve(string start)
            {
                string current = start;
                for (int hop = 0; hop < 5; hop++)
                {
                    var request = (HttpWebRequest)WebRequest.Create(current);
                    request.UserAgent = UserAgent;
                    request.Method = "HEAD";
                    request.AllowAutoRedirect = false;
                    request.Timeout = 30000;
                    using (var response = (HttpWebResponse)request.GetResponse())
                    {
                        int status = (int)response.StatusCode;
                        string location = response.Headers["Location"];
                        if (status < 300 || status >= 400 || string.IsNullOrEmpty(location)) return current;
                        current = new Uri(new Uri(current), location).AbsoluteUri;
                    }
                }
                return current;
            }

            public static void Download(string url, Stream output, Action<long> onRead, CancellationToken cancel)
            {
                var request = (HttpWebRequest)WebRequest.Create(url);
                request.UserAgent = UserAgent;
                request.Timeout = 60000;
                request.ReadWriteTimeout = 60000;
                using (var response = request.GetResponse())
                using (var body = new CountingStream(response.GetResponseStream(), onRead, cancel))
                    body.CopyTo(output);
            }
        }

        private static int U16(byte[] b, int i) { return b[i] | b[i + 1] << 8; }
        private static uint U32(byte[] b, int i) { return (uint)(b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24); }

        private sealed class CountingStream : Stream
        {
            private readonly Stream inner;
            private readonly Action<long> onRead;
            private readonly CancellationToken cancel;
            public long Count { get; private set; }
            public CountingStream(Stream inner, Action<long> onRead, CancellationToken cancel) { this.inner = inner; this.onRead = onRead; this.cancel = cancel; }
            public override int Read(byte[] buffer, int offset, int count)
            {
                cancel.ThrowIfCancellationRequested();
                int read = inner.Read(buffer, offset, count);
                Count += read;
                if (onRead != null && read > 0) onRead(Count);
                return read;
            }
            public override bool CanRead { get { return true; } }
            public override bool CanSeek { get { return false; } }
            public override bool CanWrite { get { return false; } }
            public override long Length { get { throw new NotSupportedException(); } }
            public override long Position { get { return Count; } set { throw new NotSupportedException(); } }
            public override void Flush() { }
            public override long Seek(long offset, SeekOrigin origin) { throw new NotSupportedException(); }
            public override void SetLength(long value) { throw new NotSupportedException(); }
            public override void Write(byte[] buffer, int offset, int count) { throw new NotSupportedException(); }
            protected override void Dispose(bool disposing) { if (disposing) inner.Dispose(); base.Dispose(disposing); }
        }
    }
}
