using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Nebula.Setup
{
    /// <summary>The install drive's free space and file system, and the PC's memory (Setup's requirement checks).</summary>
    internal static class SystemInfo
    {
        public static long FreeBytes(string path)
        {
            ulong available, total, free;
            string probe = Path.GetFullPath(path);
            while (!Directory.Exists(probe)) probe = Path.GetDirectoryName(probe);
            if (!GetDiskFreeSpaceExW(probe, out available, out total, out free))
                throw new Win32Exception(Marshal.GetLastWin32Error(), probe);
            return (long)available;
        }

        public static string FileSystemName(string path)
        {
            string root = Path.GetPathRoot(Path.GetFullPath(path));
            var name = new StringBuilder(64);
            uint serial, maxLength, flags;
            if (!GetVolumeInformationW(root, null, 0, out serial, out maxLength, out flags, name, (uint)name.Capacity))
                return "unknown";
            return name.ToString();
        }

        public static ulong TotalPhysicalMemory()
        {
            var status = new MEMORYSTATUSEX();
            status.dwLength = (uint)Marshal.SizeOf(typeof(MEMORYSTATUSEX));
            return GlobalMemoryStatusEx(ref status) ? status.ullTotalPhys : 0;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetDiskFreeSpaceExW(string directory, out ulong available, out ulong total, out ulong free);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool GetVolumeInformationW(string root, StringBuilder volumeName, uint volumeNameSize, out uint serial,
            out uint maxComponentLength, out uint flags, StringBuilder fileSystemName, uint fileSystemNameSize);

        [StructLayout(LayoutKind.Sequential)]
        private struct MEMORYSTATUSEX
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
        private static extern bool GlobalMemoryStatusEx(ref MEMORYSTATUSEX buffer);
    }
}
