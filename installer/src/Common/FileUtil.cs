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

        public static bool IsReparsePoint(string path)
        {
            var attributes = File.GetAttributes(path);
            return (attributes & FileAttributes.ReparsePoint) != 0;
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
                if (!NativeMethods.CopyFileW(file, destination, true))
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
            IntPtr find = NativeMethods.FindFirstStreamW(source, 0, out data, 0);
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
                    using (var input = NativeMethods.CreateFileW(source + name, 0x80000000, 1, IntPtr.Zero, 3, 0, IntPtr.Zero))
                    using (var output = NativeMethods.CreateFileW(target + name, 0x40000000, 0, IntPtr.Zero, 1, 0, IntPtr.Zero))
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
            IntPtr find = NativeMethods.FindFirstStreamW(path, 0, out data, 0);
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
                    using (var handle = NativeMethods.CreateFileW(name == "::$DATA" ? path : path + name,
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
