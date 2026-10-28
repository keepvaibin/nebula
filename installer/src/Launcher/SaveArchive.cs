using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Text;

namespace Nebula.Launcher
{
    /// <summary>
    /// Checks of the save files the game reads: GameData.bin's layout and checksum, the save banner's header, and the
    /// Mii database's magic numbers and CRC.
    /// </summary>
    internal static class SaveFormat
    {
        public const int GameDataSize = 0xBE00;
        public const int BannerSize = 0x72A0;
        public const int MiiDatabaseSize = 0x1F1E0;
        public const int MiiHiddenOffset = 0x1D00;
        internal static readonly string[] GameDataNames =
        {
            "mario1", "luigi1", "config1", "mario2", "luigi2", "config2", "mario3", "luigi3", "config3",
            "mario4", "luigi4", "config4", "mario5", "luigi5", "config5", "mario6", "luigi6", "config6", "sysconf",
        };
        internal static readonly uint[] GameDataOffsets =
        {
            0x140, 0x10C0, 0x2040, 0x20A0, 0x3020, 0x3FA0, 0x4000, 0x4F80, 0x5F00, 0x5F60,
            0x6EE0, 0x7E60, 0x7EC0, 0x8E40, 0x9DC0, 0x9E20, 0xADA0, 0xBD20, 0xBD80,
        };

        private static uint Be32(byte[] bytes, int offset)
        {
            return (uint)(bytes[offset] << 24 | bytes[offset + 1] << 16 | bytes[offset + 2] << 8 | bytes[offset + 3]);
        }

        /// <summary>The header word: the 16-bit sum of the words after it and the sum of their complements.</summary>
        public static uint GameDataChecksum(byte[] bytes)
        {
            ushort sum = 0, inverse = 0;
            for (int offset = 4; offset < GameDataSize; offset += 2)
            {
                var word = (ushort)(bytes[offset] << 8 | bytes[offset + 1]);
                sum += word;
                inverse += (ushort)~word;
            }
            return (uint)sum << 16 | inverse;
        }

        public static bool IsGameData(byte[] bytes)
        {
            if (bytes == null || bytes.Length != GameDataSize || Be32(bytes, 4) != 2 || Be32(bytes, 8) != 19 ||
                Be32(bytes, 12) != GameDataSize)
                return false;
            for (int i = 0; i < GameDataNames.Length; i++)
            {
                int info = 0x10 + i * 0x10, length = 0;
                while (length < 12 && bytes[info + length] != 0) length++;
                if (length == 12 || Encoding.ASCII.GetString(bytes, info, length) != GameDataNames[i] ||
                    Be32(bytes, info + 12) != GameDataOffsets[i])
                    return false;
            }
            return Be32(bytes, 0) == GameDataChecksum(bytes);
        }

        public static bool IsBanner(byte[] bytes)
        {
            if (bytes == null || bytes.Length != BannerSize || bytes[0] != 'W' || bytes[1] != 'I' || bytes[2] != 'B' ||
                bytes[3] != 'N' || Be32(bytes, 4) != 0)
                return false;
            int iconSpeed = bytes[8] << 8 | bytes[9];
            if (iconSpeed != 2 && iconSpeed != 3) return false;
            for (int i = 10; i < 32; i++)
                if (bytes[i] != 0) return false;
            return true;
        }

        public static bool IsMiiDatabase(byte[] bytes)
        {
            return bytes != null && bytes.Length == MiiDatabaseSize && Be32(bytes, 0) == 0x524E4F44 &&
                Be32(bytes, MiiHiddenOffset) == 0x524E4844 && MiiCrc(bytes) == 0;
        }

        /// <summary>CRC-16 (polynomial 0x1021) over the whole database; zero when its stored CRC is intact.</summary>
        public static int MiiCrc(byte[] bytes)
        {
            int crc = 0;
            foreach (byte value in bytes)
            {
                int data = value;
                for (int bit = 0; bit < 8; bit++, data <<= 1)
                {
                    crc = (crc & 0x8000) != 0 ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
                    if ((data & 0x80) != 0) crc ^= 1;
                }
            }
            return crc;
        }
    }

