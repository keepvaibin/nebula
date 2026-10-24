using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Nebula.Launcher
{
    /// <summary>A hardware GPU as DXGI lists it to the game.</summary>
    internal sealed class GpuAdapter
    {
        public string Name;
        public uint VendorId, DeviceId, SubSysId;
        // GDI device names (\\.\DISPLAY1) of the displays connected to this GPU (DXGI_OUTPUT_DESC.DeviceName).
        public List<string> Outputs = new List<string>();

        /// <summary>The game's gpu key (render_config.cpp gpu_identity): vendor, device and subsystem id in lowercase hex.</summary>
        public string Id { get { return string.Format("{0:x4}:{1:x4}:{2:x8}", VendorId & 0xFFFF, DeviceId & 0xFFFF, SubSysId); } }

        /// <summary>An NVIDIA RTX GPU: DLSS, and Nebula's ray tracing (DXR 1.1, shader model 6.5, its own memory).</summary>
        public bool IsNvidiaRtx { get { return VendorId == 0x10DE && Name.IndexOf("RTX", StringComparison.OrdinalIgnoreCase) >= 0; } }

        public bool Drives(string device)
        {
            return Outputs.Exists(output => string.Equals(output, device, StringComparison.OrdinalIgnoreCase));
        }
    }

    /// <summary>
    /// The hardware GPUs the game can render on, in the order its automatic choice tries them (DXGI's
    /// high-performance order).
    /// </summary>
    internal static class GpuAdapters
    {
        /// <summary>Replaced by tests; null when DXGI cannot be asked.</summary>
        internal static Func<List<GpuAdapter>> Source = () => Enumerate(false);

        public static List<GpuAdapter> List()
        {
            try { return Source(); }
            catch (COMException) { return null; }
            catch (InvalidCastException) { return null; }
            catch (DllNotFoundException) { return null; }
            catch (EntryPointNotFoundException) { return null; }
        }

        /// <summary>The GPU the game will run on for a gpu setting: that GPU when present, else the automatic first.</summary>
        public static GpuAdapter Resolve(List<GpuAdapter> adapters, string chosen)
        {
            if (adapters == null || adapters.Count == 0) return null;
            if (!string.IsNullOrEmpty(chosen))
                foreach (var adapter in adapters)
                    if (adapter.Id == chosen) return adapter;
            return adapters[0];
        }

        /// <summary>The GPU's name in the Graphics card list; two of the same model are told apart by their place in it.</summary>
        public static string Label(List<GpuAdapter> adapters, GpuAdapter gpu)
        {
            var same = adapters.FindAll(g => g.Name == gpu.Name);
            return same.Count > 1 ? gpu.Name + " #" + (same.IndexOf(gpu) + 1) : gpu.Name;
        }

        /// <summary>
        /// The line under the Monitor row when the GPU the game will run on is not the one the display is connected
        /// to; null when it is, or when no listed GPU names the display as its output.
        /// </summary>
        public static string DisplayNotice(List<GpuAdapter> adapters, string gpu, string monitorDevice)
        {
            GpuAdapter target = Resolve(adapters, gpu);
            if (target == null || string.IsNullOrEmpty(monitorDevice) || target.Drives(monitorDevice)) return null;
            GpuAdapter connected = adapters.Find(adapter => adapter.Drives(monitorDevice));
            if (connected == null) return null;
            return string.Format("Connected to the {0}; frames are copied from the {1}.",
                                 Label(adapters, connected), Label(adapters, target));
        }

        private const uint SoftwareAdapter = 2; // DXGI_ADAPTER_FLAG_SOFTWARE
        private const int HighPerformance = 2; // DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE

        // Software adapters (WARP, the Microsoft Basic Render Driver) are never the game's; tests list them.
        internal static List<GpuAdapter> Enumerate(bool software)
        {
            Guid factoryId = typeof(IDXGIFactory1).GUID;
            object created;
            Marshal.ThrowExceptionForHR(CreateDXGIFactory1(ref factoryId, out created));
            var adapters = new List<GpuAdapter>();
            try
            {
                var factory6 = created as IDXGIFactory6;
                Guid adapterId = typeof(IDXGIAdapter1).GUID;
                for (uint index = 0; ; index++)
                {
                    object item;
                    int result = factory6 != null
                        ? factory6.EnumAdapterByGpuPreference(index, HighPerformance, ref adapterId, out item)
                        : EnumAdapters1((IDXGIFactory1)created, index, out item);
                    if (result != 0) break; // DXGI_ERROR_NOT_FOUND ends the list
                    var adapter = (IDXGIAdapter1)item;
                    try
                    {
                        DxgiAdapterDesc1 desc;
                        if (adapter.GetDesc1(out desc) != 0 || (!software && (desc.Flags & SoftwareAdapter) != 0)) continue;
                        var gpu = new GpuAdapter
                        {
                            Name = (desc.Description ?? "").Trim(),
                            VendorId = desc.VendorId, DeviceId = desc.DeviceId, SubSysId = desc.SubSysId,
                            Outputs = OutputNames(adapter)
                        };
                        // Windows can list one GPU more than once (a virtual or remote display adds an entry with its
                        // own LUID). It is one choice; the game takes the first entry in this order. The displays
                        // of every entry are that GPU's.
                        var listed = adapters.Find(other => other.Id == gpu.Id);
                        if (listed == null) adapters.Add(gpu);
                        else listed.Outputs.AddRange(gpu.Outputs.FindAll(output => !listed.Drives(output)));
                    }
                    finally { Marshal.ReleaseComObject(adapter); }
                }
            }
            finally { Marshal.ReleaseComObject(created); }
            return adapters;
        }

        private static List<string> OutputNames(IDXGIAdapter1 adapter)
        {
            var outputs = new List<string>();
            for (uint index = 0; ; index++)
            {
                IDXGIOutput output;
                if (adapter.EnumOutputs(index, out output) != 0) break; // DXGI_ERROR_NOT_FOUND ends the list
                try
                {
                    DxgiOutputDesc desc;
                    if (output.GetDesc(out desc) == 0 && !string.IsNullOrEmpty(desc.DeviceName)) outputs.Add(desc.DeviceName);
                }
                finally { Marshal.ReleaseComObject(output); }
            }
            return outputs;
        }

        private static int EnumAdapters1(IDXGIFactory1 factory, uint index, out object adapter)
        {
            IDXGIAdapter1 found;
            int result = factory.EnumAdapters1(index, out found);
            adapter = found;
            return result;
        }

        [DllImport("dxgi.dll")]
        private static extern int CreateDXGIFactory1(ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object factory);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct DxgiAdapterDesc1
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Description;
            public uint VendorId, DeviceId, SubSysId, Revision;
            public UIntPtr DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory;
            public uint LuidLowPart;
            public int LuidHighPart;
            public uint Flags;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct DxgiOutputDesc
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
            public int Left, Top, Right, Bottom;
            public int AttachedToDesktop;
            public int Rotation;
            public IntPtr Monitor;
        }

        // Only the methods used are declared; _VtblGap entries keep the others' slots.
        [ComImport, Guid("770aae78-f26f-4dba-a829-253c83d1b387"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IDXGIFactory1
        {
            void _VtblGap1_9(); // IDXGIObject (4), IDXGIFactory (5)
            [PreserveSig] int EnumAdapters1(uint index, out IDXGIAdapter1 adapter);
        }

        [ComImport, Guid("c1b6694f-ff09-44a9-b03c-77900a0a1d17"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IDXGIFactory6
        {
            void _VtblGap1_26(); // IDXGIObject (4), IDXGIFactory (5), IDXGIFactory1 (2), 2 (11), 3 (1), 4 (2), 5 (1)
            [PreserveSig] int EnumAdapterByGpuPreference(uint index, int preference, ref Guid riid,
                [MarshalAs(UnmanagedType.IUnknown)] out object adapter);
        }

        [ComImport, Guid("29038f61-3839-4626-91fd-086879011a05"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IDXGIAdapter1
        {
            void _VtblGap1_4(); // IDXGIObject (4)
            [PreserveSig] int EnumOutputs(uint index, out IDXGIOutput output);
            void _VtblGap2_2(); // IDXGIAdapter GetDesc, CheckInterfaceSupport
            [PreserveSig] int GetDesc1(out DxgiAdapterDesc1 desc);
        }

        [ComImport, Guid("ae02eedb-c735-4690-8d52-5a8dc20213aa"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IDXGIOutput
        {
            void _VtblGap1_4(); // IDXGIObject (4)
            [PreserveSig] int GetDesc(out DxgiOutputDesc desc);
        }
    }
}
