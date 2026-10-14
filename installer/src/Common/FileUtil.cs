using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace Nebula
{
    internal static class FileUtil
    {
        public static string Sha256File(string path)
        {
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 1 << 20))
            using (var sha = SHA256.Create())
                return Hex(sha.ComputeHash(stream));
        }

        public static string Sha256Bytes(byte[] bytes)
        {
            using (var sha = SHA256.Create()) return Hex(sha.ComputeHash(bytes));
        }

        public static string Sha256Text(string text) { return Sha256Bytes(Encoding.UTF8.GetBytes(text)); }

        public static string Hex(byte[] bytes)
        {
            var builder = new StringBuilder(bytes.Length * 2);
            foreach (byte b in bytes) builder.Append(b.ToString("x2"));
            return builder.ToString();
        }

        public static void WriteAllTextAtomic(string path, string text)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            File.WriteAllText(temp, text, new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temp, path, null, true);
            else File.Move(temp, path);
        }

        /// <summary>Delete a directory tree, clearing read-only attributes.</summary>
        public static void DeleteTree(string path)
        {
            if (!Directory.Exists(path)) return;
            foreach (var file in Directory.GetFiles(path, "*", SearchOption.AllDirectories))
                File.SetAttributes(file, FileAttributes.Normal);
            for (int attempt = 0; ; attempt++)
            {
                try { Directory.Delete(path, true); return; }
                catch (IOException) { if (attempt >= 5) throw; System.Threading.Thread.Sleep(500); }
                catch (UnauthorizedAccessException) { if (attempt >= 5) throw; System.Threading.Thread.Sleep(500); }
            }
        }

        /// <summary>
        /// Moves a directory. A file in it that another process holds open for a moment (a virus scanner or indexer
        /// reading freshly written programs) refuses the move: it is retried for about ten seconds, then the tree is
        /// copied instead (a reader's handle still allows that). A failure names the processes the Restart Manager
        /// reports holding its files.
        /// </summary>
        public static void MoveDirectory(string source, string destination, Action<string> log)
        {
            Exception refused = null;
            for (int attempt = 0; attempt < 20; attempt++)
            {
                try { Directory.Move(source, destination); return; }
                catch (IOException error) { refused = error; }
                catch (UnauthorizedAccessException error) { refused = error; }
                System.Threading.Thread.Sleep(500);
            }
            log("Could not move " + source + " (" + refused.Message + "); copying it instead.");
            try
            {
                CopyTree(source, destination);
            }
            catch (Exception error)
            {
                if (!(error is IOException) && !(error is UnauthorizedAccessException)) throw;
                try { DeleteTree(destination); } catch (Exception) { }
                string holders = Holders(Directory.GetFiles(source, "*", SearchOption.AllDirectories));
                throw new IOException(refused.Message + (holders.Length != 0 ? " In use by: " + holders + "." : ""), error);
            }
            try { DeleteTree(source); } catch (Exception) { }
        }

        public static void CopyTree(string source, string destination)
        {
            string from = Path.GetFullPath(source).TrimEnd(Path.DirectorySeparatorChar);
            string to = Path.GetFullPath(destination);
            Directory.CreateDirectory(to);
            foreach (var dir in Directory.GetDirectories(from, "*", SearchOption.AllDirectories))
                Directory.CreateDirectory(Path.Combine(to, dir.Substring(from.Length + 1)));
            foreach (var file in Directory.GetFiles(from, "*", SearchOption.AllDirectories))
                File.Copy(file, Path.Combine(to, file.Substring(from.Length + 1)), true);
        }

        /// <summary>The processes holding any of the files open, as "name (pid)"; "" when none or unknown.</summary>
        public static string Holders(string[] files)
        {
            uint session;
            if (files.Length == 0 || NativeMethods.RmStartSession(out session, 0, new StringBuilder(64)) != 0) return "";
            try
            {
                if (NativeMethods.RmRegisterResources(session, (uint)files.Length, files, 0, null, 0, null) != 0) return "";
                uint needed, count = 0, reasons = 0;
                const int ErrorMoreData = 234;
                if (NativeMethods.RmGetList(session, out needed, ref count, null, ref reasons) != ErrorMoreData || needed == 0)
                    return "";
                var info = new NativeMethods.RmProcessInfo[needed];
                count = needed;
                if (NativeMethods.RmGetList(session, out needed, ref count, info, ref reasons) != 0) return "";
                var names = new List<string>();
                for (int i = 0; i < count; i++) names.Add(info[i].AppName + " (" + info[i].Process.ProcessId + ")");
                return string.Join(", ", names);
            }
            finally { NativeMethods.RmEndSession(session); }
        }

        public static bool IsReparsePoint(string path)
        {
            var attributes = File.GetAttributes(path);
            return (attributes & FileAttributes.ReparsePoint) != 0;
        }

        // Named streams can push otherwise valid save paths past MAX_PATH.
        // Keep Win32 calls in the extended-length namespace, including UNC paths.
        private static string NativePath(string path)
        {
            if (path.StartsWith(@"\\?\", StringComparison.Ordinal)) return path;
            string full = Path.GetFullPath(path);
            return full.StartsWith(@"\\", StringComparison.Ordinal)
                ? @"\\?\UNC\" + full.Substring(2) : @"\\?\" + full;
        }

        /// <summary>Resolve junctions/symlinks in a directory path to the physical path.</summary>
        public static string FinalPath(string path)
        {
            using (var handle = NativeMethods.CreateFileW(path, 0, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero))
            {
                if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), path);
                var buffer = new StringBuilder(1024);
                uint length = NativeMethods.GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);
                if (length == 0 || length >= buffer.Capacity) throw new Win32Exception(Marshal.GetLastWin32Error(), path);
                string result = buffer.ToString();
                if (result.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) return @"\\" + result.Substring(8);
                if (result.StartsWith(@"\\?\", StringComparison.Ordinal)) return result.Substring(4);
                return result;
            }
        }

        /// <summary>
        /// Copy a directory tree with CopyFileW, which carries every alternate
        /// data stream of each file. Reparse points inside the tree are refused.
        /// </summary>
        public static void CopyTreeWithStreams(string source, string target, Action<string> onFile)
        {
            if (IsReparsePoint(source) && !string.Equals(source, FinalPath(source), StringComparison.OrdinalIgnoreCase))
                source = FinalPath(source);
            Directory.CreateDirectory(target);
            CopyNamedStreams(source, target);
            foreach (var directory in Directory.GetDirectories(source))
            {
                if (IsReparsePoint(directory)) throw new IOException("Refusing to copy a linked folder: " + directory);
                CopyTreeWithStreams(directory, Path.Combine(target, Path.GetFileName(directory)), onFile);
            }
            foreach (var file in Directory.GetFiles(source))
            {
                if (IsReparsePoint(file)) throw new IOException("Refusing to copy a linked file: " + file);
                string destination = Path.Combine(target, Path.GetFileName(file));
                if (!NativeMethods.CopyFileW(NativePath(file), NativePath(destination), true))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "Copy failed: " + file);
                if (onFile != null) onFile(file);
            }
        }

        /// <summary>
        /// Copy the named (alternate) data streams of a directory. CopyFileW
        /// carries file streams, but directories can hold streams as well.
        /// </summary>
        public static void CopyNamedStreams(string source, string target)
        {
            NativeMethods.WIN32_FIND_STREAM_DATA data;
            IntPtr find = NativeMethods.FindFirstStreamW(NativePath(source), 0, out data, 0);
            if (find == new IntPtr(-1))
            {
                int error = Marshal.GetLastWin32Error();
                if (error == 38) return; // ERROR_HANDLE_EOF: no streams
                throw new Win32Exception(error, source);
            }
            try
            {
                do
                {
                    string name = data.cStreamName;
                    if (name == "::$DATA") continue;
                    using (var input = NativeMethods.CreateFileW(NativePath(source) + name, 0x80000000, 1, IntPtr.Zero, 3, 0, IntPtr.Zero))
                    using (var output = NativeMethods.CreateFileW(NativePath(target) + name, 0x40000000, 0, IntPtr.Zero, 1, 0, IntPtr.Zero))
                    {
                        if (input.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), source + name);
                        if (output.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), target + name);
                        using (var reader = new FileStream(input, FileAccess.Read))
                        using (var writer = new FileStream(output, FileAccess.Write))
                            reader.CopyTo(writer);
                    }
                } while (NativeMethods.FindNextStreamW(find, out data));
            }
            finally { NativeMethods.FindClose(find); }
        }

        /// <summary>Names and contents of every stream of a file or directory, for verification.</summary>
        public static SortedDictionary<string, string> StreamDigests(string path)
        {
            var result = new SortedDictionary<string, string>(StringComparer.Ordinal);
            NativeMethods.WIN32_FIND_STREAM_DATA data;
            IntPtr find = NativeMethods.FindFirstStreamW(NativePath(path), 0, out data, 0);
            if (find == new IntPtr(-1))
            {
                int error = Marshal.GetLastWin32Error();
                if (error == 38) return result; // a directory without streams
                throw new Win32Exception(error, path);
            }
            try
            {
                do
                {
                    string name = data.cStreamName;
                    // FileStream rejects "file:stream" paths; open streams natively.
                    using (var handle = NativeMethods.CreateFileW(name == "::$DATA" ? NativePath(path) : NativePath(path) + name,
                        0x80000000, 1, IntPtr.Zero, 3, 0, IntPtr.Zero))
                    {
                        if (handle.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(), path + name);
                        using (var stream = new FileStream(handle, FileAccess.Read))
                        using (var sha = SHA256.Create())
                            result[name] = Hex(sha.ComputeHash(stream));
                    }
                } while (NativeMethods.FindNextStreamW(find, out data));
            }
            finally { NativeMethods.FindClose(find); }
            return result;
        }

        public static long FreeBytes(string path)
        {
            ulong available, total, free;
            string probe = Path.GetFullPath(path);
            while (!Directory.Exists(probe)) probe = Path.GetDirectoryName(probe);
            if (!NativeMethods.GetDiskFreeSpaceExW(probe, out available, out total, out free))
                throw new Win32Exception(Marshal.GetLastWin32Error(), probe);
            return (long)available;
        }

        public static string FileSystemName(string path)
        {
            string root = Path.GetPathRoot(Path.GetFullPath(path));
            var name = new StringBuilder(64);
            uint serial, maxLength, flags;
            if (!NativeMethods.GetVolumeInformationW(root, null, 0, out serial, out maxLength, out flags, name, (uint)name.Capacity))
                return "unknown";
            return name.ToString();
        }

        public static string FormatBytes(long bytes)
        {
            if (bytes >= 1L << 30) return string.Format("{0:0.0} GB", bytes / (double)(1L << 30));
            if (bytes >= 1L << 20) return string.Format("{0:0} MB", bytes / (double)(1L << 20));
            return string.Format("{0:0} KB", bytes / 1024.0);
        }
    }

    internal static class NativeMethods
    {
        // Restart Manager (FileUtil.Holders).
        [StructLayout(LayoutKind.Sequential)]
        internal struct RmUniqueProcess
        {
            public int ProcessId;
            public System.Runtime.InteropServices.ComTypes.FILETIME StartTime;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        internal struct RmProcessInfo
        {
            public RmUniqueProcess Process;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string AppName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string ServiceShortName;
            public int ApplicationType;
            public uint AppStatus;
            public uint TSSessionId;
            [MarshalAs(UnmanagedType.Bool)] public bool Restartable;
        }

        [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
        internal static extern int RmStartSession(out uint session, int flags, StringBuilder key);

        [DllImport("rstrtmgr.dll")]
        internal static extern int RmEndSession(uint session);

        [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
        internal static extern int RmRegisterResources(uint session, uint fileCount, string[] files, uint processCount,
            RmUniqueProcess[] processes, uint serviceCount, string[] services);

        [DllImport("rstrtmgr.dll")]
        internal static extern int RmGetList(uint session, out uint needed, ref uint count, [In, Out] RmProcessInfo[] info,
            ref uint rebootReasons);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern SafeFileHandle CreateFileW(string path, uint access, uint share, IntPtr security, uint disposition, uint flags, IntPtr template);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle, StringBuilder path, uint size, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool CopyFileW(string existing, string target, [MarshalAs(UnmanagedType.Bool)] bool failIfExists);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct WIN32_FIND_STREAM_DATA
        {
            public long StreamSize;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 296)]
            public string cStreamName;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern IntPtr FindFirstStreamW(string path, int level, out WIN32_FIND_STREAM_DATA data, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool FindNextStreamW(IntPtr find, out WIN32_FIND_STREAM_DATA data);

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool FindClose(IntPtr find);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetDiskFreeSpaceExW(string directory, out ulong available, out ulong total, out ulong free);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GetVolumeInformationW(string root, StringBuilder volumeName, uint volumeNameSize, out uint serial, out uint maxComponentLength, out uint flags, StringBuilder fileSystemName, uint fileSystemNameSize);

        [StructLayout(LayoutKind.Sequential)]
        public struct MEMORYSTATUSEX
        {
            public uint dwLength;
            public uint dwMemoryLoad;
            public ulong ullTotalPhys;
            public ulong ullAvailPhys;
            public ulong ullTotalPageFile;
            public ulong ullAvailPageFile;
            public ulong ullTotalVirtual;
            public ulong ullAvailVirtual;
            public ulong ullAvailExtendedVirtual;
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        public static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX buffer);

        public static ulong TotalPhysicalMemory()
        {
            var status = new MEMORYSTATUSEX();
            status.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
            return GlobalMemoryStatusEx(ref status) ? status.ullTotalPhys : 0;
        }

        public static ulong AvailablePhysicalMemory()
        {
            var status = new MEMORYSTATUSEX();
            status.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
            return GlobalMemoryStatusEx(ref status) ? status.ullAvailPhys : 0;
        }

        [DllImport("user32.dll")]
        public static extern bool SetProcessDpiAwarenessContext(IntPtr context);
    }
}