    /// <summary>A folder or file of a saves tree with its named streams. Paths are relative and '/'-separated; "" is the root.</summary>
    internal sealed class SaveItem
    {
        public string Path;
        public bool IsFolder;
        public byte[] Data;
        public readonly SortedDictionary<string, byte[]> Streams = new SortedDictionary<string, byte[]>(StringComparer.Ordinal);
    }

    /// <summary>
    /// Export and import of the saves folder (the runtime's NAND root). An export holds the files the game reads and
    /// the folders above them, each with its NTFS streams (galaxy.isfs.meta holds the ISFS metadata) as separate zip
    /// entries, so an imported export is the same tree. /tmp is left out: it only holds files a save stages.
    /// Archive layout: nebula-saves.json (items with sizes and SHA-256), saves/&lt;path&gt;, streams/&lt;path&gt;/&lt;stream&gt;.
    /// </summary>
    internal static class SaveArchive
    {
        /// <summary>The releases' save titles, as their game ids in hex: North America's (RMGE), Europe's (RMGP), Japan's
        /// (RMGJ). Each keeps its own save, as on the Wii.</summary>
        internal static readonly string[] Titles = { "524d4745", "524d4750", "524d474a" };

        public const string GameDataPath = "title/00010000/524d4745/data/GameData.bin";
        public const string BannerPath = "title/00010000/524d4745/data/banner.bin";
        public const string MiiDatabasePath = "shared2/menu/FaceLib/RFL_DB.dat";

        internal static string GameDataPathOf(string title) { return "title/00010000/" + title + "/data/GameData.bin"; }
        internal static string BannerPathOf(string title) { return "title/00010000/" + title + "/data/banner.bin"; }

        /// <summary>The save title of a game id ("RMGP01": 524d4750); North America's for any other.</summary>
        internal static string TitleOf(string gameId)
        {
            if (gameId == null || gameId.Length < 4) return Titles[0];
            string title = string.Concat(gameId.Substring(0, 4).Select(c => ((int)c).ToString("x2", CultureInfo.InvariantCulture)));
            return Titles.Contains(title) ? title : Titles[0];
        }

        // Parents first.
        internal static readonly string[] Folders = new[]
        {
            "", "shared2", "shared2/menu", "shared2/menu/FaceLib", "shared2/sys", "title", "title/00000001",
            "title/00000001/00000002", "title/00000001/00000002/data", "title/00010000",
        }.Concat(Titles.SelectMany(t => new[] { "title/00010000/" + t, "title/00010000/" + t + "/data" })).ToArray();
        internal static readonly string[] Files = Titles.SelectMany(t => new[] { GameDataPathOf(t), BannerPathOf(t) })
            .Concat(new[]
            {
                MiiDatabasePath, "shared2/sys/SYSCONF", "title/00000001/00000002/data/setting.txt",
                "title/00000001/00000002/data/play_rec.dat",
            }).ToArray();

        /// <summary>The save title a loose GameData.bin (a folder or zip that holds it alone) is imported into: the
        /// installed disc's (the settings set it from game.pak).</summary>
        internal static string LooseTitle = Titles[0];

        private const string ManifestEntry = "nebula-saves.json";
        private const string FormatName = "nebula-saves";
        private const int FormatVersion = 1;
        private const int MaximumFileBytes = 1 << 20;
        private const int MaximumStreamBytes = 4096;

        /// <summary>The player's game holds the saves open while it runs.</summary>
        internal static Func<bool> GameIsRunning = LaunchSession.RuntimeLeaseHeld;

        /// <summary>Called after each staged folder or file (tests fail an import part-way).</summary>
        internal static Action<string> Staged = delegate { };

