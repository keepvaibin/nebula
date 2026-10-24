using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// Display and graphics keys of the runtime's config.ini (render_config.cpp).
    /// The in-game settings menu saves the same file, and a running game takes
    /// the file's changes. Save writes only the settings changed here since Load
    /// (SettingsFile), so a change made in the game meanwhile is kept. Unknown
    /// keys are kept.
    /// </summary>
    internal sealed class GraphicsSettings
    {
        public static readonly string[] DisplayModes = { "Windowed", "Borderless fullscreen" };
        public static readonly string[] Aspects = { "Auto (match the window)", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9" };
        // Their names in config.ini (the runtime's kAspectNames). A number there is an index of the list before 3:2.
        public static readonly string[] AspectNames = { "auto", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9" };
        private static readonly int[] AspectsBefore3To2 = { 0, 1, 3, 4, 5, 6 };
        public static readonly string[] AntiAliasingModes = { "Off", "SMAA", "Auto (SMAA unless supersampled)", "FXAA" };
        public static readonly string[] AntiAliasingQualities = { "Low", "Medium", "High", "Ultra" };
        // MSAA (config msaa_samples 1, 2, 4, 8); off with DLSS, FSR 3 or ray tracing.
        public static readonly string[] MsaaModes = { "Off", "2x", "4x", "8x" };
        // Frame rate: the game's own 60 (Off), Nebula's interpolated frames (Native) or NVIDIA's
        // generated ones (DLSS Frame Generation, with DLSS on an RTX GPU).
        public static readonly string[] InterpolationModes = { "Off", "Native", "DLSS Frame Generation" };
        // Release runtimes are built without FSR 3 (upscaler 2); a saved 2 reads as FSR 1.
        public static readonly string[] Upscalers = { "Bilinear", "AMD FidelityFX FSR 1" };
        // NVIDIA DLSS (upscaler 3) runs from files the player downloads from NVIDIA
        // (DlssPackage); Nebula ships none of them.
        public static string StreamlineDir { get { return DlssPackage.Folder; } }
        public static bool DlssInstalled() { return DlssPackage.Installed(); }
        // The upscaler setting values offered, in list order. DLSS is listed (before it is
        // downloaded too) when the game runs on an NVIDIA RTX GPU.
        public static List<int> UpscalerChoices(string gpu)
        {
            var choices = new List<int> { 0, 1 };
            if (DlssAdapter(gpu)) choices.Add(3);
            return choices;
        }

        /// <summary>
        /// The GPU the game will run on for the gpu setting: the chosen GPU, or the automatic choice when
        /// it is empty or not present (GpuAdapters.Resolve). Null when DXGI cannot list the GPUs.
        /// </summary>
        public static GpuAdapter TargetAdapter(string gpu)
        {
            return GpuAdapters.Resolve(GpuAdapters.List(), gpu);
        }

        /// <summary>
        /// The GPU the game will run on is an NVIDIA RTX GPU. Without DXGI's list, the runtime's record of
        /// its last run on the high-performance GPU (adapter.ini) decides; with no record, any RTX adapter
        /// in the machine.
        /// </summary>
        public static bool DlssAdapter(string gpu)
        {
            GpuAdapter target = TargetAdapter(gpu);
            if (target != null) return target.IsNvidiaRtx;
            var values = AdapterRecord(true);
            string vendor, description;
            if (values != null && values.TryGetValue("vendor_id", out vendor) && values.TryGetValue("description", out description))
            {
                return vendor == "4318" && description.IndexOf("RTX", StringComparison.OrdinalIgnoreCase) >= 0;
            }
            return DlssPackage.RtxAdapterPresent();
        }

        /// <summary>
        /// The GPU the game will run on runs Nebula's ray tracing (DXR 1.1, shader model 6.5, dedicated
        /// memory). The runtime records whether the GPU it ran on does (adapter.ini); a GPU it has not run on
        /// yet does when it is an NVIDIA RTX GPU, and is otherwise offered after its first run.
        /// </summary>
        public static bool RayTracingAdapter(string gpu)
        {
            GpuAdapter target = TargetAdapter(gpu);
            string rt, id;
            if (target != null)
            {
                var record = AdapterRecord(false);
                if (record != null && record.TryGetValue("id", out id) && id == target.Id)
                    return record.TryGetValue("ray_tracing", out rt) && rt == "1";
                return target.IsNvidiaRtx;
            }
            var values = AdapterRecord(true);
            return values != null && values.TryGetValue("ray_tracing", out rt) && rt == "1";
        }

        // adapter.ini; with highPerformanceOnly, only when the game wrote it running with the high-performance GPU
        // preference (its default). Null otherwise.
        private static Dictionary<string, string> AdapterRecord(bool highPerformanceOnly)
        {
            string path = Path.Combine(NebulaPaths.DataRoot, "adapter.ini");
            try
            {
                if (!File.Exists(path)) return null;
                var values = new Dictionary<string, string>();
                foreach (string line in File.ReadAllLines(path))
                {
                    int equals = line.IndexOf('=');
                    if (equals > 0) values[line.Substring(0, equals).Trim()] = line.Substring(equals + 1).Trim();
                }
                string preference;
                return !highPerformanceOnly || (values.TryGetValue("gpu_preference", out preference) && preference != "low")
                    ? values : null;
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return null;
        }

        public static string UpscalerName(int value)
        {
            if (value == 3) return DlssInstalled() ? "NVIDIA DLSS" : "NVIDIA DLSS (download)";
            return Upscalers[Math.Max(0, Math.Min(value, Upscalers.Length - 1))];
        }
        public static readonly string[] DlssQualities = { "DLAA", "Quality", "Balanced", "Performance", "Ultra Performance" };
        // Every upscaler: the first mode renders at the internal resolution, the others at the
        // internal resolution divided by 1.5, 1.7, 2 or 3.
        public static readonly string[] SpatialQualities = { "Native", "Quality", "Balanced", "Performance", "Ultra Performance" };
        private const int TemporalQualityCount = 5;
        public static readonly string[] TextureFilters = { "Trilinear", "2x anisotropic", "4x anisotropic", "8x anisotropic", "16x anisotropic" };
        public static readonly string[] LightingModes = { "Enhanced", "Per pixel", "Per vertex (original)" };
        private static readonly int[] Anisotropy = { 1, 2, 4, 8, 16 };

        /// <summary>
        /// The frame rates an interpolation mode offers, lowest first: 30 and 60 without one; else
        /// the standard rates below the display's, then the display's own (0, "match refresh rate").
        /// With DLSS upscaling native interpolation runs at multiples of 60. The runtime keeps the
        /// same list (host_window_win32.cpp interpolation_targets).
        /// </summary>
        public static List<int> InterpolationTargets(int mode, int refreshHz, bool dlss)
        {
            var targets = new List<int>();
            if (mode == 0) { targets.Add(30); targets.Add(60); return targets; }
            bool sixties = mode == 1 && dlss;
            foreach (int rate in new[] { 120, 144, 165, 180, 240, 360 })
            {
                if (refreshHz != 0 && rate + 1 >= refreshHz) break;
                if ((rate == 180 && mode == 1 && !sixties) || (sixties && rate % 60 != 0)) continue;
                targets.Add(rate);
            }
            bool own = refreshHz == 0 || (refreshHz > 61 && (!sixties || refreshHz % 60 <= 1 || refreshHz % 60 >= 59));
            if (own) targets.Add(0);
            return targets;
        }

        // Width:height of each aspect choice; Auto has none.
        private static readonly int[,] AspectRatios = { { 0, 0 }, { 4, 3 }, { 3, 2 }, { 16, 10 }, { 16, 9 }, { 21, 9 }, { 32, 9 } };
        // Window heights offered for a fixed aspect when the width comes out whole.
        // The runtime's settings_window_sizes uses the same list.
        private static readonly int[] WindowHeights = { 360, 480, 540, 600, 720, 768, 800, 900, 960, 1050, 1080, 1200, 1440, 1536, 1600, 1800, 2160, 2400, 2880, 4320 };

        public int DisplayMode = 1;
        public string Monitor = "";
        // The GPU the game renders on (GpuAdapter.Id); empty chooses automatically.
        public string Gpu = "";
        public int WindowWidth = 1280, WindowHeight = 720;
        public int FullscreenWidth = 0, FullscreenHeight = 0, FullscreenRefresh = 0;
        public int Aspect = 0;
        public int EfbScale = 2;
        public bool MatchOutput = true;
        public bool VSync = false;
        public int FrameRate = 60;
        public int AntiAliasing = 0, Quality = 2, Upscaler = 0, Sharpness = 0, TextureFilter = 0;
        // Index in MsaaModes: 2^Msaa samples per pixel.
        public int Msaa = 0;
        public int TemporalQuality = 1;
        public int FrameGeneration = 0;
        public int SpatialQuality = 0;
        public bool GeneratedMipmaps = false, PerPixelLighting = false, EnhancedLighting = false, AmbientOcclusion = false;
        public bool HudOutputResolution = false, RayTracing = false;
        // Ray tracing quality (config rt_quality): 0 medium (one sample per 2 x 2 pixels of the internal
        // resolution), 1 high (every internal pixel, more rays, reflections shaded where they land), 2 low (one
        // per 3 x 3, single rays: RTX 3050-class cards). Shown Low, Medium, High (RtQualityOrder).
        public int RtQuality = 1;
        public static readonly string[] RtQualities = { "Low (1/3 res)", "Medium (1/2 res)", "High (full res)" };
        public static readonly int[] RtQualityOrder = { 2, 0, 1 };

        public static string ConfigPath { get { return Path.Combine(NebulaPaths.DataRoot, "config.ini"); } }

        // Window sizes of exactly the given aspect that fit maxWidth x maxHeight,
        // smallest first. Empty for Auto.
        public static List<Size> AspectWindowSizes(int aspect, int maxWidth, int maxHeight)
        {
            var sizes = new List<Size>();
            if (aspect <= 0 || aspect >= Aspects.Length) return sizes;
            int ratioWidth = AspectRatios[aspect, 0], ratioHeight = AspectRatios[aspect, 1];
            foreach (int height in WindowHeights)
            {
                if (height * ratioWidth % ratioHeight != 0) continue;
                int width = height * ratioWidth / ratioHeight;
                if (width <= maxWidth && height <= maxHeight) sizes.Add(new Size(width, height));
            }
            return sizes;
        }

        // Index of the smallest size at least as tall as want, or of the largest.
        public static int SnapWindowSize(IList<Size> sizes, Size want)
        {
            for (int i = 0; i < sizes.Count; i++)
                if (sizes[i].Height >= want.Height) return i;
            return sizes.Count - 1;
        }

        public static GraphicsSettings Load()
        {
            var settings = new GraphicsSettings();
            Dictionary<string, string> values;
            try { values = Read(ConfigPath).Item2; }
            catch (IOException) { return settings; }
            catch (UnauthorizedAccessException) { return settings; }
            // Exclusive fullscreen (2) is not offered; it opens borderless.
            settings.DisplayMode = Math.Min(Choice(values, "display_mode", 3, settings.DisplayMode), 1);
            string monitor;
            if (values.TryGetValue("monitor", out monitor)) settings.Monitor = monitor.Trim();
            string gpu;
            if (values.TryGetValue("gpu", out gpu)) settings.Gpu = gpu.Trim().ToLowerInvariant();
            settings.WindowWidth = Choice(values, "window_width", 65536, settings.WindowWidth);
            settings.WindowHeight = Choice(values, "window_height", 65536, settings.WindowHeight);
            settings.FullscreenWidth = Choice(values, "fullscreen_width", 16385, 0);
            settings.FullscreenHeight = Choice(values, "fullscreen_height", 16385, 0);
            settings.FullscreenRefresh = Choice(values, "fullscreen_refresh_hz", 1001, 0);
            settings.Aspect = AspectChoice(values, settings.Aspect);
            settings.EfbScale = Math.Max(1, Choice(values, "efb_scale", 17, settings.EfbScale));
            string follow, vsync;
            if (values.TryGetValue("internal_follow_window", out follow)) settings.MatchOutput = follow.Trim() != "0";
            if (values.TryGetValue("vsync_interval", out vsync)) settings.VSync = vsync.Trim() != "0";
            string fps;
            double rate;
            if (values.TryGetValue("max_fps", out fps) &&
                double.TryParse(fps.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out rate) &&
                (rate == 0 || (rate >= 1 && rate <= 1000))) settings.FrameRate = (int)Math.Round(rate);
            settings.AntiAliasing = Choice(values, "aa", AntiAliasingModes.Length, settings.AntiAliasing);
            settings.Quality = Choice(values, "smaa_quality", AntiAliasingQualities.Length, settings.Quality);
            int samples = Choice(values, "msaa_samples", 9, 1 << settings.Msaa);
            settings.Msaa = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
            int upscaler = Choice(values, "upscaler", 4, settings.Upscaler);
            settings.Upscaler = upscaler == 2 ? 1 : upscaler;
            settings.TemporalQuality = Choice(values, "temporal_quality", TemporalQualityCount, settings.TemporalQuality);
            settings.FrameGeneration = Choice(values, "frame_generation", 6, settings.FrameGeneration);
            settings.SpatialQuality = Choice(values, "spatial_quality", TemporalQualityCount, settings.SpatialQuality);
            settings.Sharpness = Math.Min(Choice(values, "sharpness", 101, settings.Sharpness), 100);
            int anisotropy = Choice(values, "anisotropic_filtering", 17, -1);
            if (anisotropy >= 1)
            {
                settings.TextureFilter = 0;
                for (int i = 0; i < Anisotropy.Length; i++) if (Anisotropy[i] <= anisotropy) settings.TextureFilter = i;
            }
            string mipmaps, lighting, enhanced, hud, rt;
            if (values.TryGetValue("enhanced_mipmaps", out mipmaps)) settings.GeneratedMipmaps = mipmaps.Trim() != "0";
            if (values.TryGetValue("per_pixel_lighting", out lighting)) settings.PerPixelLighting = lighting.Trim() != "0";
            if (values.TryGetValue("enhanced_lighting", out enhanced)) settings.EnhancedLighting = enhanced.Trim() != "0";
            string occlusion;
            if (values.TryGetValue("ambient_occlusion", out occlusion)) settings.AmbientOcclusion = occlusion.Trim() != "0";
            if (values.TryGetValue("hud_output_resolution", out hud)) settings.HudOutputResolution = hud.Trim() != "0";
            if (values.TryGetValue("ray_tracing_enabled", out rt)) settings.RayTracing = rt.Trim() != "0";
            string quality;
            if (values.TryGetValue("rt_quality", out quality))
                settings.RtQuality = quality.Trim() == "0" ||
                                     quality.Trim().Equals("medium", StringComparison.OrdinalIgnoreCase) ? 0
                                   : quality.Trim() == "2" ||
                                     quality.Trim().Equals("low", StringComparison.OrdinalIgnoreCase) ? 2 : 1;
            settings.read = SettingsFile.Snapshot(settings.Values());
            return settings;
        }

        // The values as Load read them (or as the last Save wrote them).
        private Dictionary<string, string> read;

        private List<KeyValuePair<string, string>> Values()
        {
            Func<int, string> n = value => value.ToString(CultureInfo.InvariantCulture);
            return new List<KeyValuePair<string, string>>
            {
                new KeyValuePair<string, string>("display_mode", n(DisplayMode)),
                new KeyValuePair<string, string>("monitor", Monitor ?? ""),
                new KeyValuePair<string, string>("gpu", Gpu ?? ""),
                new KeyValuePair<string, string>("window_width", n(WindowWidth)),
                new KeyValuePair<string, string>("window_height", n(WindowHeight)),
                new KeyValuePair<string, string>("fullscreen_width", n(FullscreenWidth)),
                new KeyValuePair<string, string>("fullscreen_height", n(FullscreenHeight)),
                new KeyValuePair<string, string>("fullscreen_refresh_hz", n(FullscreenRefresh)),
                new KeyValuePair<string, string>("aspect", AspectNames[Math.Max(0, Math.Min(Aspect, AspectNames.Length - 1))]),
                new KeyValuePair<string, string>("efb_scale", n(EfbScale)),
                new KeyValuePair<string, string>("internal_follow_window", MatchOutput ? "1" : "0"),
                new KeyValuePair<string, string>("vsync_interval", VSync ? "1" : "0"),
                new KeyValuePair<string, string>("max_fps", n(FrameRate)),
                new KeyValuePair<string, string>("aa", n(AntiAliasing)),
                new KeyValuePair<string, string>("smaa_quality", n(Quality)),
                new KeyValuePair<string, string>("msaa_samples", n(1 << Math.Max(0, Math.Min(Msaa, 3)))),
                new KeyValuePair<string, string>("upscaler", n(Upscaler)),
                new KeyValuePair<string, string>("temporal_quality", n(TemporalQuality)),
                new KeyValuePair<string, string>("frame_generation", n(FrameGeneration)),
                new KeyValuePair<string, string>("spatial_quality", n(SpatialQuality)),
                new KeyValuePair<string, string>("sharpness", n(Sharpness)),
                new KeyValuePair<string, string>("anisotropic_filtering", n(Anisotropy[Math.Max(0, Math.Min(TextureFilter, Anisotropy.Length - 1))])),
                new KeyValuePair<string, string>("enhanced_mipmaps", GeneratedMipmaps ? "1" : "0"),
                new KeyValuePair<string, string>("per_pixel_lighting", PerPixelLighting ? "1" : "0"),
                new KeyValuePair<string, string>("enhanced_lighting", EnhancedLighting ? "1" : "0"),
                new KeyValuePair<string, string>("ambient_occlusion", AmbientOcclusion ? "1" : "0"),
                new KeyValuePair<string, string>("hud_output_resolution", HudOutputResolution ? "1" : "0"),
                new KeyValuePair<string, string>("ray_tracing_enabled", RayTracing ? "1" : "0"),
                new KeyValuePair<string, string>("rt_quality", RtQuality == 0 ? "medium" : RtQuality == 2 ? "low" : "high"),
            };
        }

        public void Save()
        {
            var values = Values();
            SettingsFile.Save(ConfigPath, values, read);
            read = SettingsFile.Snapshot(values);
        }

        private static Tuple<List<string>, Dictionary<string, string>> Read(string path)
        {
            var lines = new List<string>();
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (File.Exists(path))
            {
                foreach (string line in File.ReadAllLines(path))
                {
                    lines.Add(line);
                    int equals = line.IndexOf('=');
                    string key = equals > 0 ? line.Substring(0, equals).Trim() : "";
                    if (key.Length != 0 && !key.StartsWith("#") && !key.StartsWith(";")) values[key] = line.Substring(equals + 1);
                }
            }
            return Tuple.Create(lines, values);
        }

        private static int AspectChoice(Dictionary<string, string> values, int fallback)
        {
            string text;
            if (!values.TryGetValue("aspect", out text)) return fallback;
            text = text.Trim();
            int index;
            if (int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out index))
                return index >= 0 && index < AspectsBefore3To2.Length ? AspectsBefore3To2[index] : fallback;
            int named = Array.FindIndex(AspectNames, name => string.Equals(name, text, StringComparison.OrdinalIgnoreCase));
            return named >= 0 ? named : fallback;
        }

        private static int Choice(Dictionary<string, string> values, string key, int count, int fallback)
        {
            string text;
            int value;
            if (!values.TryGetValue(key, out text) ||
                !int.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out value) ||
                value < 0 || value >= count) return fallback;
            return value;
        }
    }
}
