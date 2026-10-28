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

        /// <summary>The game's languages as runtime_settings.ini names them, each in its own name, and the language
        /// its disc folders hold (UsEnglish, EuEnglish: English). Automatic: the system's language where the disc has it,
        /// else English, else the disc's own (runtime_settings.cpp game_language_resolved).</summary>
        public static readonly string[] LanguageKeys = { "auto", "english", "german", "french", "spanish", "italian", "dutch", "japanese" };
        public static readonly string[] LanguageNames = { "Automatic", "English", "Deutsch", "Fran\u00E7ais", "Espa\u00F1ol", "Italiano", "Nederlands", "\u65E5\u672C\u8A9E" };
        private static readonly string[] FolderLanguages = { null, "English", "German", "French", "Spanish", "Italian", "Dutch", "Japanese" };

        /// <summary>The languages a game.pak offers, as indices into <see cref="LanguageKeys"/>: Automatic, then those its
        /// language folders hold, in that order (every one when game.pak cannot be read).</summary>
        public static List<int> Offered(string pak)
        {
            var folders = GameDisc.LanguageFolders(pak);
            var offered = new List<int> { 0 };
            for (int i = 1; i < LanguageKeys.Length; i++)
            {
                if (folders.Count == 0 || folders.Exists(folder => folder.Substring(2) == FolderLanguages[i])) offered.Add(i);
            }
            return offered;
        }

        /// <summary>Steps of dialogue text revealed per frame, 1 to 3; 4 shows each page at once.</summary>
        public int TextSpeed = 1;

        /// <summary>Index into <see cref="LanguageKeys"/>; the game starts in it.</summary>
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