        /// <summary>Writes Nebula-Saves-yyyyMMdd-HHmmss.zip into <paramref name="folder"/> and returns its path.</summary>
        public static string Export(string folder)
        {
            if (GameIsRunning()) throw new InvalidOperationException("Close the game before exporting saves.");
            string root = NebulaPaths.Saves;
            List<SaveItem> items = Directory.Exists(root) ? ReadTree(FileUtil.FinalPath(root)) : new List<SaveItem>();
            List<SaveItem> games = GameSaves(items);
            if (games.Count == 0) throw new InvalidOperationException("There are no saves to export yet.");
            if (games.Any(save => !SaveFormat.IsGameData(save.Data)))
                throw new InvalidDataException("GameData.bin is not a valid Super Mario Galaxy save.");
            if (items.Any(i => i.Streams.Keys.Any(s => s != "galaxy.isfs.meta" && s.StartsWith("galaxy.", StringComparison.Ordinal))) ||
                Directory.EnumerateFileSystemEntries(FileUtil.FinalPath(root), "*", SearchOption.AllDirectories)
                    .Any(p => p.IndexOf(".__galaxy_isfs_", StringComparison.OrdinalIgnoreCase) >= 0))
                throw new InvalidOperationException("A save was interrupted. Start and close the game once, then export again.");

            string stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
            string path = Path.Combine(folder, "Nebula-Saves-" + stamp + ".zip");
            for (int number = 2; File.Exists(path); number++)
                path = Path.Combine(folder, "Nebula-Saves-" + stamp + "-" + number + ".zip");
            string partial = path + ".partial";
            try
            {
                WriteArchive(partial, items);
                if (!Same(items, ReadArchive(partial))) throw new IOException("The written archive does not match the saves.");
                File.Move(partial, path);
            }
            finally
            {
                if (File.Exists(partial)) File.Delete(partial);
            }
            return path;
        }

        internal static void WriteArchive(string path, List<SaveItem> items)
        {
            var listed = new List<object>();
            foreach (SaveItem item in items)
            {
                var record = new Dictionary<string, object> { { "path", item.Path }, { "type", item.IsFolder ? "folder" : "file" } };
                if (!item.IsFolder)
                {
                    record["size"] = item.Data.Length;
                    record["sha256"] = FileUtil.Sha256Bytes(item.Data);
                }
                record["streams"] = item.Streams.Select(s => (object)new Dictionary<string, object>
                {
                    { "name", s.Key }, { "size", s.Value.Length }, { "sha256", FileUtil.Sha256Bytes(s.Value) }
                }).ToList();
                listed.Add(record);
            }
            var manifest = new Dictionary<string, object>
            {
                { "format", FormatName }, { "version", FormatVersion }, { "nebula", BuildInfo.Version }, { "items", listed }
            };
            using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            using (var archive = new ZipArchive(output, ZipArchiveMode.Create))
            {
                AddEntry(archive, ManifestEntry, Encoding.UTF8.GetBytes(Json.Serialize(manifest) + "\n"));
                foreach (SaveItem item in items)
                {
                    if (!item.IsFolder) AddEntry(archive, DataEntry(item.Path), item.Data);
                    foreach (var stream in item.Streams) AddEntry(archive, StreamEntry(item.Path, stream.Key), stream.Value);
                }
            }
        }

        private static void AddEntry(ZipArchive archive, string name, byte[] bytes)
        {
            using (Stream stream = archive.CreateEntry(name, CompressionLevel.Optimal).Open())
                stream.Write(bytes, 0, bytes.Length);
        }

        private static string DataEntry(string path) { return "saves/" + path; }

        private static string StreamEntry(string path, string stream)
        {
            return "streams/" + (path.Length == 0 ? "" : path + "/") + stream;
        }

