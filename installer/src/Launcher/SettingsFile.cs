using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// The key=value files that Nebula Settings and the running game both save (config.ini, runtime_settings.ini).
    /// Settings write back only the keys whose values changed since they were read, so a value the game saved in
    /// the meantime (the in-game menu, a window dragged to a new size) is not replaced by the one read earlier.
    /// Every other line stays as the file has it now.
    /// </summary>
    internal static class SettingsFile
    {
        public static Dictionary<string, string> Snapshot(IEnumerable<KeyValuePair<string, string>> values)
        {
            var snapshot = new Dictionary<string, string>(StringComparer.Ordinal);
            foreach (var pair in values) snapshot[pair.Key] = pair.Value;
            return snapshot;
        }

        /// <summary>
        /// Writes <paramref name="values"/> into <paramref name="path"/>. A key whose value equals its value in
        /// <paramref name="read"/> (the values as these settings were read) keeps the file's line when the file
        /// has one; null writes every key.
        /// </summary>
        public static void Save(string path, IEnumerable<KeyValuePair<string, string>> values, IDictionary<string, string> read)
        {
            var lines = File.Exists(path) ? new List<string>(File.ReadAllLines(path)) : new List<string>();
            foreach (var pair in values)
            {
                int index = lines.FindIndex(existing => string.Equals(Key(existing), pair.Key, StringComparison.OrdinalIgnoreCase));
                string before;
                if (index >= 0 && read != null && read.TryGetValue(pair.Key, out before) && before == pair.Value) continue;
                string line = pair.Key + "=" + pair.Value;
                if (index >= 0) lines[index] = line;
                else lines.Add(line);
            }
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp = path + ".tmp";
            File.WriteAllText(temp, string.Join("\n", lines) + "\n", new UTF8Encoding(false));
            if (File.Exists(path)) File.Replace(temp, path, null);
            else File.Move(temp, path);
        }

        private static string Key(string line)
        {
            int equals = line.IndexOf('=');
            return equals > 0 ? line.Substring(0, equals).Trim() : null;
        }
    }
}
