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

        /// <summary>Steps of dialogue text revealed per frame, 1 to 3.</summary>
        public int TextSpeed = 1;

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
                if (equals <= 0 || line.Substring(0, equals).Trim() != "text_speed") continue;
                int speed;
                if (int.TryParse(line.Substring(equals + 1).Trim(), out speed) && speed >= 1 && speed <= 3)
                    settings.TextSpeed = speed;
            }
            return settings;
        }

        public void Save()
        {
            string path = AudioSettings.SettingsPath;
            var lines = File.Exists(path) ? new List<string>(File.ReadAllLines(path)) : new List<string>();
            string line = "text_speed=" + Math.Max(1, Math.Min(TextSpeed, 3));
            bool found = false;
            for (int i = 0; i < lines.Count && !found; i++)
            {
                int equals = lines[i].IndexOf('=');
                if (equals > 0 && lines[i].Substring(0, equals).Trim() == "text_speed") { lines[i] = line; found = true; }
            }
            if (!found) lines.Add(line);
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp = path + ".tmp";
            File.WriteAllText(temp, string.Join("\n", lines) + "\n", new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temp, path, null);
            else File.Move(temp, path);
        }
    }
}
