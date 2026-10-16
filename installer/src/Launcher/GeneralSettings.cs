using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// General keys of the runtime's runtime_settings.ini (runtime_settings.cpp). The in-game GENERAL page
    /// saves the same file; unknown keys are kept.
    /// </summary>
    internal sealed class GeneralSettings
    {
        public static readonly string[] TextSpeeds = { "1x (original)", "2x", "3x" };

        /// <summary>The game's three languages, each in its own name, and their runtime_settings.ini values.</summary>
        public static readonly string[] Languages = { "English", "Français", "Español" };
        private static readonly string[] LanguageKeys = { "english", "french", "spanish" };

        /// <summary>Steps of dialogue text revealed per frame, 1 to 3.</summary>
        public int TextSpeed = 1;

        /// <summary>Index into <see cref="Languages"/>; the game starts in it.</summary>
        public int Language = 0;

        public static GeneralSettings Load()
        {
            var settings = new GeneralSettings();
            string[] lines;
            try { lines = File.Exists(AudioSettings.SettingsPath) ? File.ReadAllLines(AudioSettings.SettingsPath) : new string[0]; }
            catch (IOException) { return settings; }
            catch (UnauthorizedAccessException) { return settings; }
            foreach (string line in lines)
            {
                int equals = line.IndexOf('=');
                if (equals <= 0) continue;
                string key = line.Substring(0, equals).Trim();
                string value = line.Substring(equals + 1).Trim();
                int speed;
                if (key == "text_speed" && int.TryParse(value, out speed) && speed >= 1 && speed <= 3)
                    settings.TextSpeed = speed;
                if (key == "language")
                {
                    int index = Array.FindIndex(LanguageKeys, k => string.Equals(k, value, StringComparison.OrdinalIgnoreCase));
                    if (index >= 0) settings.Language = index;
                }
            }
            return settings;
        }

        public void Save()
        {
            string path = AudioSettings.SettingsPath;
            var lines = File.Exists(path) ? new List<string>(File.ReadAllLines(path)) : new List<string>();
            Set(lines, "text_speed", Math.Max(1, Math.Min(TextSpeed, 3)).ToString());
            Set(lines, "language", LanguageKeys[Math.Max(0, Math.Min(Language, LanguageKeys.Length - 1))]);
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp = path + ".tmp";
            File.WriteAllText(temp, string.Join("\n", lines) + "\n", new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temp, path, null);
            else File.Move(temp, path);
        }

        private static void Set(List<string> lines, string key, string value)
        {
            string line = key + "=" + value;
            for (int i = 0; i < lines.Count; i++)
            {
                int equals = lines[i].IndexOf('=');
                if (equals > 0 && lines[i].Substring(0, equals).Trim() == key) { lines[i] = line; return; }
            }
            lines.Add(line);
        }
    }
}
