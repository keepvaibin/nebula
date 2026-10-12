using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// Image-quality keys of the runtime's config.ini (render_config.cpp). The
    /// in-game settings overlay saves the same file, so either side's last
    /// change is what the next launch shows. Unknown keys are kept.
    /// </summary>
    internal sealed class GraphicsSettings
    {
        public static readonly string[] AntiAliasingModes = { "Off", "SMAA", "Auto (SMAA unless supersampled)" };
        public static readonly string[] AntiAliasingQualities = { "Low", "Medium", "High", "Ultra" };
        public static readonly string[] Upscalers = { "Bilinear", "AMD FidelityFX FSR 1" };
        public static readonly string[] TextureFilters = { "Trilinear", "2x anisotropic", "4x anisotropic", "8x anisotropic", "16x anisotropic" };
        public static readonly string[] LightingModes = { "Per pixel (enhanced)", "Per vertex (original)" };
        private static readonly int[] Anisotropy = { 1, 2, 4, 8, 16 };

        public int AntiAliasing = 2, Quality = 2, Upscaler = 1, Sharpness = 20, TextureFilter = 3;
        public bool GeneratedMipmaps = true, PerPixelLighting = true, HudOutputResolution = false;

        public static string ConfigPath { get { return Path.Combine(NebulaPaths.DataRoot, "config.ini"); } }

        public static GraphicsSettings Load()
        {
            var settings = new GraphicsSettings();
            Dictionary<string, string> values;
            try { values = Read(ConfigPath).Item2; }
            catch (IOException) { return settings; }
            catch (UnauthorizedAccessException) { return settings; }
            settings.AntiAliasing = Choice(values, "aa", AntiAliasingModes.Length, settings.AntiAliasing);
            settings.Quality = Choice(values, "smaa_quality", AntiAliasingQualities.Length, settings.Quality);
            settings.Upscaler = Choice(values, "upscaler", Upscalers.Length, settings.Upscaler);
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
            return settings;
        }

        /// <param name="followWindow">Internal resolution follows window resizes ("Match Output").</param>
        public void Save(bool followWindow)
        {
            var file = Read(ConfigPath);
            var lines = file.Item1;
            Set(lines, "aa", AntiAliasing);
            Set(lines, "smaa_quality", Quality);
            Set(lines, "upscaler", Upscaler);
            Set(lines, "sharpness", Sharpness);
            Set(lines, "anisotropic_filtering", Anisotropy[Math.Max(0, Math.Min(TextureFilter, Anisotropy.Length - 1))]);
            Set(lines, "enhanced_mipmaps", GeneratedMipmaps ? 1 : 0);
            Set(lines, "per_pixel_lighting", PerPixelLighting ? 1 : 0);
            Set(lines, "hud_output_resolution", HudOutputResolution ? 1 : 0);
            Set(lines, "internal_follow_window", followWindow ? 1 : 0);
            Directory.CreateDirectory(Path.GetDirectoryName(ConfigPath));
            string temp = ConfigPath + ".tmp";
            File.WriteAllText(temp, string.Join("\n", lines) + "\n", new UTF8Encoding(false));
            if (File.Exists(ConfigPath)) File.Replace(temp, ConfigPath, null);
            else File.Move(temp, ConfigPath);
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

        private static void Set(List<string> lines, string key, int value)
        {
            string line = key + "=" + value.ToString(CultureInfo.InvariantCulture);
            for (int i = 0; i < lines.Count; i++)
            {
                int equals = lines[i].IndexOf('=');
                if (equals > 0 && string.Equals(lines[i].Substring(0, equals).Trim(), key, StringComparison.OrdinalIgnoreCase))
                {
                    lines[i] = line;
                    return;
                }
            }
            lines.Add(line);
        }
    }
}
