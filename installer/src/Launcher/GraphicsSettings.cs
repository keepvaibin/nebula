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
        public static readonly string[] DisplayModes = { NebulaText.Of("Windowed"), NebulaText.Of("Borderless fullscreen") };
        public static readonly string[] Aspects = { NebulaText.Of("Auto (match the window)"), "4:3", "3:2", "16:10", "16:9", "21:9", "32:9" };
        // Their names in config.ini (the runtime's kAspectNames). A number there is an index of the list before 3:2.
        public static readonly string[] AspectNames = { "auto", "4:3", "3:2", "16:10", "16:9", "21:9", "32:9" };
        private static readonly int[] AspectsBefore3To2 = { 0, 1, 3, 4, 5, 6 };
        public static readonly string[] AntiAliasingModes = { NebulaText.Of("Off"), "SMAA", NebulaText.Of("Auto (SMAA unless supersampled)"), "FXAA" };
        public static readonly string[] AntiAliasingQualities = { NebulaText.Of("Low"), NebulaText.Of("Medium"), NebulaText.Of("High"), NebulaText.Of("Ultra") };
        // MSAA (config msaa_samples 1, 2, 4, 8); off with a temporal upscaler or ray tracing.
        public static readonly string[] MsaaModes = { NebulaText.Of("Off"), "2x", "4x", "8x" };
        // Frame rate: the game's own 60 (Off), Nebula's interpolated frames (Native), NVIDIA's generated ones (DLSS
        // Frame Generation, with DLSS on an RTX GPU: config frame_generation 1) or AMD's (FSR frame generation, with
        // FSR 3 or XeSS on a display of 120 Hz or more: frame_generation 2).
        public static readonly string[] InterpolationModes = { NebulaText.Of("Off"), NebulaText.Of("Native"), NebulaText.Of("DLSS Frame Generation"), NebulaText.Of("FSR frame generation") };
        // Upscaling by its config number: 0 off, 1 FSR 1, 2 FSR 3, 3 DLSS, 4 XeSS. SGSR 2 (5) is the Vulkan
        // renderer's; this one runs FSR 3 for it.
        public static readonly string[] Upscalers = { NebulaText.Of("Off"), "AMD FidelityFX FSR 1", "AMD FidelityFX FSR 3", "NVIDIA DLSS", "Intel XeSS" };
        // NVIDIA DLSS (upscaler 3) and Intel XeSS (4) run from files the player downloads from the vendor
        // (DlssPackage, XessPackage); Nebula ships none of them.
        public static string StreamlineDir { get { return DlssPackage.Folder; } }
        public static bool DlssInstalled() { return DlssPackage.Installed(); }
        public static bool XessInstalled() { return XessPackage.Package.Installed(); }
        // The upscaler setting values offered, in list order. DLSS is listed (before it is
        // downloaded too) when the game runs on an NVIDIA RTX GPU; XeSS runs on any GPU.
        public static List<int> UpscalerChoices(string gpu) { return UpscalerChoices(GpuAdapters.List(), gpu); }

        /// <summary>The same, with the GPUs already listed (the settings window lists them once).</summary>
        public static List<int> UpscalerChoices(List<GpuAdapter> adapters, string gpu)
        {
            var choices = new List<int> { 0, 1, 2 };
            if (DlssAdapter(adapters, gpu)) choices.Add(3);
            choices.Add(4);
            return choices;
        }
        // FSR 3, DLSS, XeSS and SGSR 2 rebuild the image from several frames: they antialias it themselves and set
        // its resolution by their mode (temporal_quality).
        public static bool TemporalUpscaler(int upscaler) { return upscaler >= 2; }
        // The upscaler this renderer runs for a saved one it does not offer: a temporal one's place goes to FSR 3.
        public static int OfferedUpscaler(List<int> choices, int upscaler)
        {
            return choices.Contains(upscaler) ? upscaler : TemporalUpscaler(upscaler) ? 2 : 1;
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
        public static bool DlssAdapter(string gpu) { return DlssAdapter(GpuAdapters.List(), gpu); }

        public static bool DlssAdapter(List<GpuAdapter> adapters, string gpu)
        {
            GpuAdapter target = GpuAdapters.Resolve(adapters, gpu);
            if (target != null) return target.IsNvidiaRtx;
            var values = AdapterRecord();
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
        public static bool RayTracingAdapter(string gpu) { return RayTracingAdapter(GpuAdapters.List(), gpu); }

        public static bool RayTracingAdapter(List<GpuAdapter> adapters, string gpu)
        {
            GpuAdapter target = GpuAdapters.Resolve(adapters, gpu);
            string rt, id;
            if (target != null)
            {
                var record = AdapterRecord();
                if (record != null && record.TryGetValue("id", out id) && id == target.Id)
                    return record.TryGetValue("ray_tracing", out rt) && rt == "1";
                return target.IsNvidiaRtx;
            }
            var values = AdapterRecord();
            return values != null && values.TryGetValue("ray_tracing", out rt) && rt == "1";
        }

        // adapter.ini, null when there is none.
        private static Dictionary<string, string> AdapterRecord()
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
                return values;
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return null;
        }

        public static string UpscalerName(int value)
        {
            if (value == 3) return DlssInstalled() ? "NVIDIA DLSS" : NebulaText.Of("NVIDIA DLSS (download)");
            if (value == 4) return XessInstalled() ? "Intel XeSS" : NebulaText.Of("Intel XeSS (download)");
            return Upscalers[Math.Max(0, Math.Min(value, Upscalers.Length - 1))];
        }
        // The upscaler's mode by its config number (temporal_quality for FSR 3, DLSS and XeSS, spatial_quality for
        // FSR 1): the display's resolution divided by 1 (native), 1.5, 1.7, 2, 3 or 1.3 (ultra quality, which came
        // last). Listed sharpest first, as the game's menu lists them; DLSS has no ultra quality mode.
        private static readonly string[] QualityNames = { NebulaText.Of("Native"), NebulaText.Of("Quality"), NebulaText.Of("Balanced"), NebulaText.Of("Performance"), NebulaText.Of("Ultra Performance"), NebulaText.Of("Ultra Quality") };
        public static readonly int[] QualityOrder = { 0, 5, 1, 2, 3, 4 };
        public static readonly int[] DlssQualityOrder = { 0, 1, 2, 3, 4 };
        public static string QualityName(int upscaler, int value)
        {
            if (value == 0) return upscaler == 3 ? "DLAA" : TemporalUpscaler(upscaler) ? NebulaText.Of("Native AA") : QualityNames[0];
            return QualityNames[Math.Max(0, Math.Min(value, QualityNames.Length - 1))];
        }
        private const int TemporalQualityCount = 6;
        public static readonly string[] TextureFilters = { NebulaText.Of("Trilinear"), NebulaText.Of("2x anisotropic"), NebulaText.Of("4x anisotropic"), NebulaText.Of("8x anisotropic"), NebulaText.Of("16x anisotropic") };
        public static readonly string[] LightingModes = { NebulaText.Of("Enhanced"), NebulaText.Of("Per pixel"), NebulaText.Of("Per vertex (original)") };
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
            // AMD's makes one frame between each two of the game's 60 (settings_model.cpp interpolation_targets).
            if (mode == 3) { targets.Add(120); return targets; }
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
        // The runtime's defaults (render_config.h) for a file without the keys.
        public int WindowWidth = 854, WindowHeight = 480;
        public int Aspect = 0;
        public bool VSync = false;
        public int FrameRate = 60;
        public int AntiAliasing = 0, Quality = 2, Upscaler = 0, Sharpness = 0, TextureFilter = 0;
        // Index in MsaaModes: 2^Msaa samples per pixel.
        public int Msaa = 0;
        public int TemporalQuality = 1;
        public int FrameGeneration = 0;
        public static readonly int[] InternalHeights = { 0, 144, 240, 360, 480, 720, 1080, 1440, 2160 };
        public static readonly string[] InternalResolutions = { "Match display", "144p", "240p", "360p", "480p", "720p", "1080p", "1440p", "2160p (4K)" };
        public int InternalHeight = 0;
        public int SpatialQuality = 0;
        public bool GeneratedMipmaps = false, PerPixelLighting = false, EnhancedLighting = false, AmbientOcclusion = false;
        public bool HudOutputResolution = false, RayTracing = false;
        // HDR (only while Windows shows the display in HDR) and its paper white, 80 to 500 nits.
        public bool Hdr = false;
        public int PaperWhite = 200;
        public const int PaperWhiteMin = 80, PaperWhiteMax = 500;
        // Ambient occlusion's kind when on: SSAO, else GTAO (ambient_occlusion_ssao).
        public bool AmbientOcclusionSsao = false;
        public bool CurvedSurfaces = true;
        // Bloom strength, percent in steps of ten, 0 off (render_config.h kMaxBloom).
        public int Bloom = 100;
        public const int BloomMax = 200;
        // Texture packs Off, On (as textures appear) or Preload (all at once); dumping the game's textures for a pack.
        public bool TexturePacks = true, TexturePackPreload = false, TextureDump = false;
        public static readonly string[] OcclusionModes = { NebulaText.Of("Off"), "SSAO", "GTAO" };
        public static readonly string[] TexturePackModes = { NebulaText.Of("Off"), NebulaText.Of("On"), NebulaText.Of("Preload") };

        /// <summary>Ambient occlusion as one choice: 0 off, 1 SSAO, 2 GTAO (the game's menu's order).</summary>
        public int OcclusionMode
        {
            get { return !AmbientOcclusion ? 0 : AmbientOcclusionSsao ? 1 : 2; }
            set { AmbientOcclusion = value != 0; AmbientOcclusionSsao = value == 1; }
        }

        /// <summary>Texture packs as one choice: 0 off, 1 on, 2 preload.</summary>
        public int TexturePackMode
        {
            get { return !TexturePacks ? 0 : TexturePackPreload ? 2 : 1; }
            set { TexturePacks = value != 0; TexturePackPreload = value == 2; }
        }
        // Ray tracing quality (config rt_quality): 0 medium (one sample per 2 x 2 pixels of the internal
        // resolution), 1 high (every internal pixel, more rays, reflections shaded where they land), 2 low (one
        // per 3 x 3, single rays: RTX 3050-class cards). Shown Low, Medium, High (RtQualityOrder).
        public int RtQuality = 1;
        public static readonly string[] RtQualities = { NebulaText.Of("Low (1/3 res)"), NebulaText.Of("Medium (1/2 res)"), NebulaText.Of("High (full res)") };
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
            settings.Aspect = AspectChoice(values, settings.Aspect);
            string vsync;
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
            settings.Upscaler = Choice(values, "upscaler", 6, settings.Upscaler);
            settings.TemporalQuality = Choice(values, "temporal_quality", TemporalQualityCount, settings.TemporalQuality);
            settings.FrameGeneration = Choice(values, "frame_generation", 6, settings.FrameGeneration);
            settings.SpatialQuality = Choice(values, "spatial_quality", TemporalQualityCount, settings.SpatialQuality);
            settings.InternalHeight = Choice(values, "internal_height", 2161, 0);
            if (Array.IndexOf(InternalHeights, settings.InternalHeight) < 0) settings.InternalHeight = 0;
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
            settings.Hdr = Flag(values, "hdr", settings.Hdr);
            settings.PaperWhite = Math.Max(PaperWhiteMin, Math.Min(Number(values, "paper_white", settings.PaperWhite), PaperWhiteMax));
            settings.AmbientOcclusionSsao = Flag(values, "ambient_occlusion_ssao", settings.AmbientOcclusionSsao);
            settings.CurvedSurfaces = Flag(values, "curved_surfaces", settings.CurvedSurfaces);
            settings.Bloom = Math.Max(0, Math.Min(Number(values, "bloom", settings.Bloom), BloomMax));
            settings.TexturePacks = Flag(values, "texture_packs", settings.TexturePacks);
            settings.TexturePackPreload = Flag(values, "texture_pack_preload", settings.TexturePackPreload);
            settings.TextureDump = Flag(values, "texture_dump", settings.TextureDump);
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
                new KeyValuePair<string, string>("aspect", AspectNames[Math.Max(0, Math.Min(Aspect, AspectNames.Length - 1))]),
                new KeyValuePair<string, string>("vsync_interval", VSync ? "1" : "0"),
                new KeyValuePair<string, string>("max_fps", n(FrameRate)),
                new KeyValuePair<string, string>("aa", n(AntiAliasing)),
                new KeyValuePair<string, string>("smaa_quality", n(Quality)),
                new KeyValuePair<string, string>("msaa_samples", n(1 << Math.Max(0, Math.Min(Msaa, 3)))),
                new KeyValuePair<string, string>("upscaler", n(Upscaler)),
                new KeyValuePair<string, string>("temporal_quality", n(TemporalQuality)),
                new KeyValuePair<string, string>("frame_generation", n(FrameGeneration)),
                new KeyValuePair<string, string>("internal_height", n(InternalHeight)),
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
                new KeyValuePair<string, string>("hdr", Hdr ? "1" : "0"),
                new KeyValuePair<string, string>("paper_white", n(Math.Max(PaperWhiteMin, Math.Min(PaperWhite, PaperWhiteMax)))),
                new KeyValuePair<string, string>("ambient_occlusion_ssao", AmbientOcclusionSsao ? "1" : "0"),
                new KeyValuePair<string, string>("curved_surfaces", CurvedSurfaces ? "1" : "0"),
                new KeyValuePair<string, string>("bloom", n(Math.Max(0, Math.Min(Bloom, BloomMax)))),
                new KeyValuePair<string, string>("texture_packs", TexturePacks ? "1" : "0"),
                new KeyValuePair<string, string>("texture_pack_preload", TexturePackPreload ? "1" : "0"),
                new KeyValuePair<string, string>("texture_dump", TextureDump ? "1" : "0"),
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

        // A bool as the runtime reads it (parse_bool): 0/1, false/true, off/on, no/yes; anything else keeps the fallback.
        private static bool Flag(Dictionary<string, string> values, string key, bool fallback)
        {
            string text;
            if (!values.TryGetValue(key, out text)) return fallback;
            switch (text.Trim().ToLowerInvariant())
            {
                case "1": case "true": case "on": case "yes": return true;
                case "0": case "false": case "off": case "no": return false;
                default: return fallback;
            }
        }

        private static int Number(Dictionary<string, string> values, string key, int fallback)
        {
            string text;
            int value;
            return values.TryGetValue(key, out text) && int.TryParse(text.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out value) ? value : fallback;
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
