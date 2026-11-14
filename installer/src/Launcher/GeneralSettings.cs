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
        public static readonly string[] TextSpeeds = { NebulaText.Of("1x (original)"), "2x", "3x", NebulaText.Of("Instant") };

        /// <summary>The game's languages as runtime_settings.ini names them, each in its own name, and the language
        /// its disc folders hold (UsEnglish, EuEnglish: English). Automatic: the system's language where the disc has it,
        /// else English, else the disc's own (runtime_settings.cpp game_language_resolved).</summary>
        public static readonly string[] LanguageKeys = { "auto", "english", "german", "french", "spanish", "italian", "dutch", "japanese" };
        public static readonly string[] LanguageNames = { NebulaText.Of("Automatic"), "English", "Deutsch", "Fran\u00E7ais", "Espa\u00F1ol", "Italiano", "Nederlands", "\u65E5\u672C\u8A9E" };
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

        // runtime_settings.cpp button_icons_key, focus_loss_key; the game's menu's steps for the text size (0 automatic).
        public static readonly string[] ButtonIconKeys = { "auto", "game", "xbox", "nintendo", "playstation", "keyboard" };
        public static readonly string[] ButtonIconNames = { NebulaText.Of("Auto"), NebulaText.Of("Wii"), NebulaText.Of("Xbox"), NebulaText.Of("Nintendo Switch"), NebulaText.Of("PlayStation"), NebulaText.Of("Keyboard and mouse") };
        public static readonly string[] FocusLossKeys = { "keep", "pause", "mute" };
        public static readonly string[] FocusLossNames = { NebulaText.Of("Keep running"), NebulaText.Of("Pause"), NebulaText.Of("Mute") };
        public static readonly int[] MenuTextSizes = { 0, 80, 90, 100, 110, 120, 130, 140, 150, 160, 180, 200 };

        public int ButtonIcons = 0, FocusLoss = 0;
        /// <summary>0 automatic, else 70 to 200 percent.</summary>
        public int MenuTextSize = 0;
        public bool DiscordStatus = true;

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
                if (key == "button_icons")
                {
                    int index = Array.FindIndex(ButtonIconKeys, k => string.Equals(k, value, StringComparison.OrdinalIgnoreCase));
                    if (string.Equals(value, "wii", StringComparison.OrdinalIgnoreCase)) index = 1;
                    if (index >= 0) settings.ButtonIcons = index;
                }
                if (key == "focus_loss")
                {
                    int index = Array.FindIndex(FocusLossKeys, k => string.Equals(k, value, StringComparison.OrdinalIgnoreCase));
                    if (index >= 0) settings.FocusLoss = index;
                }
                int size;
                if (key == "menu_text_size" && int.TryParse(value, out size))
                    settings.MenuTextSize = size <= 0 ? 0 : Math.Max(70, Math.Min(size, 200));
                if (key == "discord_status" && value.Length > 0) settings.DiscordStatus = value[0] != '0';
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
                new KeyValuePair<string, string>("button_icons", ButtonIconKeys[Math.Max(0, Math.Min(ButtonIcons, ButtonIconKeys.Length - 1))]),
                new KeyValuePair<string, string>("menu_text_size", (MenuTextSize <= 0 ? 0 : Math.Max(70, Math.Min(MenuTextSize, 200))).ToString(System.Globalization.CultureInfo.InvariantCulture)),
                new KeyValuePair<string, string>("focus_loss", FocusLossKeys[Math.Max(0, Math.Min(FocusLoss, FocusLossKeys.Length - 1))]),
                new KeyValuePair<string, string>("discord_status", DiscordStatus ? "1" : "0"),
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
