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
        public static readonly string[] Aspects = { "Auto (match the window)", "4:3", "16:10", "16:9", "21:9", "32:9" };
        public static readonly string[] AntiAliasingModes = { "Off", "SMAA", "Auto (SMAA unless supersampled)", "FXAA" };
        public static readonly string[] AntiAliasingQualities = { "Low", "Medium", "High", "Ultra" };
        // Release runtimes are built without FSR 3 (upscaler 2); a saved 2 reads as FSR 1.
        public static readonly string[] Upscalers = { "Bilinear", "AMD FidelityFX FSR 1" };
        // NVIDIA DLSS (upscaler 3) runs from files the player downloads from NVIDIA
        // (DlssPackage); Nebula ships none of them.
        public static string StreamlineDir { get { return DlssPackage.Folder; } }
        public static bool DlssInstalled() { return DlssPackage.Installed(); }
        // The upscaler setting values offered, in list order. DLSS is listed (before it is
        // downloaded too) when the game runs on an NVIDIA RTX GPU.
        public static List<int> UpscalerChoices()
        {
            var choices = new List<int> { 0, 1 };
            if (DlssAdapter()) choices.Add(3);
            return choices;
        }

        /// <summary>
        /// The GPU the game runs on is an NVIDIA RTX GPU. The runtime records its adapter in
        /// adapter.ini beside config.ini with the GPU preference it started with; a record from
        /// another preference (another package) or no record yet falls back to this package's
        /// preference: the integrated GPU never, otherwise any RTX adapter in the machine.
        /// </summary>
        public static bool DlssAdapter()
        {
            bool lowPower = PackagePrefersIntegratedGpu();
            string path = Path.Combine(NebulaPaths.DataRoot, "adapter.ini");
            try
            {
                if (File.Exists(path))
                {
                    var values = new Dictionary<string, string>();
                    foreach (string line in File.ReadAllLines(path))
                    {
                        int equals = line.IndexOf('=');
                        if (equals > 0) values[line.Substring(0, equals).Trim()] = line.Substring(equals + 1).Trim();
                    }
                    string preference, vendor, description;
                    if (values.TryGetValue("gpu_preference", out preference) &&
                        (preference == "low") == lowPower &&
                        values.TryGetValue("vendor_id", out vendor) && values.TryGetValue("description", out description))
                    {
                        return vendor == "4318" && description.IndexOf("RTX", StringComparison.OrdinalIgnoreCase) >= 0;
                    }
                }
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return !lowPower && DlssPackage.RtxAdapterPresent();
        }

        /// <summary>runtime-env.json beside the launcher selects the integrated GPU (GALAXY_GPU_PREFERENCE=low).</summary>
        public static bool PackagePrefersIntegratedGpu()
        {
            try
            {
                string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "runtime-env.json");
                if (!File.Exists(path)) return false;
                foreach (var entry in (System.Collections.IList)Json.ParseAny(File.ReadAllText(path)))
                {
                    var pair = entry as Dictionary<string, object>;
                    if (pair != null && Json.Str(pair, "name") == "GALAXY_GPU_PREFERENCE")
                        return string.Equals(Json.Str(pair, "value"), "low", StringComparison.OrdinalIgnoreCase);
                }
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            catch (ArgumentException) { }
            catch (InvalidCastException) { }
            return false;
        }
        public static string UpscalerName(int value)
        {
            if (value == 3) return DlssInstalled() ? "NVIDIA DLSS" : "NVIDIA DLSS (download)";
            return Upscalers[Math.Max(0, Math.Min(value, Upscalers.Length - 1))];
        }
        public static readonly string[] DlssQualities = { "DLAA", "Quality", "Balanced", "Performance", "Ultra Performance" };
        private const int TemporalQualityCount = 5;
        public static readonly string[] TextureFilters = { "Trilinear", "2x anisotropic", "4x anisotropic", "8x anisotropic", "16x anisotropic" };
        public static readonly string[] LightingModes = { "Per pixel (enhanced)", "Per vertex (original)" };
        private static readonly int[] Anisotropy = { 1, 2, 4, 8, 16 };
        private static readonly int[] StandardFrameRates = { 30, 60, 120, 144, 165, 240, 360 };

        // The runtime's frame_rate_options: standard rates the display can
        // show, its own rate when that is not one of them, then 0 (unlimited).
        public static List<int> FrameRates(int refreshHz)
        {
            var rates = new List<int>();
            bool listed = false;
            foreach (int rate in StandardFrameRates)
            {
                if (refreshHz != 0 && rate > refreshHz + 1) break;
                rates.Add(rate);
                listed = listed || (refreshHz + 1 >= rate && refreshHz <= rate + 1);
            }
            if (refreshHz > 30 && !listed)
            {
                int at = rates.FindIndex(r => r > refreshHz);
                rates.Insert(at < 0 ? rates.Count : at, refreshHz);
            }
            rates.Add(0);
            return rates;
        }
        // Width:height of each aspect choice; Auto has none.
        private static readonly int[,] AspectRatios = { { 0, 0 }, { 4, 3 }, { 16, 10 }, { 16, 9 }, { 21, 9 }, { 32, 9 } };
        // Window heights offered for a fixed aspect when the width comes out whole.
        // The runtime's settings_window_sizes uses the same list.
        private static readonly int[] WindowHeights = { 360, 480, 540, 600, 720, 768, 800, 900, 960, 1050, 1080, 1200, 1440, 1536, 1600, 1800, 2160, 2400, 2880, 4320 };

        public int DisplayMode = 1;
        public string Monitor = "";
        public int WindowWidth = 1280, WindowHeight = 720;
        public int FullscreenWidth = 0, FullscreenHeight = 0, FullscreenRefresh = 0;
        public int Aspect = 0;
        public int EfbScale = 2;
        public bool MatchOutput = true;
        public bool VSync = false;
        public int FrameRate = 60;
        public int AntiAliasing = 0, Quality = 2, Upscaler = 0, Sharpness = 0, TextureFilter = 0;
        public int TemporalQuality = 1;
        public bool GeneratedMipmaps = false, PerPixelLighting = false, HudOutputResolution = false;

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
            settings.WindowWidth = Choice(values, "window_width", 65536, settings.WindowWidth);
            settings.WindowHeight = Choice(values, "window_height", 65536, settings.WindowHeight);
            settings.FullscreenWidth = Choice(values, "fullscreen_width", 16385, 0);
            settings.FullscreenHeight = Choice(values, "fullscreen_height", 16385, 0);
            settings.FullscreenRefresh = Choice(values, "fullscreen_refresh_hz", 1001, 0);
            settings.Aspect = Choice(values, "aspect", Aspects.Length, settings.Aspect);
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
            int upscaler = Choice(values, "upscaler", 4, settings.Upscaler);
            settings.Upscaler = upscaler == 2 ? 1 : upscaler;
            settings.TemporalQuality = Choice(values, "temporal_quality", TemporalQualityCount, settings.TemporalQuality);
            settings.Sharpness = Math.Min(Choice(values, "sharpness", 101, settings.Sharpness), 100);
            int anisotropy = Choice(values, "anisotropic_filtering", 17, -1);
            if (anisotropy >= 1)
            {
                settings.TextureFilter = 0;
                for (int i = 0; i < Anisotropy.Length; i++) if (Anisotropy[i] <= anisotropy) settings.TextureFilter = i;
            }
            string mipmaps, lighting, hud;
            if (values.TryGetValue("enhanced_mipmaps", out mipmaps)) settings.GeneratedMipmaps = mipmaps.Trim() != "0";
            if (values.TryGetValue("per_pixel_lighting", out lighting)) settings.PerPixelLighting = lighting.Trim() != "0";
            if (values.TryGetValue("hud_output_resolution", out hud)) settings.HudOutputResolution = hud.Trim() != "0";
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
                new KeyValuePair<string, string>("window_width", n(WindowWidth)),
                new KeyValuePair<string, string>("window_height", n(WindowHeight)),
                new KeyValuePair<string, string>("fullscreen_width", n(FullscreenWidth)),
                new KeyValuePair<string, string>("fullscreen_height", n(FullscreenHeight)),
                new KeyValuePair<string, string>("fullscreen_refresh_hz", n(FullscreenRefresh)),
                new KeyValuePair<string, string>("aspect", n(Aspect)),
                new KeyValuePair<string, string>("efb_scale", n(EfbScale)),
                new KeyValuePair<string, string>("internal_follow_window", MatchOutput ? "1" : "0"),
                new KeyValuePair<string, string>("vsync_interval", VSync ? "1" : "0"),
                new KeyValuePair<string, string>("max_fps", n(FrameRate)),
                new KeyValuePair<string, string>("aa", n(AntiAliasing)),
                new KeyValuePair<string, string>("smaa_quality", n(Quality)),
                new KeyValuePair<string, string>("upscaler", n(Upscaler)),
                new KeyValuePair<string, string>("temporal_quality", n(TemporalQuality)),
                new KeyValuePair<string, string>("sharpness", n(Sharpness)),
                new KeyValuePair<string, string>("anisotropic_filtering", n(Anisotropy[Math.Max(0, Math.Min(TextureFilter, Anisotropy.Length - 1))])),
                new KeyValuePair<string, string>("enhanced_mipmaps", GeneratedMipmaps ? "1" : "0"),
                new KeyValuePair<string, string>("per_pixel_lighting", PerPixelLighting ? "1" : "0"),
                new KeyValuePair<string, string>("hud_output_resolution", HudOutputResolution ? "1" : "0"),
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
