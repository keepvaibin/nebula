using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

namespace Nebula.Setup
{
    // Compatibility follows the DLLs' exported ABI contracts, not a hash of
    // every host source file. A renderer-only edit must not recompile the game.
    internal static class ModuleCompatibility
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate IntPtr Manifest();
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate uint Number();

        internal static void CheckVersions(uint game, uint home, uint rso, uint dsp, uint capabilities,
            uint requiredNative, uint requiredRso, uint requiredDsp, uint requiredCapabilities)
        {
            if (game != requiredNative || home != requiredNative || rso != requiredRso ||
                dsp != requiredDsp || (capabilities & requiredCapabilities) != requiredCapabilities)
                throw new InvalidOperationException("This release is not compatible with the installed game modules. " +
                    "The installed modules cannot be reused with this runtime.");
        }

        internal static void Validate(string directory, Dictionary<string, object> contract)
        {
            uint game, home, rso, dsp, capabilities;
            using (var module = new Library(Path.Combine(directory, "RMGE01_game.dll")))
            {
                module.Require("galaxy_module_init", "galaxy_module_entry", "galaxy_lookup_function");
                var ptr = module.Function<Manifest>("galaxy_module_manifest")();
                if (ptr == IntPtr.Zero || Marshal.ReadInt32(ptr) < 64 || Marshal.PtrToStringAnsi(IntPtr.Add(ptr, 8), 6) != "RMGE01")
                    throw new InvalidDataException("The installed game manifest is invalid.");
                game = unchecked((uint)Marshal.ReadInt32(ptr, 4));
            }
            using (var module = new Library(Path.Combine(directory, "RMGE01_home_button.dll")))
            {
                module.Require("galaxy_home_button_rso_try_call", "galaxy_home_button_rso_resume");
                var ptr = module.Function<Manifest>("galaxy_home_button_rso_manifest")();
                if (ptr == IntPtr.Zero || Marshal.ReadInt32(ptr) < 100 || Marshal.PtrToStringAnsi(IntPtr.Add(ptr, 12), 6) != "RMGE01")
                    throw new InvalidDataException("The installed Home manifest is invalid.");
                rso = unchecked((uint)Marshal.ReadInt32(ptr, 4));
                home = unchecked((uint)Marshal.ReadInt32(ptr, 8));
            }
            using (var module = new Library(Path.Combine(directory, "RMGE01_dsp.dll")))
            {
                module.Require("galaxy_rmge01_dsp_entry", "galaxy_rmge01_dsp_entry_expected_iram_sha1",
                    "galaxy_rmge01_dsp_entry_expected_iram_start_address", "galaxy_rmge01_dsp_entry_expected_iram_byte_len");
                dsp = module.Function<Number>("galaxy_rmge01_dsp_entry_generated_probe_contract_version")();
                capabilities = module.Function<Number>("galaxy_rmge01_dsp_entry_generated_probe_capabilities")();
            }
            CheckVersions(game, home, rso, dsp, capabilities,
                (uint)Json.Long(contract, "nativeAbi"), (uint)Json.Long(contract, "rsoAbi"),
                (uint)Json.Long(contract, "dspProbeVersion"), (uint)Json.Long(contract, "dspCapabilities"));
        }

        private sealed class Library : IDisposable
        {
            private readonly IntPtr handle;
            internal Library(string path)
            {
                handle = LoadLibraryEx(Path.GetFullPath(path), IntPtr.Zero, 8);
                if (handle == IntPtr.Zero) throw new InvalidDataException("Could not load " + Path.GetFileName(path) + " (error " + Marshal.GetLastWin32Error() + ").");
            }
            internal void Require(params string[] names) { foreach (var name in names) Address(name); }
            private IntPtr Address(string name)
            {
                var address = GetProcAddress(handle, name);
                if (address == IntPtr.Zero) throw new InvalidDataException("The installed game module is missing " + name + ". No game code was rebuilt.");
                return address;
            }
            internal T Function<T>(string name) where T : class
            { return Marshal.GetDelegateForFunctionPointer(Address(name), typeof(T)) as T; }
            public void Dispose() { FreeLibrary(handle); }
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr LoadLibraryEx(string path, IntPtr reserved, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll")]
        private static extern bool FreeLibrary(IntPtr module);
    }
}
