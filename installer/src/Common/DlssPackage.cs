using System;
using System.Threading;
using Microsoft.Win32;

namespace Nebula
{
    /// <summary>
    /// NVIDIA DLSS, DLSS Frame Generation and DLSS Ray Reconstruction, installed on request from NVIDIA's
    /// Streamline 2.14.1 release on GitHub (VendorPackage). Only the files the
    /// runtime loads are read out of the release zip (58 MB of its 276 MB), and
    /// each must match its SHA-256 below. Nebula ships none of NVIDIA's files.
    /// </summary>
    internal static class DlssPackage
    {
        // Streamline's interposer and plug-ins, NVIDIA's DLSS, DLSS Frame Generation and DLSS Ray
        // Reconstruction (the ray tracer's denoiser) libraries, Reflex (Frame Generation needs it) and the three
        // licenses. An install made before Ray Reconstruction's two files reads as not installed; its download
        // fetches only those.
        public static readonly VendorPackage Package = new VendorPackage(
            "NVIDIA DLSS", "NVIDIA's Streamline 2.14.1 release",
            "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.14.1/streamline-sdk-v2.14.1.zip", 275994000,
            "92c4d954631a1710da86ca3fa8d5034f2b9503838c95fc4ae977ae149319781b", "streamline-2.14.1", "bin/x64/nvngx_dlss.license.txt",
            new[]
            {
                new VendorPackage.Part("bin/x64/sl.interposer.dll", "sl.interposer.dll", "8c87c9499461da561edd529aa9bf7831d67d7b94ebb1c1a5ed54ef4934e1ea4c", 652928, 284550),
                new VendorPackage.Part("bin/x64/sl.common.dll", "sl.common.dll", "82924a8954dd671e09351c5de0eb87ad0eb25b944cc9f9ab955ca1d9950de15d", 843392, 390298),
                new VendorPackage.Part("bin/x64/sl.dlss.dll", "sl.dlss.dll", "73bf52c0cfaa5900a8f3f4a91306e4625e7cca696dfb305aae44c9f97b582e1f", 422016, 194964),
                new VendorPackage.Part("bin/x64/nvngx_dlss.dll", "nvngx_dlss.dll", "3975567b8943c53acce397f2b72380092f84f162d00b0d2c7d08a1025c563983", 58956912, 31123319),
                new VendorPackage.Part("bin/x64/nvngx_dlss.license.txt", "nvngx_dlss.license.txt", "3027f23ca5a46dd9cb8183fbd522983a86f64d7daac5982912bf9f214671f294", 26727, 9256),
                new VendorPackage.Part("license.txt", "streamline.license.txt", "7b6f23e7d6f3ad6292f9308d2b42cdc3d82ae4e9b2abb55f230279d83bedd43d", 1591, 883),
                new VendorPackage.Part("bin/x64/sl.dlss_g.dll", "sl.dlss_g.dll", "f4a6b2b14dcc0b1485989e430d3b4e3a44ac1800b92ba1ad74f476e64fb2b09c", 636032, 290880),
                new VendorPackage.Part("bin/x64/nvngx_dlssg.dll", "nvngx_dlssg.dll", "ff6e90eb78b827927dff5b4ecc6b1c870c2e9bca29ed9f48c7d348cc9e170b82", 7460976, 3717448),
                new VendorPackage.Part("bin/x64/sl.reflex.dll", "sl.reflex.dll", "0ce9725e3e03ea9e7f81d008b57f33ee365973d2e349131c8b1c3e3378fe2db0", 388736, 180983),
                new VendorPackage.Part("bin/x64/sl.pcl.dll", "sl.pcl.dll", "f13d51cfa05f4cd514df2026049e2db8adf359221713170ad386fd499915b582", 360064, 167265),
                new VendorPackage.Part("bin/x64/reflex.license.txt", "reflex.license.txt", "ebf83c07fb3b2939908c3795d887afde3161c89a28ba391724efc784ce1bdabe", 20504, 7617),
                new VendorPackage.Part("bin/x64/sl.dlss_d.dll", "sl.dlss_d.dll", "026b9f4f9ea848f2f5aa0e2024fc6b1c885f57eea5d41078124dba14f873b86b", 437376, 198360),
                new VendorPackage.Part("bin/x64/nvngx_dlssd.dll", "nvngx_dlssd.dll", "4bc7ea5fcb2f32cf86bc2cb072e8d2860914a0be511cbdcb2fc206c1d9d83b80", 48343664, 21565278),
            });

        public static long DownloadBytes { get { return Package.DownloadBytes; } }

        /// <summary>The runtime loads Streamline from here (vendor\streamline-2.14.1 under the data root).</summary>
        public static string Folder { get { return Package.Folder; } }

        public static bool Installed() { return Package.Installed(); }

        /// <summary>NVIDIA's DLSS license (NVIDIA RTX SDKs license), from the install or the release zip.</summary>
        public static string LicenseText(CancellationToken cancel) { return Package.LicenseText(cancel); }

        /// <summary>
        /// Downloads the missing files into <see cref="Folder"/>. progress gets
        /// (bytes read, bytes to read). Files already in place are kept.
        /// </summary>
        public static void Install(Action<long, long> progress, CancellationToken cancel) { Package.Install(progress, cancel); }

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

    /// <summary>
    /// Intel XeSS, installed on request from Intel's XeSS SDK 3.0.2 release on GitHub (VendorPackage): its
    /// signed library and licenses, each checked by its SHA-256. It runs on any GPU that takes its shaders.
    /// </summary>
    internal static class XessPackage
    {
        public static readonly VendorPackage Package = new VendorPackage(
            "Intel XeSS", "Intel's XeSS SDK 3.0.2 release",
            "https://github.com/intel/xess/releases/download/v3.0.2/XeSS_SDK_3.0.2.zip", 76839749, null, "xess-3.0.2", "LICENSE.txt",
            new[]
            {
                new VendorPackage.Part("bin/libxess.dll", "libxess.dll", "251659dd84a3e84de67c886a4186e01f3eca49b00641906fe38bb6b807e5d5b7", 77795704, 59737353),
                new VendorPackage.Part("LICENSE.txt", "LICENSE.txt", "784be42c39a4a4d03cf85d88e15e59d75828fca773ce45b8dbfffbd7eaf208df", 4194, 1970),
                new VendorPackage.Part("third-party-programs.txt", "third-party-programs.txt", "e8d8899dbfc4de0e8ca25560be9358e03c1405d7c956d20a1838c3c709b9b51c", 35788, 9217),
            });
    }
}
