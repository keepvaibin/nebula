using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// General keys of the runtime's runtime_settings.ini (runtime_settings.cpp). The in-game GENERAL page
    /// saves the same file, and a running game takes the file's text speed (and its language at the next
    /// start). Save writes only the settings changed here since Load (SettingsFile); unknown keys are kept.
    /// </summary>
    internal sealed class GeneralSettings
    {
        public static readonly string[] TextSpeeds = { "1x (original)", "2x", "3x", "Instant" };

        /// <summary>The game's three languages, each in its own name, and their runtime_settings.ini values.</summary>
        public static readonly string[] Languages = { "English", "Français", "Español" };
        private static readonly string[] LanguageKeys = { "english", "french", "spanish" };

        /// <summary>Steps of dialogue text revealed per frame, 1 to 3; 4 shows each page at once.</summary>
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
                if (key == "text_speed" && int.TryParse(value, out speed) && speed >= 1 && speed <= 4)
                    settings.TextSpeed = speed;
                if (key == "language")
                {
                    int index = Array.FindIndex(LanguageKeys, k => string.Equals(k, value, StringComparison.OrdinalIgnoreCase));
                    if (index >= 0) settings.Language = index;
                }
            }
            settings.read = SettingsFile.Snapshot(settings.Values());
            return settings;
        }

        // The values as Load read them (or as the last Save wrote them).
        private Dictionary<string, string> read;

        private List<KeyValuePair<string, string>> Values()
        {
            return new List<KeyValuePair<string, string>>
            {
                new KeyValuePair<string, string>("text_speed", Math.Max(1, Math.Min(TextSpeed, 4)).ToString(System.Globalization.CultureInfo.InvariantCulture)),
                new KeyValuePair<string, string>("language", LanguageKeys[Math.Max(0, Math.Min(Language, LanguageKeys.Length - 1))]),
            };
        }

        public void Save()
        {
            var values = Values();
            SettingsFile.Save(AudioSettings.SettingsPath, values, read);
            read = SettingsFile.Snapshot(values);
        }
    }
}
