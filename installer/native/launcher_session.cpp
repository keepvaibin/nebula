#include "launcher_model.h"

#include <commctrl.h>
#include <psapi.h>
#include <shlobj.h>

#include <algorithm>

namespace nebula::launcher {

// ---- save files (native_host.cpp is_valid_rmge_game_data, is_valid_rmge_banner, is_valid_rfl_database) ----------

namespace format {
const size_t kGameDataSize = 0xBE00, kBannerSize = 0x72A0, kMiiDatabaseSize = 0x1F1E0, kMiiHiddenOffset = 0x1D00;
const char* const kGameDataNames[] = {"mario1", "luigi1", "config1", "mario2", "luigi2", "config2", "mario3", "luigi3", "config3", "mario4",
                                      "luigi4", "config4", "mario5", "luigi5", "config5", "mario6", "luigi6", "config6", "sysconf"};
const uint32_t kGameDataOffsets[] = {0x140,  0x10C0, 0x2040, 0x20A0, 0x3020, 0x3FA0, 0x4000, 0x4F80, 0x5F00, 0x5F60,
                                     0x6EE0, 0x7E60, 0x7EC0, 0x8E40, 0x9DC0, 0x9E20, 0xADA0, 0xBD20, 0xBD80};

uint32_t Be32(const Bytes& b, size_t o) { return uint32_t(b[o]) << 24 | uint32_t(b[o + 1]) << 16 | uint32_t(b[o + 2]) << 8 | b[o + 3]; }

uint32_t GameDataChecksum(const Bytes& bytes) {
    uint16_t sum = 0, inverse = 0;
    for (size_t offset = 4; offset < kGameDataSize; offset += 2) {
        uint16_t word = uint16_t(bytes[offset] << 8 | bytes[offset + 1]);
        sum = uint16_t(sum + word);
        inverse = uint16_t(inverse + uint16_t(~word));
    }
    return uint32_t(sum) << 16 | inverse;
}

bool IsGameData(const Bytes& bytes) {
    if (bytes.size() != kGameDataSize || Be32(bytes, 4) != 2 || Be32(bytes, 8) != 19 || Be32(bytes, 12) != kGameDataSize) return false;
    for (size_t i = 0; i < 19; i++) {
        size_t info = 0x10 + i * 0x10, length = 0;
        while (length < 12 && bytes[info + length] != 0) length++;
        if (length == 12 || std::string(reinterpret_cast<const char*>(&bytes[info]), length) != kGameDataNames[i] ||
            Be32(bytes, info + 12) != kGameDataOffsets[i])
            return false;
    }
    return Be32(bytes, 0) == GameDataChecksum(bytes);
}

bool IsBanner(const Bytes& bytes) {
    if (bytes.size() != kBannerSize || bytes[0] != 'W' || bytes[1] != 'I' || bytes[2] != 'B' || bytes[3] != 'N' || Be32(bytes, 4) != 0)
        return false;
    int iconSpeed = bytes[8] << 8 | bytes[9];
    if (iconSpeed != 2 && iconSpeed != 3) return false;
    for (size_t i = 10; i < 32; i++)
        if (bytes[i] != 0) return false;
    return true;
}

int MiiCrc(const Bytes& bytes) {
    int crc = 0;
    for (uint8_t value : bytes) {
        int data = value;
        for (int bit = 0; bit < 8; bit++, data <<= 1) {
            crc = (crc & 0x8000) ? ((crc << 1) ^ 0x1021) & 0xFFFF : (crc << 1) & 0xFFFF;
            if (data & 0x80) crc ^= 1;
        }
    }
    return crc;
}

bool IsMiiDatabase(const Bytes& bytes) {
    return bytes.size() == kMiiDatabaseSize && Be32(bytes, 0) == 0x524E4F44 && Be32(bytes, kMiiHiddenOffset) == 0x524E4844 &&
           MiiCrc(bytes) == 0;
}
}  // namespace format

// ---- the saves archive -------------------------------------------------------------------------------------------

namespace saves {
namespace {
struct SaveItem {
    wstring Path;
    bool IsFolder = false;
    Bytes Data;
    std::map<wstring, Bytes> Streams;
};

const wstring kGameDataPath = L"title/00010000/524d4745/data/GameData.bin";
const wstring kBannerPath = L"title/00010000/524d4745/data/banner.bin";
const wstring kMiiDatabasePath = L"shared2/menu/FaceLib/RFL_DB.dat";
// Parents first.
const std::vector<wstring> kFolders = {L"", L"shared2", L"shared2/menu", L"shared2/menu/FaceLib", L"shared2/sys", L"title", L"title/00000001",
                                       L"title/00000001/00000002", L"title/00000001/00000002/data", L"title/00010000",
                                       L"title/00010000/524d4745", L"title/00010000/524d4745/data"};
const std::vector<wstring> kFiles = {kGameDataPath, kBannerPath, kMiiDatabasePath, L"shared2/sys/SYSCONF",
                                     L"title/00000001/00000002/data/setting.txt", L"title/00000001/00000002/data/play_rec.dat"};
const wstring kManifestEntry = L"nebula-saves.json";
const wstring kFormatName = L"nebula-saves";
const int kFormatVersion = 1;
const long long kMaximumFileBytes = 1 << 20;
const long long kMaximumStreamBytes = 4096;

[[noreturn]] void Damaged(const wstring& detail = L"") {
    Fail(ErrorKind::InvalidData, detail.empty() ? L"The archive is damaged." : L"The archive is damaged: " + detail);
}

wstring HostPath(const wstring& root, const wstring& path) { return path.empty() ? root : PathCombine(root, Slashes(path)); }
wstring DataEntry(const wstring& path) { return L"saves/" + path; }
wstring StreamEntry(const wstring& path, const wstring& stream) { return L"streams/" + (path.empty() ? L"" : path + L"/") + stream; }

SaveItem* FindItem(std::vector<SaveItem>& items, bool folder, const wstring& path) {
    for (auto& item : items)
        if (item.IsFolder == folder && item.Path == path) return &item;
    return nullptr;
}

SaveItem ReadItem(const wstring& host, const wstring& path, bool folder) {
    SaveItem item;
    item.Path = path;
    item.IsFolder = folder;
    if (!folder) {
        if (FileSize(host) > kMaximumFileBytes) Fail(ErrorKind::InvalidData, host + L" is too large for a save file.");
        item.Data = ReadAllBytes(host);
    }
    for (auto& stream : ReadNamedStreams(host, kMaximumStreamBytes)) item.Streams[stream.first] = std::move(stream.second);
    return item;
}

std::vector<SaveItem> ReadTree(const wstring& root) {
    std::vector<SaveItem> items;
    for (const auto& folder : kFolders) {
        wstring host = HostPath(root, folder);
        if (!DirExists(host)) continue;
        if (!folder.empty() && IsReparsePoint(host)) Fail(ErrorKind::Io, L"Linked folders are not supported: " + host);
        items.push_back(ReadItem(host, folder, true));
    }
    for (const auto& file : kFiles) {
        wstring host = HostPath(root, file);
        if (!FileExists(host)) continue;
        if (IsReparsePoint(host)) Fail(ErrorKind::Io, L"Linked files are not supported: " + host);
        items.push_back(ReadItem(host, file, false));
    }
    return items;
}

std::vector<SaveItem> ReadWholeTree(const wstring& root) {
    std::vector<SaveItem> items = {ReadItem(root, L"", true)};
    for (const auto& folder : ListDirs(root, true)) items.push_back(ReadItem(folder, Replace(folder.substr(root.size() + 1), L"\\", L"/"), true));
    for (const auto& file : ListFiles(root, true)) items.push_back(ReadItem(file, Replace(file.substr(root.size() + 1), L"\\", L"/"), false));
    return items;
}

bool Same(const std::vector<SaveItem>& expected, std::vector<SaveItem> actual) {
    if (expected.size() != actual.size()) return false;
    for (const auto& e : expected) {
        SaveItem* a = FindItem(actual, e.IsFolder, e.Path);
        if (!a || (!e.IsFolder && a->Data != e.Data) || a->Streams.size() != e.Streams.size()) return false;
        for (const auto& stream : e.Streams) {
            auto it = a->Streams.find(stream.first);
            if (it == a->Streams.end() || it->second != stream.second) return false;
        }
    }
    return true;
}

using Reader = std::function<std::optional<Bytes>(const wstring& name, long long maximumBytes)>;

bool IsStreamName(const wstring& name) {
    if (name.empty() || name.size() > 64 || name == L"." || name == L"..") return false;
    for (wchar_t c : name)
        if (c < 32 || wcschr(L"\"<>|:*?\\/", c)) return false;
    return true;
}

std::optional<wstring> Canonical(const std::optional<wstring>& path, bool folder) {
    if (!path) return std::nullopt;
    for (const auto& known : folder ? kFolders : kFiles)
        if (IEquals(known, *path)) return known;
    return std::nullopt;
}

Bytes ReadVerified(const Reader& read, const wstring& name, const Json* record, long long maximumBytes) {
    auto bytes = read(name, maximumBytes);
    if (!bytes) Damaged(name + L" is missing.");
    if (JLong(record, L"size") != (long long)bytes->size() || !IEquals(JStr(record, L"sha256").value_or(L""), Sha256Bytes(bytes->data(), bytes->size())))
        Damaged(name + L" does not match its checksum.");
    return *bytes;
}

std::vector<SaveItem> ReadExport(const Reader& read) {
    Json manifest;
    try {
        auto text = read(kManifestEntry, kMaximumFileBytes);
        manifest = ParseJson(Widen(std::string(text->begin(), text->end())));
        if (!manifest.IsObject()) Damaged();
    } catch (const Error& error) {
        if (error.kind == ErrorKind::InvalidData && error.message.rfind(L"Invalid JSON", 0) == 0) Damaged();
        throw;
    }
    if (JStr(&manifest, L"format").value_or(L"") != kFormatName) Fail(ErrorKind::InvalidData, L"This is not a Nebula saves archive.");
    long long version = JLong(&manifest, L"version");
    if (version > kFormatVersion) Fail(ErrorKind::InvalidData, L"This archive needs a newer version of Nebula.");
    const Json* listed = JList(&manifest, L"items");
    if (version != kFormatVersion || !listed) Damaged();
    std::vector<SaveItem> items;
    for (const auto& record : listed->items) {
        auto type = JStr(&record, L"type");
        if (!record.IsObject() || (type != L"folder" && type != L"file")) Damaged();
        bool folder = type == L"folder";
        auto path = Canonical(JStr(&record, L"path"), folder);
        if (!path) continue;  // a path this version does not keep
        if (FindItem(items, folder, *path)) Damaged();
        SaveItem item;
        item.Path = *path;
        item.IsFolder = folder;
        if (!folder) item.Data = ReadVerified(read, DataEntry(*path), &record, kMaximumFileBytes);
        if (const Json* streams = JList(&record, L"streams")) {
            for (const auto& stream : streams->items) {
                auto name = JStr(&stream, L"name");
                if (!stream.IsObject() || !name || !IsStreamName(*name) || item.Streams.count(*name)) Damaged();
                item.Streams[*name] = ReadVerified(read, StreamEntry(*path, *name), &stream, kMaximumStreamBytes);
            }
        }
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<SaveItem> ReadPlainArchive(const std::vector<wstring>& names, const Reader& read) {
    std::optional<wstring> prefix;
    for (const auto& name : names) {
        if (!EndsWith(name, kGameDataPath, true)) continue;
        wstring head = name.substr(0, name.size() - kGameDataPath.size());
        if ((head.empty() || EndsWith(head, L"/")) && (!prefix || head.size() < prefix->size())) prefix = head;
    }
    std::vector<SaveItem> items;
    if (prefix) {
        for (const auto& file : kFiles) {
            auto bytes = read(*prefix + file, kMaximumFileBytes);
            if (bytes) {
                SaveItem item;
                item.Path = file;
                item.Data = std::move(*bytes);
                items.push_back(std::move(item));
            }
        }
        return items;
    }
    std::optional<wstring> save;
    for (const auto& name : names)
        if ((IEquals(name, L"GameData.bin") || EndsWith(name, L"/GameData.bin", true)) && (!save || name.size() < save->size())) save = name;
    if (!save) Fail(ErrorKind::InvalidData, L"This archive does not contain a Super Mario Galaxy save.");
    SaveItem game;
    game.Path = kGameDataPath;
    game.Data = read(*save, kMaximumFileBytes).value_or(Bytes());
    items.push_back(std::move(game));
    auto banner = read(save->substr(0, save->size() - 12) + L"banner.bin", kMaximumFileBytes);
    if (banner) {
        SaveItem item;
        item.Path = kBannerPath;
        item.Data = std::move(*banner);
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<SaveItem> ReadArchive(const wstring& path) {
    std::unique_ptr<ZipReader> archive;
    try {
        archive = ZipReader::OpenFile(path);
    } catch (const Error& error) {
        if (error.kind == ErrorKind::InvalidData) Fail(ErrorKind::InvalidData, L"This is not a zip archive, or the archive is damaged.");
        throw;
    }
    std::map<wstring, const ZipReader::Entry*> entries;  // case-insensitive (lower-cased keys)
    std::vector<wstring> names;
    for (const auto& entry : archive->Entries()) {
        wstring name = Replace(Widen(entry.name), L"\\", L"/");
        if (EndsWith(name, L"/")) continue;
        if (entries.count(ToLower(name))) Fail(ErrorKind::InvalidData, L"The archive holds " + name + L" twice.");
        entries[ToLower(name)] = &entry;
        names.push_back(name);
    }
    Reader read = [&](const wstring& name, long long maximumBytes) -> std::optional<Bytes> {
        auto it = entries.find(ToLower(name));
        if (it == entries.end()) return std::nullopt;
        if ((long long)it->second->size > maximumBytes) Fail(ErrorKind::InvalidData, Widen(it->second->name) + L" is too large for a save file.");
        try {
            return archive->Read(*it->second, (unsigned long long)maximumBytes);
        } catch (const Error& error) {
            if (error.kind == ErrorKind::InvalidData) Damaged();
            throw;
        }
    };
    return entries.count(ToLower(kManifestEntry)) ? ReadExport(read) : ReadPlainArchive(names, read);
}

Reader FolderReader(const wstring& root) {
    return [root](const wstring& name, long long maximumBytes) -> std::optional<Bytes> {
        wstring file = PathCombine(root, Slashes(name));
        if (!FileExists(file)) return std::nullopt;
        if (FileSize(file) > maximumBytes) Fail(ErrorKind::InvalidData, name + L" is too large for a save file.");
        return ReadAllBytes(file);
    };
}

std::vector<SaveItem> ReadFolder(const wstring& root) {
    for (const auto& candidate : {root, PathCombine(root, L"saves"), PathCombine(root, L"save-data"), PathCombine(root, L"nand")})
        if (FileExists(HostPath(candidate, kGameDataPath))) return ReadTree(FinalPath(candidate));
    wstring save = PathCombine(root, L"GameData.bin");
    if (!FileExists(save)) Fail(ErrorKind::InvalidData, L"This folder does not contain a Super Mario Galaxy save.");
    std::vector<SaveItem> items = {ReadItem(save, kGameDataPath, false)};
    wstring banner = PathCombine(root, L"banner.bin");
    if (FileExists(banner)) items.push_back(ReadItem(banner, kBannerPath, false));
    return items;
}

std::vector<SaveItem> ReadSource(const wstring& source) {
    if (DirExists(source)) {
        wstring root = FinalPath(FullPath(source));
        if (FileExists(PathCombine(root, kManifestEntry))) return ReadExport(FolderReader(root));
        return ReadFolder(root);
    }
    if (!FileExists(source)) Fail(ErrorKind::NotFound, L"The file was not found.");
    return ReadArchive(source);
}

void WriteArchive(const wstring& path, const std::vector<SaveItem>& items) {
    Json listed = Json::Array();
    for (const auto& item : items) {
        Json record = Json::Object();
        record.Set(L"path", Json::Str(item.Path));
        record.Set(L"type", Json::Str(item.IsFolder ? L"folder" : L"file"));
        if (!item.IsFolder) {
            record.Set(L"size", Json::Int((long long)item.Data.size()));
            record.Set(L"sha256", Json::Str(Sha256Bytes(item.Data.data(), item.Data.size())));
        }
        Json streams = Json::Array();
        for (const auto& stream : item.Streams) {
            Json s = Json::Object();
            s.Set(L"name", Json::Str(stream.first));
            s.Set(L"size", Json::Int((long long)stream.second.size()));
            s.Set(L"sha256", Json::Str(Sha256Bytes(stream.second.data(), stream.second.size())));
            streams.Add(std::move(s));
        }
        record.Set(L"streams", std::move(streams));
        listed.Add(std::move(record));
    }
    Json manifest = Json::Object();
    manifest.Set(L"format", Json::Str(kFormatName));
    manifest.Set(L"version", Json::Int(kFormatVersion));
    manifest.Set(L"nebula", Json::Str(build::kVersion));
    manifest.Set(L"items", std::move(listed));
    ZipWriter zip(path);
    std::string text = Narrow(SerializeJson(manifest) + L"\n");
    zip.Add(Narrow(kManifestEntry), text.data(), text.size());
    for (const auto& item : items) {
        if (!item.IsFolder) zip.Add(Narrow(DataEntry(item.Path)), item.Data.data(), item.Data.size());
        for (const auto& stream : item.Streams) zip.Add(Narrow(StreamEntry(item.Path, stream.first)), stream.second.data(), stream.second.size());
    }
    zip.Finish();
}

std::vector<SaveItem> WithFolders(std::vector<SaveItem>& items) {
    std::vector<SaveItem> result;
    for (const auto& folder : kFolders) {
        if (SaveItem* listed = FindItem(items, true, folder)) {
            result.push_back(*listed);
            continue;
        }
        bool holds = std::any_of(items.begin(), items.end(), [&](const SaveItem& i) {
            return !i.IsFolder && (folder.empty() || StartsWith(i.Path, folder + L"/"));
        });
        if (holds) {
            SaveItem item;
            item.Path = folder;
            item.IsFolder = true;
            result.push_back(item);
        }
    }
    for (const auto& file : kFiles)
        if (SaveItem* listed = FindItem(items, false, file)) result.push_back(*listed);
    return result;
}

void LeaveOutInvalid(std::vector<SaveItem>& items, const wstring& path, bool (*valid)(const Bytes&), const wstring& note,
                     std::vector<wstring>& notes) {
    auto it = std::find_if(items.begin(), items.end(), [&](const SaveItem& i) { return !i.IsFolder && i.Path == path; });
    if (it == items.end() || valid(it->Data)) return;
    items.erase(it);
    notes.push_back(note);
}

void Stage(const wstring& staging, const std::vector<SaveItem>& items) {
    for (const auto& item : items) {
        wstring host = HostPath(staging, item.Path);
        if (item.IsFolder) CreateDirs(host);
        else WriteAllBytes(host, item.Data.data(), item.Data.size(), true, true);
        for (const auto& stream : item.Streams) WriteNamedStream(host, stream.first, stream.second);
    }
}

std::optional<wstring> ReplaceSaves(const wstring& target, const wstring& staging) {
    std::optional<wstring> backup;
    if (DirExists(target)) {
        if (!IsDirectoryEmpty(target)) {
            wstring stamp = target + L"-backup-" + LocalStamp();
            wstring candidate = stamp;
            for (int number = 2; DirExists(candidate) || FileExists(candidate); number++) candidate = stamp + L"-" + Num(number);
            MoveDirectoryInPlace(target, candidate);
            backup = candidate;
        } else {
            RemoveEmptyDirectory(target);
        }
    }
    try {
        MoveDirectoryInPlace(staging, target);
    } catch (...) {
        if (backup && !DirExists(target)) MoveDirectoryInPlace(*backup, target);
        throw;
    }
    return backup;
}

bool AnyInterrupted(const wstring& root) {
    for (const auto& path : ListFiles(root, true))
        if (IFind(path, L".__galaxy_isfs_") != wstring::npos) return true;
    for (const auto& path : ListDirs(root, true))
        if (IFind(path, L".__galaxy_isfs_") != wstring::npos) return true;
    return false;
}
}  // namespace

wstring Export(const wstring& folder) {
    if (RuntimeLeaseHeld()) Fail(ErrorKind::InvalidOperation, L"Close the game before exporting saves.");
    wstring root = paths::Saves();
    std::vector<SaveItem> items = DirExists(root) ? ReadTree(FinalPath(root)) : std::vector<SaveItem>();
    SaveItem* save = FindItem(items, false, kGameDataPath);
    if (!save) Fail(ErrorKind::InvalidOperation, L"There are no saves to export yet.");
    if (!format::IsGameData(save->Data)) Fail(ErrorKind::InvalidData, L"GameData.bin is not a valid Super Mario Galaxy save.");
    bool interrupted = std::any_of(items.begin(), items.end(), [](const SaveItem& item) {
        return std::any_of(item.Streams.begin(), item.Streams.end(),
                           [](const auto& s) { return s.first != L"galaxy.isfs.meta" && StartsWith(s.first, L"galaxy."); });
    });
    if (interrupted || AnyInterrupted(FinalPath(root)))
        Fail(ErrorKind::InvalidOperation, L"A save was interrupted. Start and close the game once, then export again.");

    wstring stamp = LocalStamp();
    wstring path = PathCombine(folder, L"Nebula-Saves-" + stamp + L".zip");
    for (int number = 2; FileExists(path); number++) path = PathCombine(folder, L"Nebula-Saves-" + stamp + L"-" + Num(number) + L".zip");
    wstring partial = path + L".partial";
    try {
        WriteArchive(partial, items);
        if (!Same(items, ReadArchive(partial))) Fail(ErrorKind::Io, L"The written archive does not match the saves.");
        MoveFileOrThrow(partial, path);
    } catch (...) {
        if (FileExists(partial)) DeleteFileQuietly(partial);
        throw;
    }
    return path;
}

std::optional<wstring> Import(const wstring& source, const std::function<bool()>& confirmReplace) {
    if (RuntimeLeaseHeld()) Fail(ErrorKind::InvalidOperation, L"Close the game before importing saves.");
    std::vector<SaveItem> items = ReadSource(source);
    std::vector<wstring> notes;
    SaveItem* save = FindItem(items, false, kGameDataPath);
    if (!save) Fail(ErrorKind::InvalidData, L"No Super Mario Galaxy save (GameData.bin) was found.");
    if (!format::IsGameData(save->Data)) Fail(ErrorKind::InvalidData, L"GameData.bin is not a valid Super Mario Galaxy save.");
    // The runtime would set these aside as well.
    LeaveOutInvalid(items, kBannerPath, format::IsBanner, L"The save banner (banner.bin) is not valid and was left out.", notes);
    LeaveOutInvalid(items, kMiiDatabasePath, format::IsMiiDatabase, L"The Mii data (RFL_DB.dat) is not valid and was left out.", notes);
    items = WithFolders(items);

    wstring saves = FullPath(paths::Saves());
    wstring target = DirExists(saves) ? FinalPath(saves) : saves;
    CreateDirs(DirName(target));
    wstring staging = target + L".import-" + NewGuidN().substr(0, 8);
    std::optional<wstring> backup;
    bool declined = false;
    try {
        Stage(staging, items);
        if (!Same(items, ReadWholeTree(staging))) Fail(ErrorKind::Io, L"The staged saves do not match the source; nothing was changed.");
        if (confirmReplace && FileExists(HostPath(target, kGameDataPath)) && !confirmReplace()) {
            declined = true;
        } else {
            if (RuntimeLeaseHeld()) Fail(ErrorKind::InvalidOperation, L"Close the game before importing saves.");
            backup = ReplaceSaves(target, staging);
        }
    } catch (...) {
        try { DeleteTree(staging); } catch (const Error&) {}
        throw;
    }
    try { DeleteTree(staging); } catch (const Error&) {}
    if (declined) return std::nullopt;
    wstring message = L"Saves imported from " + FileName(TrimEnd(source, L"\\/")) + L".";
    for (const auto& note : notes) message += L"\r\n" + note;
    if (backup) message += L"\r\nThe previous saves are in " + *backup + L".";
    return message;
}
}  // namespace saves

// ---- launching ---------------------------------------------------------------------------------------------------

LaunchSession::LaunchSession() {
    App = TrimEnd(ModuleDirectory(), L"\\");
    Portable = FileExists(PathCombine(App, L"portable.json"));
    Root = Portable ? paths::RegisteredInstallRoot().value_or(paths::DefaultInstallRoot()) : DirName(DirName(App));
    Content = PathCombine(Root, L"content");
    if (FileExists(PathCombine(App, L"content", L"game.pak"))) Content = PathCombine(App, L"content");
    try {
        if (!FileExists(SettingsFile())) return;
        Json settings = LoadJson(SettingsFile());
        auto saved = JStr(&settings, L"content");
        if (saved && FileExists(PathCombine(*saved, L"game.pak"))) Content = *saved;
        SetupComplete = JLong(&settings, L"settingsVersion") >= 3;
        // A recording applies to the next launch only (settings version 4).
        long long recording = JLong(&settings, L"recording");
        if (JLong(&settings, L"settingsVersion") >= 4 && recording >= 0 && recording <= 2) Recording = RecordingMode(recording);
    } catch (const std::exception&) {
    }
}

wstring LaunchSession::SettingsFile() const {
    return Portable ? PathCombine(App, L"portable-settings.json") : paths::SettingsFile();
}

void LaunchSession::Save() {
    Json settings = Json::Object();
    settings.Set(L"settingsVersion", Json::Int(4));
    settings.Set(L"content", Json::Str(Content));
    settings.Set(L"recording", Json::Int(int(Recording)));
    SaveJson(SettingsFile(), settings);
    SetupComplete = true;
}

namespace {
void SetVar(Environment& environment, const wstring& name, const wstring& value) {
    for (auto& pair : environment)
        if (IEquals(pair.first, name)) { pair.second = value; return; }
    environment.emplace_back(name, value);
}

std::optional<wstring> GetVar(const Environment& environment, const wstring& name) {
    for (const auto& pair : environment)
        if (IEquals(pair.first, name)) return pair.second;
    return std::nullopt;
}

void ApplyRecording(Environment& env, RecordingMode mode) {
    bool record = mode != RecordingMode::None, detailed = mode == RecordingMode::Detailed;
    for (const wchar_t* flag : {L"GALAXY_TRACE_PRESENT_STATS", L"GALAXY_MONITOR_POINTER_LATENCY", L"GALAXY_MONITOR_FRAME_TAILS",
                                L"GALAXY_MONITOR_DISPLAY_LAYOUT", L"GALAXY_FRAME_TELEMETRY"})
        SetVar(env, flag, record ? L"1" : L"0");
    SetVar(env, L"GALAXY_TRACE_IOS_ANOMALIES", record ? L"1" : L"0");
    SetVar(env, L"GALAXY_DIAGNOSTIC_IOS_COUNTERS", detailed ? L"1" : L"0");
    SetVar(env, L"GALAXY_TRACE_NATIVE_THP_BOUNDARY", detailed ? L"1" : L"0");
    SetVar(env, L"GALAXY_TRACE_GX_STALLS", detailed ? L"1" : L"0");
    SetVar(env, L"GALAXY_SIM_PROFILE", detailed ? L"1" : L"0");
    SetVar(env, L"GALAXY_FRAME_TIMELINE", record ? L"1" : L"0");
    SetVar(env, L"GALAXY_FRAME_TIMELINE_EVENTS", L"4000000");
    SetVar(env, L"GALAXY_GPU_TIMESTAMPS", detailed ? L"1" : L"0");
    SetVar(env, L"GALAXY_TRACE_GX_MICROPROFILE", L"0");
    SetVar(env, L"GALAXY_GX_FRAME_TIMING_SAMPLE", detailed ? L"32" : L"0");
    SetVar(env, L"GALAXY_GX_PSO_CYCLES", detailed ? L"1" : L"0");
    SetVar(env, L"GALAXY_TRACE_GX_STALL_US", L"20000");
}

// The runtime reads display and graphics settings from config.ini; only paths and diagnostics travel on the command
// line and in the environment.
Environment LaunchEnvironment(const wstring& app, const wstring& saves, RecordingMode recording) {
    Environment env;
    for (auto& pair : CurrentEnvironment())
        if (!StartsWith(pair.first, L"GALAXY_", true)) env.push_back(pair);
    Json list = ParseJson(ReadAllText(PathCombine(app, L"runtime-env.json")));
    for (const auto& entry : list.items) SetVar(env, JStr(&entry, L"name").value_or(L""), JStr(&entry, L"value").value_or(L""));
    SetVar(env, L"GALAXY_NAND_ROOT", saves);
    SetVar(env, L"GALAXY_STREAMLINE_DIR", dlss::Folder());
    SetVar(env, L"GALAXY_NATIVE_THP_VIDEO_DECODE", L"1");
    SetVar(env, L"GALAXY_DUSK_DSP_HLE", L"0");
    ApplyRecording(env, recording);
    return env;
}

void ValidatePair(const wstring& app) {
    wstring manifest = PathCombine(app, L"validation-pair.json");
    if (!FileExists(manifest)) return;  // Standard setup validates its own payload.
    Json loaded = LoadJson(manifest);
    const Json* files = JObj(&loaded, L"files");
    if (!files) Fail(ErrorKind::InvalidData, L"The portable build manifest is missing.");
    wstring prefix = TrimEnd(FullPath(app), L"\\") + L"\\";
    for (const auto& file : files->members) {
        wstring path = FullPath(PathCombine(app, file.first));
        if (!StartsWith(path, prefix, true) || !FileExists(path) || !IEquals(Sha256File(path), JScalarText(file.second)))
            Fail(ErrorKind::InvalidData, L"Portable build file mismatch: " + file.first + L". Extract the complete ZIP again.");
    }
}

bool SameDigests(const std::map<wstring, wstring>& a, const std::map<wstring, wstring>& b) { return a == b; }

// Called once per portable beta before opening the shared saves and cache.
void PreserveUserData(const wstring& dataRoot, const wstring& backupKey) {
    if (backupKey.find_first_of(L"\"<>|:*?\\/") != wstring::npos || backupKey == L"." || backupKey == L"..")
        Fail(ErrorKind::Argument, L"Invalid backup identity.");
    wstring backup = PathCombine(dataRoot, L"backups", backupKey);
    wstring completed = PathCombine(backup, L"backup.json");
    if (FileExists(completed)) return;
    CreateDirs(backup);
    HANDLE owner = CreateFileW(PathCombine(backup, L"owner.lock").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (owner == INVALID_HANDLE_VALUE) FailWin32(GetLastError(), PathCombine(backup, L"owner.lock"));
    struct Closer { HANDLE h; ~Closer() { CloseHandle(h); } } closer{owner};
    if (FileExists(completed)) return;
    wstring snapshot = PathCombine(backup, UtcStamp(true) + L"-" + NewGuidN());
    CreateDirs(snapshot);
    for (const wchar_t* name : {L"saves", L"shadercache"}) {
        wstring source = PathCombine(dataRoot, name), target = PathCombine(snapshot, name);
        if (!DirExists(source)) continue;
        CopyTreeWithStreams(source, target, nullptr);
        std::vector<wstring> entries = {source};
        for (const auto& d : ListDirs(source, true)) entries.push_back(d);
        for (const auto& f : ListFiles(source, true)) entries.push_back(f);
        for (const auto& entry : entries) {
            wstring relative = TrimStart(entry.substr(source.size()), L"\\");
            if (!SameDigests(StreamDigests(entry), StreamDigests(PathCombine(target, relative))))
                Fail(ErrorKind::Io, L"Save/cache backup verification failed: " + relative + L". The game was not started.");
        }
    }
    for (const wchar_t* name : {L"settings.json", L"config.ini"}) {
        wstring source = PathCombine(dataRoot, name), target = PathCombine(snapshot, name);
        if (!FileExists(source)) continue;
        if (!CopyFileW(source.c_str(), target.c_str(), TRUE)) Fail(ErrorKind::Io, wstring(L"Settings backup failed: ") + name);
        if (!SameDigests(StreamDigests(source), StreamDigests(target))) Fail(ErrorKind::Io, wstring(L"Settings backup verification failed: ") + name);
    }
    Json record = Json::Object();
    record.Set(L"createdUtc", Json::Str(UtcNowIso()));
    record.Set(L"source", Json::Str(dataRoot));
    record.Set(L"version", Json::Str(backupKey));
    record.Set(L"snapshot", Json::Str(snapshot));
    SaveJson(completed, record);
}

wstring CreateSession(const wstring& kind) {
    wstring parent = PathCombine(paths::Sessions(), kind);
    CreateDirs(parent);
    for (int attempt = 0; attempt < 8; attempt++) {
        wstring path = PathCombine(parent, UtcStamp(true) + L"-" + NewGuidN());
        if (!DirExists(path)) {
            CreateDirs(path);
            return path;
        }
    }
    Fail(ErrorKind::Io, L"Could not create a new session folder.");
}

struct Started {
    HANDLE process;
    DWORD id;
};

Started StartRuntime(const wstring& app, const Environment& env, const wstring& commandLine, HANDLE output, HANDLE error) {
    wstring exe = PathCombine(app, L"NebulaRuntime.exe");
    wstring line = L"\"" + exe + L"\" " + commandLine;
    std::vector<wchar_t> mutableLine(line.begin(), line.end());
    mutableLine.push_back(L'\0');
    std::vector<wchar_t> block = EnvironmentBlock(env);
    STARTUPINFOW startup{sizeof(startup)};
    BOOL inherit = FALSE;
    if (output && error) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = error;
        startup.hStdInput = nullptr;
        inherit = TRUE;
    }
    PROCESS_INFORMATION process{};
    // NebulaRuntime.exe is a console program: CREATE_NO_WINDOW keeps a console window from opening beside the game.
    if (!CreateProcessW(exe.c_str(), mutableLine.data(), nullptr, nullptr, inherit, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        block.data(), app.c_str(), &startup, &process))
        FailWin32(GetLastError(), L"Could not start the game");
    CloseHandle(process.hThread);
    return {process.hProcess, process.dwProcessId};
}

HRESULT CALLBACK NoticeCallback(HWND hwnd, UINT message, WPARAM, LPARAM, LONG_PTR) {
    if (message == TDN_CREATED) SetForegroundWindow(hwnd);
    return S_OK;
}

// The message at the end of a recording: OK, and Open folder, which shows the recording in Explorer (selected,
// ready to attach to a report).
void ShowRecordingNotice(const wstring& message, bool warning, const wstring& saved) {
    // Zero-width spaces after the path separators let a long path wrap instead of being cut short.
    wstring text = Replace(message, L"\\", L"\\​");
    if (EndsWith(saved, L".zip", true)) text += L"\r\n\r\nAttach this ZIP file to your report.";
    TASKDIALOG_BUTTON buttons[] = {{101, L"Open folder"}};
    TASKDIALOGCONFIG config{sizeof(config)};
    // No hInstance: the TD_ icons are Windows' own, not resources of this program.
    config.dwFlags = TDF_SIZE_TO_CONTENT | TDF_ALLOW_DIALOG_CANCELLATION;
    config.pszWindowTitle = L"Nebula recording";
    config.pszMainIcon = warning ? TD_WARNING_ICON : TD_INFORMATION_ICON;
    config.pszContent = text.c_str();
    config.cButtons = 1;
    config.pButtons = buttons;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.nDefaultButton = IDOK;
    config.pfCallback = NoticeCallback;
    int pressed = 0;
    if (FAILED(TaskDialogIndirect(&config, &pressed, nullptr, nullptr))) {
        MessageBoxW(nullptr, text.c_str(), L"Nebula recording", MB_OK | MB_SETFOREGROUND | (warning ? MB_ICONWARNING : MB_ICONINFORMATION));
        return;
    }
    if (pressed != 101) return;
    try {
        wstring explorer = PathCombine(KnownFolder(FOLDERID_Windows), L"explorer.exe");
        StartProgram(explorer, L"/select,\"" + FullPath(saved) + L"\"", L"");
    } catch (const std::exception& error) {
        MessageBoxW(nullptr, (L"Could not open the folder: " + ErrorText(error) + L"\r\n" + saved).c_str(), L"Nebula recording",
                    MB_OK | MB_ICONERROR);
    }
}

struct RecordingJob {
    HANDLE process;
    wstring session;
    HANDLE output, error;
};

DWORD WINAPI RecordingWorker(void* parameter) {
    std::unique_ptr<RecordingJob> recording(static_cast<RecordingJob*>(parameter));
    ULONGLONG started = GetTickCount64();
    wstring csv = L"elapsedSeconds,cpuSeconds,workingSetBytes\n";
    HANDLE samples = CreateFileW(PathCombine(recording->session, L"process.csv").c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    auto write = [&](const wstring& text) {
        if (samples == INVALID_HANDLE_VALUE) return;
        std::string utf8 = Narrow(text);
        DWORD written = 0;
        WriteFile(samples, utf8.data(), DWORD(utf8.size()), &written, nullptr);
    };
    write(csv);
    while (WaitForSingleObject(recording->process, 1000) == WAIT_TIMEOUT) {
        FILETIME created, exited, kernel, user;
        PROCESS_MEMORY_COUNTERS memory{sizeof(memory)};
        if (!GetProcessTimes(recording->process, &created, &exited, &kernel, &user) ||
            !GetProcessMemoryInfo(recording->process, &memory, sizeof(memory)))
            break;
        auto ticks = [](FILETIME t) { return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
        double elapsed = double(GetTickCount64() - started) / 1000.0;
        double cpu = double(ticks(kernel) + ticks(user)) / 1e7;
        write(Fixed(elapsed, 3) + L"," + Fixed(cpu, 6) + L"," + Num((long long)memory.WorkingSetSize) + L"\n");
    }
    WaitForSingleObject(recording->process, INFINITE);
    if (samples != INVALID_HANDLE_VALUE) CloseHandle(samples);
    CloseHandle(recording->output);
    CloseHandle(recording->error);
    DWORD exitCode = 0;
    GetExitCodeProcess(recording->process, &exitCode);
    CloseHandle(recording->process);
    Json exit = Json::Object();
    exit.Set(L"exitCode", Json::Int(int(exitCode)));
    exit.Set(L"elapsedSeconds", Json::Double(double(GetTickCount64() - started) / 1000.0));
    exit.Set(L"endUtc", Json::Str(UtcNowIso()));
    try { SaveJson(PathCombine(recording->session, L"exit.json"), exit); } catch (const std::exception&) {}
    wstring session = recording->session;
    wstring result = L"Recording saved to " + session;
    wstring saved = session;
    try {
        ZipWriter zip(session + L".zip");
        for (const auto& file : ListFiles(session, true)) zip.AddFile(Narrow(Replace(file.substr(session.size() + 1), L"\\", L"/")), file);
        zip.Finish();
        result = L"Recording saved to " + session + L".zip";
        saved = session + L".zip";
    } catch (const std::exception&) {
        // Logs remain available if the ZIP cannot be written.
        DeleteFileQuietly(session + L".zip");
    }
    if (exitCode != 0) result = L"The game exited with code " + Num(int(exitCode)) + L".\r\n" + result;
    ShowRecordingNotice(result, exitCode != 0, saved);
    return 0;
}
}  // namespace

bool LaunchSession::EndRunningGame(void* ownerWindow) {
    wstring exe = PathCombine(App, L"NebulaRuntime.exe");
    std::vector<DWORD> running = ProcessesAt(L"NebulaRuntime", exe);
    if (running.empty()) return false;
    if (MessageBoxW(static_cast<HWND>(ownerWindow), L"Nebula is already running. End it and start again?", L"Nebula",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return false;
    for (DWORD id : running) {
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, id);
        if (!process) continue;
        TerminateProcess(process, 1);
        WaitForSingleObject(process, 5000);
        CloseHandle(process);
    }
    for (int i = 0; i < 50 && RuntimeLeaseHeld(); i++) Sleep(100);
    return !RuntimeLeaseHeld();
}

std::optional<wstring> LaunchSession::TryLaunch(void* ownerWindow) {
    try {
        if (!FileExists(PathCombine(Content, L"game.pak")))
            return wstring(L"Nebula could not find the game content (game.pak). Choose it in Nebula settings.");
        if (RuntimeLeaseHeld() && !EndRunningGame(ownerWindow)) return wstring(L"Nebula is already running.");
        ValidatePair(App);
        if (Portable) PreserveUserData(paths::DataRoot(), L"portable-" + wstring(build::kVersion));
        CreateDirs(paths::Saves());
        Environment env = LaunchEnvironment(App, FinalPath(paths::Saves()), Recording);
        wstring commandLine = QuoteArg(Content) + L" " + QuoteArg(PathCombine(App, L"RMGE01_game.dll"));
        if (Recording == RecordingMode::None) {
            Started game = StartRuntime(App, env, commandLine, nullptr, nullptr);
            CloseHandle(game.process);
            return std::nullopt;
        }
        RecordingMode mode = Recording;
        Recording = RecordingMode::None;
        try { Save(); } catch (const std::exception&) {}
        Recording = mode;
        StartRecorded(std::move(env), commandLine);
        return std::nullopt;
    } catch (const std::exception& error) {
        return ErrorText(error);
    }
}

void LaunchSession::StartRecorded(Environment env, const wstring& commandLine) {
    wstring session = CreateSession(L"diagnostics");
    SetVar(env, L"GALAXY_CRASH_DUMP_DIR", session);
    SetVar(env, L"GALAXY_FRAME_TIMELINE_PATH", PathCombine(session, L"timeline.bin"));
    Json launchFiles = Json::Object();
    for (const wchar_t* name : {L"NebulaRuntime.exe", L"RMGE01_game.dll", L"RMGE01_home_button.dll", L"RMGE01_dsp.dll",
                                L"RMGE01_boot_image.bin", L"runtime-env.json", L"Nebula.exe"}) {
        wstring path = PathCombine(App, name);
        if (FileExists(path)) launchFiles.Set(name, Json::Str(Sha256File(path)));
    }
    Json launchEnvironment = Json::Object();
    for (const auto& pair : env)
        if (StartsWith(pair.first, L"GALAXY_", true)) launchEnvironment.Set(pair.first, Json::Str(pair.second));
    GraphicsSettings graphics = GraphicsSettings::Load();
    Json info = Json::Object();
    info.Set(L"startUtc", Json::Str(UtcNowIso()));
    info.Set(L"pid", Json::Null());
    info.Set(L"monitoring", Json::Bool(true));
    info.Set(L"recording", Json::Str(Recording == RecordingMode::Detailed ? L"Detailed" : L"Lightweight"));
    info.Set(L"content", Json::Str(Content));
    info.Set(L"saves", Json::Str(GetVar(env, L"GALAXY_NAND_ROOT").value_or(L"")));
    info.Set(L"app", Json::Str(App));
    info.Set(L"displayMode", Json::Int(graphics.DisplayMode));
    info.Set(L"window", Json::Str(Num(graphics.WindowWidth) + L"x" + Num(graphics.WindowHeight)));
    info.Set(L"aspect", Json::Int(graphics.Aspect));
    info.Set(L"efbScale", Json::Int(graphics.MatchOutput ? 0 : graphics.EfbScale));
    info.Set(L"detailedRendererTimings", Json::Bool(Recording == RecordingMode::Detailed));
    info.Set(L"launchFileSha256", std::move(launchFiles));
    info.Set(L"environment", std::move(launchEnvironment));
    SaveJson(PathCombine(session, L"session.json"), info);

    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE output = CreateFileW(PathCombine(session, L"runtime.stdout.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE error = CreateFileW(PathCombine(session, L"runtime.stderr.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE || error == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
        if (error != INVALID_HANDLE_VALUE) CloseHandle(error);
        FailWin32(code, session);
    }
    Started game;
    try {
        game = StartRuntime(App, env, commandLine, output, error);
    } catch (...) {
        CloseHandle(output);
        CloseHandle(error);
        throw;
    }
    info.Set(L"pid", Json::Int(game.id));
    try { SaveJson(PathCombine(session, L"session.json"), info); } catch (const Error&) {}  // the pre-launch record remains
    auto* recording = new RecordingJob{game.process, session, output, error};
    RecordingThread = CreateThread(nullptr, 0, RecordingWorker, recording, 0, nullptr);
    if (!RecordingThread) {
        delete recording;
        FailWin32(GetLastError(), L"Could not start the recording");
    }
}

}  // namespace nebula::launcher
