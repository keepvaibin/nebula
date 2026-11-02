using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// What the installed game.pak is (the asset store, crates/nebula-import/src/cook/store.rs): its game id and its
    /// language folders (UsEnglish, EuGerman, JpJapanese...), which the language setting offers.
    /// </summary>
    internal static class GameDisc
    {
        /// <summary>The game id ("RMGE01", "RMGP01", "RMGJ01"), or null when game.pak cannot be read.</summary>
        public static string GameId(string pak)
        {
            try
            {
                using (var file = new FileStream(pak, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    var header = new byte[HeaderSize];
                    return ReadHeader(file, header) ? Encoding.ASCII.GetString(header, 48, 6) : null;
                }
            }
            catch (IOException) { return null; }
            catch (UnauthorizedAccessException) { return null; }
        }

        /// <summary>The language folders at the root of game.pak, in its order; empty when it cannot be read.</summary>
        public static List<string> LanguageFolders(string pak)
        {
            var folders = new List<string>();
            try
            {
                using (var file = new FileStream(pak, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    var header = new byte[HeaderSize];
                    if (!ReadHeader(file, header)) return folders;
                    long nodesAt = BitConverter.ToInt64(header, 16);
                    long namesAt = BitConverter.ToInt64(header, 32);
                    long namesSize = BitConverter.ToInt64(header, 40);
                    uint count = BitConverter.ToUInt32(header, 12);
                    if (count == 0 || count > 1 << 24 || namesSize <= 0 || namesSize > 16 << 20 ||
                        namesAt + namesSize > file.Length || nodesAt + count * (long)NodeSize > file.Length) return folders;
                    var names = new byte[namesSize];
                    file.Seek(namesAt, SeekOrigin.Begin);
                    if (file.Read(names, 0, names.Length) != names.Length) return folders;
                    var nodes = new byte[count * NodeSize];
                    file.Seek(nodesAt, SeekOrigin.Begin);
                    if (file.Read(nodes, 0, nodes.Length) != nodes.Length) return folders;
                    // The root is node 0: its position is its first child, its size the number of its children.
                    ulong first = BitConverter.ToUInt64(nodes, 0);
                    ulong children = BitConverter.ToUInt64(nodes, 8);
                    for (ulong index = first; index < first + children && index < count; index++)
                    {
                        int node = (int)index * NodeSize;
                        if ((BitConverter.ToUInt32(nodes, node + 24) & 1) == 0) continue;
                        string name = Name(names, BitConverter.ToUInt32(nodes, node + 16));
                        if (name != null && IsLanguageFolder(name)) folders.Add(name);
                    }
                }
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return folders;
        }

        /// <summary>A folder the game reads one language from: a region the game knows and the language's name
        /// (Language.cpp).</summary>
        public static bool IsLanguageFolder(string name)
        {
            foreach (string region in new[] { "Us", "Eu", "Jp", "Kr", "Cn", "Tw" })
            {
                if (name.Length < 3 || !name.StartsWith(region, StringComparison.Ordinal) || !char.IsUpper(name[2])) continue;
                bool letters = true;
                foreach (char c in name.Substring(2)) letters &= c < 128 && char.IsLetter(c);
                if (letters) return true;
            }
            return false;
        }

        private const int HeaderSize = 64;
        private const int NodeSize = 32;

        private static bool ReadHeader(Stream file, byte[] header)
        {
            if (file.Read(header, 0, header.Length) != header.Length) return false;
            return Encoding.ASCII.GetString(header, 0, 7) == "NBSTORE" && header[7] == 0;
        }

        private static string Name(byte[] names, long at)
        {
            if (at < 0 || at >= names.Length) return null;
            long end = at;
            while (end < names.Length && names[end] != 0) end++;
            return end < names.Length ? Encoding.ASCII.GetString(names, (int)at, (int)(end - at)) : null;
        }
    }
}
