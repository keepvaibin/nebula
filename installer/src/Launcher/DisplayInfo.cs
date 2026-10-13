using System;
using System.Collections.Generic;
using System.Drawing;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    internal sealed class MonitorMode
    {
        public int Width, Height, Refresh;
    }

    /// <summary>One monitor: its GDI device name (the runtime's "monitor" key), its own name and modes.</summary>
    internal sealed class MonitorEntry
    {
        public string Device;
        public string Name;
        public bool Primary;
        public Rectangle Bounds, WorkArea;
        public int Width, Height, Refresh;
        public List<MonitorMode> Modes = new List<MonitorMode>();

        public string Label(int index)
        {
            string name = string.IsNullOrEmpty(Name) ? "Display" : Name;
            return string.Format("{0}: {1} ({2} × {3}){4}", index + 1, name, Width, Height, Primary ? ", main" : "");
        }

        /// <summary>Distinct resolutions, largest first.</summary>
        public List<Size> Resolutions()
        {
            var sizes = new List<Size>();
            foreach (var mode in Modes)
            {
                var size = new Size(mode.Width, mode.Height);
                if (!sizes.Contains(size)) sizes.Add(size);
            }
            return sizes;
        }

        /// <summary>Refresh rates at one resolution, highest first.</summary>
        public List<int> RefreshRates(int width, int height)
        {
            return Modes.Where(m => m.Width == width && m.Height == height).Select(m => m.Refresh)
                .Distinct().OrderByDescending(r => r).ToList();
        }
    }

    /// <summary>Monitors and their display modes (same sources as the runtime's display_modes.cpp).</summary>
    internal static class DisplayInfo
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct DEVMODE
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmDeviceName;
            public short dmSpecVersion, dmDriverVersion, dmSize, dmDriverExtra;
            public int dmFields;
            public int dmPositionX, dmPositionY, dmDisplayOrientation, dmDisplayFixedOutput;
            public short dmColor, dmDuplex, dmYResolution, dmTTOption, dmCollate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string dmFormName;
            public short dmLogPixels;
            public int dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency;
            public int dmICMMethod, dmICMIntent, dmMediaType, dmDitherType, dmReserved1, dmReserved2;
            public int dmPanningWidth, dmPanningHeight;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern bool EnumDisplaySettingsExW(string device, int mode, ref DEVMODE devMode, int flags);

        private const int EnumCurrentSettings = -1;
        private const int DisplayInterlaced = 2;

        [StructLayout(LayoutKind.Sequential)]
        private struct LUID { public uint Low; public int High; }

        [StructLayout(LayoutKind.Sequential)]
        private struct PathSourceInfo { public LUID AdapterId; public uint Id; public uint ModeInfoIdx; public uint StatusFlags; }

        [StructLayout(LayoutKind.Sequential)]
        private struct PathTargetInfo
        {
            public LUID AdapterId; public uint Id; public uint ModeInfoIdx; public uint OutputTechnology; public uint Rotation;
            public uint Scaling; public uint RefreshNumerator, RefreshDenominator; public uint ScanLineOrdering;
            public int TargetAvailable; public uint StatusFlags;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct PathInfo { public PathSourceInfo Source; public PathTargetInfo Target; public uint Flags; }

        [StructLayout(LayoutKind.Sequential, Size = 64)]
        private struct ModeInfo { public uint InfoType; public uint Id; public LUID AdapterId; }

        [StructLayout(LayoutKind.Sequential)]
        private struct DeviceInfoHeader { public int Type; public int Size; public LUID AdapterId; public uint Id; }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct SourceDeviceName
        {
            public DeviceInfoHeader Header;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string ViewGdiDeviceName;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct TargetDeviceName
        {
            public DeviceInfoHeader Header;
            public uint Flags; public uint OutputTechnology; public ushort EdidManufactureId; public ushort EdidProductCodeId;
            public uint ConnectorInstance;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string MonitorFriendlyDeviceName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string MonitorDevicePath;
        }

        [DllImport("user32.dll")] private static extern int GetDisplayConfigBufferSizes(uint flags, out uint paths, out uint modes);
        [DllImport("user32.dll")] private static extern int QueryDisplayConfig(uint flags, ref uint paths, [Out] PathInfo[] pathInfo,
            ref uint modes, [Out] ModeInfo[] modeInfo, IntPtr topology);
        [DllImport("user32.dll")] private static extern int DisplayConfigGetDeviceInfo(ref SourceDeviceName name);
        [DllImport("user32.dll")] private static extern int DisplayConfigGetDeviceInfo(ref TargetDeviceName name);

        private const uint QdcOnlyActivePaths = 2;

        private static Dictionary<string, string> MonitorNames()
        {
            var names = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                uint pathCount, modeCount;
                if (GetDisplayConfigBufferSizes(QdcOnlyActivePaths, out pathCount, out modeCount) != 0) return names;
                var paths = new PathInfo[pathCount];
                var modes = new ModeInfo[modeCount];
                if (QueryDisplayConfig(QdcOnlyActivePaths, ref pathCount, paths, ref modeCount, modes, IntPtr.Zero) != 0) return names;
                for (int i = 0; i < pathCount; i++)
                {
                    var source = new SourceDeviceName();
                    source.Header.Type = 1;
                    source.Header.Size = Marshal.SizeOf(typeof(SourceDeviceName));
                    source.Header.AdapterId = paths[i].Source.AdapterId;
                    source.Header.Id = paths[i].Source.Id;
                    var target = new TargetDeviceName();
                    target.Header.Type = 2;
                    target.Header.Size = Marshal.SizeOf(typeof(TargetDeviceName));
                    target.Header.AdapterId = paths[i].Target.AdapterId;
                    target.Header.Id = paths[i].Target.Id;
                    if (DisplayConfigGetDeviceInfo(ref source) == 0 && DisplayConfigGetDeviceInfo(ref target) == 0 &&
                        !string.IsNullOrEmpty(target.MonitorFriendlyDeviceName))
                        names[source.ViewGdiDeviceName] = target.MonitorFriendlyDeviceName;
                }
            }
            catch (Exception) { /* Names are cosmetic. */ }
            return names;
        }

        /// <summary>Main monitor first, then the others left to right.</summary>
        public static List<MonitorEntry> Enumerate()
        {
            var names = MonitorNames();
            var list = new List<MonitorEntry>();
            foreach (var screen in Screen.AllScreens)
            {
                var entry = new MonitorEntry
                {
                    Device = screen.DeviceName, Primary = screen.Primary, Bounds = screen.Bounds, WorkArea = screen.WorkingArea,
                    Width = screen.Bounds.Width, Height = screen.Bounds.Height, Refresh = 60
                };
                string name;
                if (names.TryGetValue(screen.DeviceName, out name)) entry.Name = name;
                var current = new DEVMODE();
                current.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
                if (EnumDisplaySettingsExW(screen.DeviceName, EnumCurrentSettings, ref current, 0))
                {
                    entry.Width = current.dmPelsWidth;
                    entry.Height = current.dmPelsHeight;
                    entry.Refresh = Math.Max(1, current.dmDisplayFrequency);
                }
                for (int index = 0; ; index++)
                {
                    var mode = new DEVMODE();
                    mode.dmSize = (short)Marshal.SizeOf(typeof(DEVMODE));
                    if (!EnumDisplaySettingsExW(screen.DeviceName, index, ref mode, 0)) break;
                    if (mode.dmBitsPerPel != 32 || (mode.dmDisplayFlags & DisplayInterlaced) != 0 ||
                        mode.dmPelsWidth < 640 || mode.dmPelsHeight < 480) continue;
                    int refresh = Math.Max(1, mode.dmDisplayFrequency);
                    if (!entry.Modes.Any(m => m.Width == mode.dmPelsWidth && m.Height == mode.dmPelsHeight && m.Refresh == refresh))
                        entry.Modes.Add(new MonitorMode { Width = mode.dmPelsWidth, Height = mode.dmPelsHeight, Refresh = refresh });
                }
                if (entry.Modes.Count == 0)
                    entry.Modes.Add(new MonitorMode { Width = entry.Width, Height = entry.Height, Refresh = entry.Refresh });
                entry.Modes = entry.Modes.OrderByDescending(m => (long)m.Width * m.Height).ThenByDescending(m => m.Width)
                    .ThenByDescending(m => m.Refresh).ToList();
                list.Add(entry);
            }
            return list.OrderByDescending(m => m.Primary).ThenBy(m => m.Bounds.Left).ThenBy(m => m.Bounds.Top).ToList();
        }

        /// <summary>The monitor with this device name, else the main one.</summary>
        public static MonitorEntry Find(List<MonitorEntry> monitors, string device)
        {
            if (!string.IsNullOrEmpty(device))
                foreach (var monitor in monitors)
                    if (string.Equals(monitor.Device, device, StringComparison.OrdinalIgnoreCase)) return monitor;
            foreach (var monitor in monitors) if (monitor.Primary) return monitor;
            return monitors.Count > 0 ? monitors[0] : null;
        }

        /// <summary>Internal resolution whose 640x456 picture covers the given output (runtime: efb_scale_covering).</summary>
        public static int ScaleCovering(int width, int height)
        {
            if (width <= 0 || height <= 0) return 16;
            double needed = Math.Max(width / 640.0, height / 456.0);
            return Math.Max(1, Math.Min(16, (int)Math.Ceiling(needed - 1e-6)));
        }
    }
}
