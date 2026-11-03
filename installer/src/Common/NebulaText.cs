using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Nebula
{
    /// <summary>
    /// The launcher's and Setup's words in the player's language, from localization/NebulaText.tsv (embedded as
    /// NebulaText.tsv; the game has the same table). A row is found by its English cell. Its language is the language key
    /// of runtime_settings.ini, for "auto" or none the system's. An empty cell uses the language's other spelling
    /// (NebulaText.cpp); no table, row or cell: the English given.
    /// </summary>
    internal static class NebulaText
    {
        private static readonly Dictionary<string, string> Words = Load();

        public static string Of(string english)
        {
            string word;
            return Words.TryGetValue(english, out word) ? word : english;
        }

        public static string Of(string english, params object[] values) { return string.Format(Of(english), values); }

        private static Dictionary<string, string> Load()
        {
            var words = new Dictionary<string, string>();
            try
            {
                string column = Column();
                if (column == null) return words;
                using (var stream = typeof(NebulaText).Assembly.GetManifestResourceStream("NebulaText.tsv"))
                {
                    if (stream == null) return words;
                    using (var reader = new StreamReader(stream, Encoding.UTF8))
                    {
                        var header = new List<string>(reader.ReadLine().Split('\t'));
                        int own = header.IndexOf(column), sister = header.IndexOf(Sister(column)), english = header.IndexOf("UsEnglish");
                        for (string line = reader.ReadLine(); line != null; line = reader.ReadLine())
                        {
                            string[] cells = line.Split('\t');
                            string key = english < cells.Length ? Unescape(cells[english]) : "";
                            if (key.Length == 0 || words.ContainsKey(key)) continue;  // the first row with these words
                            string word = own < cells.Length ? cells[own] : "";
                            if (word.Length == 0 && sister >= 0 && sister < cells.Length) word = cells[sister];
                            if (word.Length != 0) words[key] = Unescape(word);
                        }
                    }
                }
            }
            catch (Exception) { }  // the English given
            return words;
        }

        // The table's column for the language; null for English, the words the code already has.
        private static string Column()
        {
            string language = "auto";
            string settings = Path.Combine(NebulaPaths.DataRoot, "runtime_settings.ini");
            if (File.Exists(settings))
            {
                foreach (string line in File.ReadAllLines(settings))
                {
                    int equals = line.IndexOf('=');
                    if (equals > 0 && line.Substring(0, equals).Trim() == "language") language = line.Substring(equals + 1).Trim().ToLowerInvariant();
                }
            }
            CultureInfo system = CultureInfo.CurrentUICulture;
            if (language == "auto") language = system.TwoLetterISOLanguageName;
            string name = system.Name;
            bool americas = !name.StartsWith("es-ES", StringComparison.OrdinalIgnoreCase);
            switch (language)
            {
                case "french": case "fr": return name.EndsWith("-CA", StringComparison.OrdinalIgnoreCase) || name.EndsWith("-US", StringComparison.OrdinalIgnoreCase) ? "UsFrench" : "EuFrench";
                case "spanish": case "es": return americas ? "UsSpanish" : "EuSpanish";
                case "german": case "de": return "EuGerman";
                case "italian": case "it": return "EuItalian";
                case "dutch": case "nl": return "EuDutch";
                case "japanese": case "ja": return "JpJapanese";
                case "pt": return "UsPortuguese";
                case "ko": return "KrKorean";
                case "zh": return name.Contains("Hant") || name.EndsWith("-TW", StringComparison.OrdinalIgnoreCase) || name.EndsWith("-HK", StringComparison.OrdinalIgnoreCase) || name.EndsWith("-MO", StringComparison.OrdinalIgnoreCase) ? "TwTradChinese" : "CnSimpChinese";
                default: return null;
            }
        }

        private static string Sister(string column)
        {
            return column.StartsWith("Us") ? "Eu" + column.Substring(2) : column.StartsWith("Eu") ? "Us" + column.Substring(2) : column;
        }

        private static string Unescape(string cell) { return cell.Replace("\\n", "\n").Replace("\\\\", "\\"); }
    }
}