        /// <summary>
        /// Replaces the saves with those of an exported zip, another zip of a saves folder, or a saves folder. The new
        /// tree is staged beside the saves and compared with the source first; the current saves are moved to
        /// &lt;saves&gt;-backup-&lt;time&gt;. <paramref name="confirmReplace"/> is asked before current saves are
        /// replaced; on false nothing changes and null is returned.
        /// </summary>
        public static string Import(string source, Func<bool> confirmReplace)
        {
            if (GameIsRunning()) throw new InvalidOperationException("Close the game before importing saves.");
            List<SaveItem> items = ReadSource(source);
            var notes = new List<string>();
            List<SaveItem> games = GameSaves(items);
            if (games.Count == 0) throw new InvalidDataException("No Super Mario Galaxy save (GameData.bin) was found.");
            if (games.Any(save => !SaveFormat.IsGameData(save.Data)))
                throw new InvalidDataException("GameData.bin is not a valid Super Mario Galaxy save.");
            foreach (string title in Titles)
                LeaveOutInvalid(items, BannerPathOf(title), SaveFormat.IsBanner, "The save banner (banner.bin) is not valid and was left out.", notes);
            LeaveOutInvalid(items, MiiDatabasePath, SaveFormat.IsMiiDatabase, "The Mii data (RFL_DB.dat) is not valid and was left out.", notes);
            items = WithFolders(items);

            string saves = Path.GetFullPath(NebulaPaths.Saves);
            string target = Directory.Exists(saves) ? FileUtil.FinalPath(saves) : saves;
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            string staging = target + ".import-" + Guid.NewGuid().ToString("N").Substring(0, 8);
            string backup;
            try
            {
                Stage(staging, items);
                if (!Same(items, ReadWholeTree(staging)))
                    throw new IOException("The staged saves do not match the source; nothing was changed.");
                if (confirmReplace != null && Titles.Any(t => File.Exists(HostPath(target, GameDataPathOf(t)))) && !confirmReplace()) return null;
                if (GameIsRunning()) throw new InvalidOperationException("Close the game before importing saves.");
                backup = Replace(target, staging);
            }
            finally
            {
                try { FileUtil.DeleteTree(staging); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
            var message = new StringBuilder("Saves imported from " + Path.GetFileName(source.TrimEnd('\\', '/')) + ".");
            foreach (string note in notes) message.Append("\r\n").Append(note);
            if (backup != null) message.Append("\r\nThe previous saves are in ").Append(backup).Append('.');
            return message.ToString();
        }

        /// <summary>The releases' saves (GameData.bin) among <paramref name="items"/>.</summary>
        private static List<SaveItem> GameSaves(List<SaveItem> items)
        {
            return items.Where(i => !i.IsFolder && Titles.Any(t => i.Path == GameDataPathOf(t))).ToList();
        }

        private static void LeaveOutInvalid(List<SaveItem> items, string path, Func<byte[], bool> valid, string note, List<string> notes)
        {
            SaveItem item = items.FirstOrDefault(i => !i.IsFolder && i.Path == path);
            if (item == null || valid(item.Data)) return;
            items.Remove(item);
            notes.Add(note);
        }

        /// <summary>Every folder that holds a file, parents first. Folders a source did not list have no streams.</summary>
        private static List<SaveItem> WithFolders(List<SaveItem> items)
        {
            var result = new List<SaveItem>();
            foreach (string folder in Folders)
            {
                SaveItem listed = items.FirstOrDefault(i => i.IsFolder && i.Path == folder);
                if (listed != null) result.Add(listed);
                else if (items.Any(i => !i.IsFolder && (folder.Length == 0 || i.Path.StartsWith(folder + "/", StringComparison.Ordinal))))
                    result.Add(new SaveItem { Path = folder, IsFolder = true });
            }
            foreach (string file in Files)
            {
                SaveItem listed = items.FirstOrDefault(i => !i.IsFolder && i.Path == file);
                if (listed != null) result.Add(listed);
            }
            return result;
        }

        private static void Stage(string staging, List<SaveItem> items)
        {
            foreach (SaveItem item in items)
            {
                string host = HostPath(staging, item.Path);
                if (item.IsFolder) Directory.CreateDirectory(host);
                else
                {
                    using (var file = new FileStream(host, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    {
                        file.Write(item.Data, 0, item.Data.Length);
                        file.Flush(true);
                    }
                }
                foreach (var stream in item.Streams) FileUtil.WriteNamedStream(host, stream.Key, stream.Value);
                Staged(item.Path);
            }
        }

        /// <summary>Moves the current saves aside and the staged tree in; on failure the current saves stay or return.</summary>
        private static string Replace(string target, string staging)
        {
            string backup = null;
            if (Directory.Exists(target))
            {
                if (Directory.EnumerateFileSystemEntries(target).Any())
                {
                    string stamp = target + "-backup-" + DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);
                    backup = stamp;
                    for (int number = 2; Directory.Exists(backup) || File.Exists(backup); number++) backup = stamp + "-" + number;
                    FileUtil.MoveDirectoryInPlace(target, backup);
                }
                else Directory.Delete(target);
            }
            try
            {
                FileUtil.MoveDirectoryInPlace(staging, target);
            }
            catch
            {
                if (backup != null && !Directory.Exists(target)) FileUtil.MoveDirectoryInPlace(backup, target);
                throw;
            }
            return backup;
        }

        internal static List<SaveItem> ReadSource(string source)
        {
            if (Directory.Exists(source))
            {
                string root = FileUtil.FinalPath(Path.GetFullPath(source));
                if (File.Exists(Path.Combine(root, ManifestEntry))) return ReadExport(FolderReader(root));
                return ReadFolder(root);
            }
            if (!File.Exists(source)) throw new FileNotFoundException("The file was not found.", source);
            return ReadArchive(source);
        }

        /// <summary>The folders and files of a saves tree that the game reads, with their streams.</summary>
        internal static List<SaveItem> ReadTree(string root)
        {
            var items = new List<SaveItem>();
            foreach (string folder in Folders)
            {
                string host = HostPath(root, folder);
                if (!Directory.Exists(host)) continue;
                if (folder.Length != 0 && FileUtil.IsReparsePoint(host)) throw new IOException("Linked folders are not supported: " + host);
                items.Add(ReadItem(host, folder, true));
            }
            foreach (string file in Files)
            {
                string host = HostPath(root, file);
                if (!File.Exists(host)) continue;
                if (FileUtil.IsReparsePoint(host)) throw new IOException("Linked files are not supported: " + host);
                items.Add(ReadItem(host, file, false));
            }
            return items;
        }

        /// <summary>Every folder and file below <paramref name="root"/>.</summary>
        internal static List<SaveItem> ReadWholeTree(string root)
        {
            var items = new List<SaveItem> { ReadItem(root, "", true) };
            foreach (string folder in Directory.GetDirectories(root, "*", SearchOption.AllDirectories))
                items.Add(ReadItem(folder, folder.Substring(root.Length + 1).Replace('\\', '/'), true));
            foreach (string file in Directory.GetFiles(root, "*", SearchOption.AllDirectories))
                items.Add(ReadItem(file, file.Substring(root.Length + 1).Replace('\\', '/'), false));
            return items;
        }

        private static SaveItem ReadItem(string host, string path, bool folder)
        {
            var item = new SaveItem { Path = path, IsFolder = folder };
            if (!folder)
            {
                if (new FileInfo(host).Length > MaximumFileBytes) throw new InvalidDataException(host + " is too large for a save file.");
                item.Data = File.ReadAllBytes(host);
            }
            foreach (var stream in FileUtil.ReadNamedStreams(host, MaximumStreamBytes)) item.Streams[stream.Key] = stream.Value;
            return item;
        }

        /// <summary>
        /// A saves folder (also one level down as saves, save-data or nand), or a folder that holds GameData.bin itself:
        /// the data folder, or a saves folder of earlier builds.
        /// </summary>
        private static List<SaveItem> ReadFolder(string root)
        {
            foreach (string candidate in new[] { root, Path.Combine(root, "saves"), Path.Combine(root, "save-data"), Path.Combine(root, "nand") })
            {
                if (Titles.Any(t => File.Exists(HostPath(candidate, GameDataPathOf(t))))) return ReadTree(FileUtil.FinalPath(candidate));
            }
            string save = Path.Combine(root, "GameData.bin");
            if (!File.Exists(save)) throw new InvalidDataException("This folder does not contain a Super Mario Galaxy save.");
            var items = new List<SaveItem> { ReadItem(save, GameDataPathOf(LooseTitle), false) };
            string banner = Path.Combine(root, "banner.bin");
            if (File.Exists(banner)) items.Add(ReadItem(banner, BannerPathOf(LooseTitle), false));
            return items;
        }

        /// <summary>An exported archive (checked against its manifest), or a zip of a saves folder made by another tool.</summary>
        internal static List<SaveItem> ReadArchive(string path)
        {
            ZipArchive archive;
            try { archive = ZipFile.OpenRead(path); }
            catch (InvalidDataException error) { throw new InvalidDataException("This is not a zip archive, or the archive is damaged.", error); }
            try
            {
                using (archive)
                {
                    var entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.OrdinalIgnoreCase);
                    foreach (ZipArchiveEntry entry in archive.Entries)
                    {
                        string name = entry.FullName.Replace('\\', '/');
                        if (name.EndsWith("/", StringComparison.Ordinal)) continue;
                        if (entries.ContainsKey(name)) throw new InvalidDataException("The archive holds " + name + " twice.");
                        entries.Add(name, entry);
                    }
                    Func<string, int, byte[]> read = delegate(string name, int maximumBytes)
                    {
                        ZipArchiveEntry entry;
                        return entries.TryGetValue(name, out entry) ? ReadEntry(entry, maximumBytes) : null;
                    };
                    return entries.ContainsKey(ManifestEntry) ? ReadExport(read) : ReadPlainArchive(entries.Keys, read);
                }
            }
            catch (InvalidDataException) { throw; }
            catch (FileNotFoundException) { throw; }
            catch (DirectoryNotFoundException) { throw; }
            catch (IOException error) { throw new InvalidDataException("The archive is damaged.", error); }
            catch (NotSupportedException error) { throw new InvalidDataException("The archive is damaged.", error); }
        }

        private static byte[] ReadEntry(ZipArchiveEntry entry, int maximumBytes)
        {
            if (entry.Length > maximumBytes) throw new InvalidDataException(entry.FullName + " is too large for a save file.");
            using (var memory = new MemoryStream())
            {
                try
                {
                    using (Stream input = entry.Open())
                    {
                        var buffer = new byte[65536];
                        int read;
                        while ((read = input.Read(buffer, 0, buffer.Length)) > 0 && memory.Length <= maximumBytes)
                            memory.Write(buffer, 0, read);
                    }
                }
                catch (InvalidDataException error) { throw new InvalidDataException("The archive is damaged.", error); }
                if (memory.Length != entry.Length) throw new InvalidDataException("The archive is damaged.");
                return memory.ToArray();
            }
        }

        private static Func<string, int, byte[]> FolderReader(string root)
        {
            return delegate(string name, int maximumBytes)
            {
                string file = Path.Combine(root, name.Replace('/', '\\'));
                if (!File.Exists(file)) return null;
                if (new FileInfo(file).Length > maximumBytes) throw new InvalidDataException(name + " is too large for a save file.");
                return File.ReadAllBytes(file);
            };
        }

        private static List<SaveItem> ReadExport(Func<string, int, byte[]> read)
        {
            try
            {
                Dictionary<string, object> manifest;
                try { manifest = Json.Parse(Encoding.UTF8.GetString(read(ManifestEntry, MaximumFileBytes))); }
                catch (ArgumentException) { throw new InvalidDataException("The archive is damaged."); }
                if (Json.Str(manifest, "format") != FormatName) throw new InvalidDataException("This is not a Nebula saves archive.");
                long version = Json.Long(manifest, "version");
                if (version > FormatVersion) throw new InvalidDataException("This archive needs a newer version of Nebula.");
                IList listed = Json.List(manifest, "items");
                if (version != FormatVersion || listed == null) throw new InvalidDataException("The archive is damaged.");
                var items = new List<SaveItem>();
                foreach (object entry in listed)
                {
                    var record = entry as Dictionary<string, object>;
                    string type = Json.Str(record, "type");
                    if (record == null || (type != "folder" && type != "file")) throw new InvalidDataException("The archive is damaged.");
                    bool folder = type == "folder";
                    string path = Canonical(Json.Str(record, "path"), folder);
                    if (path == null) continue; // a path this version does not keep
                    if (items.Any(i => i.IsFolder == folder && i.Path == path)) throw new InvalidDataException("The archive is damaged.");
                    var item = new SaveItem { Path = path, IsFolder = folder };
                    if (!folder) item.Data = ReadVerified(read, DataEntry(path), record, MaximumFileBytes);
                    IList streams = Json.List(record, "streams") ?? new object[0];
                    foreach (object value in streams)
                    {
                        var stream = value as Dictionary<string, object>;
                        string name = Json.Str(stream, "name");
                        if (stream == null || !IsStreamName(name) || item.Streams.ContainsKey(name)) throw new InvalidDataException("The archive is damaged.");
                        item.Streams[name] = ReadVerified(read, StreamEntry(path, name), stream, MaximumStreamBytes);
                    }
                    items.Add(item);
                }
                return items;
            }
            catch (FormatException) { throw new InvalidDataException("The archive is damaged."); }
            catch (InvalidCastException) { throw new InvalidDataException("The archive is damaged."); }
            catch (OverflowException) { throw new InvalidDataException("The archive is damaged."); }
        }

        private static byte[] ReadVerified(Func<string, int, byte[]> read, string name, Dictionary<string, object> record, int maximumBytes)
        {
            byte[] bytes = read(name, maximumBytes);
            if (bytes == null) throw new InvalidDataException("The archive is damaged: " + name + " is missing.");
            if (Json.Long(record, "size") != bytes.Length ||
                !string.Equals(Json.Str(record, "sha256"), FileUtil.Sha256Bytes(bytes), StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The archive is damaged: " + name + " does not match its checksum.");
            return bytes;
        }

        /// <summary>A zip of a saves folder (at its top or one folder down) or of a folder that holds GameData.bin.</summary>
        private static List<SaveItem> ReadPlainArchive(ICollection<string> names, Func<string, int, byte[]> read)
        {
            string prefix = null;
            foreach (string name in names)
            {
                foreach (string title in Titles)
                {
                    string path = GameDataPathOf(title);
                    if (!name.EndsWith(path, StringComparison.OrdinalIgnoreCase)) continue;
                    string head = name.Substring(0, name.Length - path.Length);
                    if ((head.Length == 0 || head.EndsWith("/", StringComparison.Ordinal)) && (prefix == null || head.Length < prefix.Length))
                        prefix = head;
                }
            }
            var items = new List<SaveItem>();
            if (prefix != null)
            {
                foreach (string file in Files)
                {
                    byte[] bytes = read(prefix + file, MaximumFileBytes);
                    if (bytes != null) items.Add(new SaveItem { Path = file, Data = bytes });
                }
                return items;
            }
            string save = names.Where(n => n.Equals("GameData.bin", StringComparison.OrdinalIgnoreCase) ||
                                           n.EndsWith("/GameData.bin", StringComparison.OrdinalIgnoreCase))
                               .OrderBy(n => n.Length).FirstOrDefault();
            if (save == null) throw new InvalidDataException("This archive does not contain a Super Mario Galaxy save.");
            items.Add(new SaveItem { Path = GameDataPathOf(LooseTitle), Data = read(save, MaximumFileBytes) });
            byte[] banner = read(save.Substring(0, save.Length - "GameData.bin".Length) + "banner.bin", MaximumFileBytes);
            if (banner != null) items.Add(new SaveItem { Path = BannerPathOf(LooseTitle), Data = banner });
            return items;
        }

        private static string Canonical(string path, bool folder)
        {
            if (path == null) return null;
            return (folder ? Folders : Files).FirstOrDefault(p => string.Equals(p, path, StringComparison.OrdinalIgnoreCase));
        }

        private static bool IsStreamName(string name)
        {
            return !string.IsNullOrEmpty(name) && name.Length <= 64 && name != "." && name != ".." &&
                name.IndexOfAny(Path.GetInvalidFileNameChars()) < 0;
        }

        internal static string HostPath(string root, string path)
        {
            return path.Length == 0 ? root : Path.Combine(root, path.Replace('/', '\\'));
        }

        /// <summary>The same folders and files, with the same contents and streams.</summary>
        internal static bool Same(List<SaveItem> expected, List<SaveItem> actual)
        {
            return expected.Count == actual.Count && expected.All(e =>
            {
                SaveItem a = actual.FirstOrDefault(i => i.IsFolder == e.IsFolder && i.Path == e.Path);
                return a != null && (e.IsFolder || a.Data.SequenceEqual(e.Data)) && a.Streams.Count == e.Streams.Count &&
                    e.Streams.All(s =>
                    {
                        byte[] bytes;
                        return a.Streams.TryGetValue(s.Key, out bytes) && bytes.SequenceEqual(s.Value);
                    });
            });
        }
    }
}
