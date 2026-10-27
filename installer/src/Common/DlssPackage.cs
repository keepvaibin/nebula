using System;
using System.IO;
using Microsoft.Win32;

namespace Nebula
{
    /// <summary>
    /// NVIDIA DLSS, installed on request by Nebula Setup from NVIDIA's Streamline
    /// 2.14.1 release on GitHub (Setup/DlssDownload.cs). Each file must match its
    /// SHA-256 below. Nebula ships none of NVIDIA's files. The launcher only reads
    /// whether DLSS is installed.
    /// </summary>
    internal static partial class DlssPackage
    {
        private const string LicenseEntry = "bin/x64/nvngx_dlss.license.txt";

        private sealed class Part
        {
            public readonly string Entry, File, Sha256;
            public readonly long Size, Compressed;
            public Part(string entry, string file, string sha256, long size, long compressed)
            {
                Entry = entry; File = file; Sha256 = sha256; Size = size; Compressed = compressed;
            }
        }

        // Streamline's interposer and plug-ins, NVIDIA's DLSS library and the two licenses.
        private static readonly Part[] Parts =
        {
            new Part("bin/x64/sl.interposer.dll", "sl.interposer.dll", "8c87c9499461da561edd529aa9bf7831d67d7b94ebb1c1a5ed54ef4934e1ea4c", 652928, 284550),
            new Part("bin/x64/sl.common.dll", "sl.common.dll", "82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d", 843392, 390298),
            new Part("bin/x64/sl.dlss.dll", "sl.dlss.dll", "73bf52c0cfaa5900a8f3f4a91306e4625e7cca696dfb305aae44c9f97b582e1f", 422016, 194964),
            new Part("bin/x64/nvngx_dlss.dll", "nvngx_dlss.dll", "3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983", 58956912, 31123319),
            new Part(LicenseEntry, "nvngx_dlss.license.txt", "3027f23ca5a46dd9cb8183fbd522983a86f64d7daac5982912bf9f214671f294", 26727, 9256),
            new Part("license.txt", "streamline.license.txt", "7b6f23e7d6f3ad6292f9308d2b42cdc3d82ae4e9b2abb55f230279d83bedd43d", 1591, 883),
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
    }
}
