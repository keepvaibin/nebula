using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// What the installed game.pak is (Setup's copy of the disc's game partition: boot.bin at 0, the file table where
    /// boot.bin says): its game id and its language folders (UsEnglish, EuGerman, JpJapanese...), which the language
    /// setting offers.
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
                    var id = new byte[6];
                    return file.Read(id, 0, 6) == 6 ? Encoding.ASCII.GetString(id) : null;
                }
            }
            catch (IOException) { return null; }
            catch (UnauthorizedAccessException) { return null; }
        }

        /// <summary>The language folders at the root of game.pak's file table, in its order; empty when it cannot be
        /// read.</summary>
        public static List<string> LanguageFolders(string pak)
        {
            var folders = new List<string>();
            try
            {
                using (var file = new FileStream(pak, FileMode.Open, FileAccess.Read, FileShare.Read))
                {
                    var boot = new byte[0x440];
                    if (file.Read(boot, 0, boot.Length) != boot.Length) return folders;
                    // Wii partition offsets and sizes are words: boot.bin holds them divided by 4.
                    long fstOffset = (long)BigEndian32(boot, 0x424) << 2;
                    long fstSize = (long)BigEndian32(boot, 0x428) << 2;
                    if (fstSize < 12 || fstSize > 16 << 20 || fstOffset + fstSize > file.Length) return folders;
                    var fst = new byte[fstSize];
                    file.Seek(fstOffset, SeekOrigin.Begin);
                    if (file.Read(fst, 0, fst.Length) != fst.Length) return folders;
                    // Entries of 12 bytes (a flag byte, the name's offset in the strings after the last entry, then
                    // two words: a directory's parent and the entry after its last); the root's second word is the
                    // entry count.
                    uint count = BigEndian32(fst, 8);
                    if (count == 0 || count * 12L > fst.Length) return folders;
                    long strings = count * 12L;
                    for (uint index = 1; index < count;)
                    {
                        long entry = index * 12L;
                        bool directory = fst[entry] != 0;
                        uint next = directory ? BigEndian32(fst, (int)entry + 8) : index + 1;
                        if (directory)
                        {
                            string name = Name(fst, strings + (BigEndian32(fst, (int)entry) & 0xFFFFFF));
                            if (name != null && IsLanguageFolder(name)) folders.Add(name);
                        }
                        if (next <= index) break;
                        index = next;
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

        private static string Name(byte[] fst, long at)
        {
            if (at < 0 || at >= fst.Length) return null;
            long end = at;
            while (end < fst.Length && fst[end] != 0) end++;
            return end < fst.Length ? Encoding.ASCII.GetString(fst, (int)at, (int)(end - at)) : null;
        }

        private static uint BigEndian32(byte[] data, int at)
        {
            return (uint)data[at] << 24 | (uint)data[at + 1] << 16 | (uint)data[at + 2] << 8 | data[at + 3];
        }
    }
}
