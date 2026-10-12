using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// Volume keys of the runtime's runtime_settings.ini (runtime_settings.cpp),
    /// as percentages. The in-game AUDIO page saves the same file; unknown keys
    /// are kept.
    /// </summary>
    internal sealed class AudioSettings
    {
        public static readonly string[] Names = { "Master", "Music", "Sound effects", "Voices", "Ambience", "Cutscenes" };
        private static readonly string[] Keys = { "audio_master_gain", "audio_music_gain", "audio_sfx_gain", "audio_voice_gain", "audio_ambience_gain", "audio_cutscene_gain" };

        public readonly int[] Percent = { 100, 100, 100, 100, 100, 100 };

        public static string SettingsPath { get { return Path.Combine(NebulaPaths.DataRoot, "runtime_settings.ini"); } }

        public static AudioSettings Load()
        {
            var settings = new AudioSettings();
            List<string> lines;
            try { lines = File.Exists(SettingsPath) ? new List<string>(File.ReadAllLines(SettingsPath)) : new List<string>(); }
            catch (IOException) { return settings; }
            catch (UnauthorizedAccessException) { return settings; }
            foreach (string line in lines)
            {
                int equals = line.IndexOf('=');
                if (equals <= 0) continue;
                int index = Array.IndexOf(Keys, line.Substring(0, equals).Trim());
                float gain;
                if (index >= 0 && float.TryParse(line.Substring(equals + 1).Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out gain) &&
                    !float.IsNaN(gain) && !float.IsInfinity(gain))
                    settings.Percent[index] = (int)Math.Round(Math.Max(0f, Math.Min(gain, 2f)) * 100f);
            }
            return settings;
        }

        public void Save()
        {
            var lines = File.Exists(SettingsPath) ? new List<string>(File.ReadAllLines(SettingsPath)) : new List<string>();
            for (int k = 0; k < Keys.Length; k++)
            {
                string line = Keys[k] + "=" + (Percent[k] / 100f).ToString("R", CultureInfo.InvariantCulture);
                bool found = false;
                for (int i = 0; i < lines.Count && !found; i++)
                {
                    int equals = lines[i].IndexOf('=');
                    if (equals > 0 && lines[i].Substring(0, equals).Trim() == Keys[k]) { lines[i] = line; found = true; }
                }
                if (!found) lines.Add(line);
            }
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath));
            string temp = SettingsPath + ".tmp";
            File.WriteAllText(temp, string.Join("\n", lines) + "\n", new UTF8Encoding(false));
            if (File.Exists(SettingsPath)) File.Replace(temp, SettingsPath, null);
            else File.Move(temp, SettingsPath);
        }
    }
}
