using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// Volume keys of the runtime's runtime_settings.ini (runtime_settings.cpp),
    /// as percentages. The in-game AUDIO page saves the same file, and a running
    /// game takes the file's volumes. Save writes only the volumes changed here
    /// since Load (SettingsFile); unknown keys are kept.
    /// </summary>
    internal sealed class AudioSettings
    {
        public static readonly string[] Names = { NebulaText.Of("Master"), NebulaText.Of("Music"), NebulaText.Of("Sound effects"), NebulaText.Of("Voices"), NebulaText.Of("Ambience"), NebulaText.Of("Cutscenes"), NebulaText.Of("Wiimote sounds") };
        private static readonly string[] Keys = { "audio_master_gain", "audio_music_gain", "audio_sfx_gain", "audio_voice_gain", "audio_ambience_gain", "audio_cutscene_gain", "audio_wiimote_speaker_gain" };

        // runtime_settings.cpp sound_output_key.
        public static readonly string[] SoundOutputKeys = { "mono", "stereo", "surround" };
        public static readonly string[] SoundOutputNames = { NebulaText.Of("Mono"), NebulaText.Of("Stereo"), NebulaText.Of("Surround") };
        public int SoundOutput = 1;

        // Wiimote sounds are off unless chosen, also in files written before the key existed.
        public readonly int[] Percent = { 100, 100, 100, 100, 100, 100, 0 };

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
                string key = line.Substring(0, equals).Trim();
                if (key == "audio_sound_output")
                {
                    int output = Array.FindIndex(SoundOutputKeys, k => string.Equals(k, line.Substring(equals + 1).Trim(), StringComparison.OrdinalIgnoreCase));
                    if (output >= 0) settings.SoundOutput = output;
                    continue;
                }
                int index = Array.IndexOf(Keys, key);
                float gain;
                if (index >= 0 && float.TryParse(line.Substring(equals + 1).Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out gain) &&
                    !float.IsNaN(gain) && !float.IsInfinity(gain))
                    settings.Percent[index] = (int)Math.Round(Math.Max(0f, Math.Min(gain, 2f)) * 100f);
            }
            settings.read = SettingsFile.Snapshot(settings.Values());
            return settings;
        }

        // The values as Load read them (or as the last Save wrote them).
        private Dictionary<string, string> read;

        private List<KeyValuePair<string, string>> Values()
        {
            var values = new List<KeyValuePair<string, string>>();
            for (int k = 0; k < Keys.Length; k++)
                values.Add(new KeyValuePair<string, string>(Keys[k], (Percent[k] / 100f).ToString("R", CultureInfo.InvariantCulture)));
            values.Add(new KeyValuePair<string, string>("audio_sound_output", SoundOutputKeys[Math.Max(0, Math.Min(SoundOutput, SoundOutputKeys.Length - 1))]));
            return values;
        }

        public void Save()
        {
            var values = Values();
            SettingsFile.Save(SettingsPath, values, read);
            read = SettingsFile.Snapshot(values);
        }
    }
}
