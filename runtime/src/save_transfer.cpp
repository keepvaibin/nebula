// SPDX-License-Identifier: GPL-3.0-only
// Save import and export (save_transfer.h).
//
// Layouts read here: the Wii SD card save (data.bin) as Dolphin's WiiSave.cpp reads it: a 0xF0C0-byte header (title,
// banner size, permissions, MD5 over the header with its MD5 field blanked, the banner) under AES-128-CBC with the SD
// key and IV; a 0x80-byte Bk header in the clear (0x70, "Bk" 0x0001, file count, files' size, title); each file a
// 0x80-byte header in the clear (0x03adf17e, size, permissions, attributes, type, name, IV) and its data padded to 64
// bytes, under the SD key and the file's own IV; then the console's signature and certificates (not read). Nebula.sav:
// SaveDataHandler.cpp. GameData.bin: SaveFileFormat.hpp (the game's own check). A Nebula saves archive:
// installer/src/Launcher/SaveArchive.cs of 1.0.2 and the v2 beta.
#include "galaxy/save_transfer.h"

#include "galaxy/atomic_file.h"
#include "galaxy/mii_formats.h"
#include "galaxy/path_text.h"

#include "Game/System/SaveFileFormat.hpp"

#include <openssl/aes.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#ifndef Z_PREFIX
#define Z_PREFIX
#endif
#include <zlib.h>
#undef Byte

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <utility>

namespace galaxy::save_transfer {

namespace fs = std::filesystem;
using Bytes = std::vector<std::byte>;
using ByteSpan = std::span<const std::byte>;

namespace {

constexpr std::array<std::string_view, 4> kReleaseCodes = {"RMGE", "RMGP", "RMGJ", "RMGK"};
constexpr std::string_view kManifestName = "nebula-saves.json";
constexpr std::string_view kFormatName = "nebula-saves";
constexpr std::int64_t kFormatVersion = 1;
// Nebula's own Mii file (mii_import.cpp): its name, and its first line's format name (kLibraryFormat, any version).
constexpr std::string_view kMiisFilePath = "nebula-miis.txt";
constexpr std::string_view kMiisFileFormat = "nebula-mii-library ";
constexpr std::string_view kNativeSave = "Nebula.sav";
constexpr std::string_view kGameData = "GameData.bin";
constexpr std::string_view kBanner = "banner.bin";
constexpr std::string_view kModFolder = "luma";           // save_files.h kModFolder
constexpr std::string_view kReplacedFolder = "replaced";  // save_files.h kReplacedFolder
constexpr std::array<std::string_view, 3> kCopies = {"", ".1", ".2"};  // the game's read order, newest first
constexpr std::size_t kMaxModFiles = 1024;

// The SD card save's key and IV (the Wii's public SD key) and the MD5 the header's own MD5 field is replaced by.
constexpr std::uint8_t kSdKey[16] = {0xab, 0x01, 0xb9, 0xd8, 0xe1, 0x62, 0x2b, 0x08,
                                     0xaf, 0xba, 0xd8, 0x4d, 0xbf, 0xc2, 0xa5, 0x5d};
constexpr std::uint8_t kSdIv[16] = {0x21, 0x67, 0x12, 0xe6, 0xaa, 0x1f, 0x68, 0x9f,
                                    0x95, 0xc5, 0xa2, 0x23, 0x24, 0xdc, 0x6a, 0x98};
constexpr std::uint8_t kMd5Blanker[16] = {0x0e, 0x65, 0x37, 0x81, 0x99, 0xbe, 0x45, 0x17,
                                          0xab, 0x06, 0xec, 0x22, 0x45, 0x1a, 0x57, 0x93};
constexpr std::size_t kSdHeaderSize = 0xF0C0;
constexpr std::size_t kSdBannerMost = 0xF0A0;
constexpr std::size_t kSdBkSize = 0x80;
constexpr std::size_t kSdFileHeaderSize = 0x80;
constexpr std::uint32_t kSdBkMagic = 0x426B0001;
constexpr std::uint32_t kSdFileMagic = 0x03ADF17E;
constexpr std::uint32_t kSdMostFiles = 64;

constexpr std::size_t kBannerSize = 0x72A0;
constexpr std::size_t kMiiDatabaseSize = 0x1F1E0;
constexpr std::size_t kMiiHiddenOffset = 0x1D00;

constexpr std::uint32_t kNativeMagic = 0x5653424E;  // "NBSV"
constexpr std::uint32_t kNativeVersion = 4;
constexpr std::size_t kNativeHeader = 16;
constexpr std::size_t kNativeRow = 20;

std::atomic<int> gCommitFault{0};

// ---- bytes ----

std::uint32_t be32(const std::byte* p) noexcept {
    return std::to_integer<std::uint32_t>(p[0]) << 24 | std::to_integer<std::uint32_t>(p[1]) << 16 |
           std::to_integer<std::uint32_t>(p[2]) << 8 | std::to_integer<std::uint32_t>(p[3]);
}
std::uint16_t be16(const std::byte* p) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(p[0]) << 8 | std::to_integer<unsigned>(p[1]));
}
std::uint32_t le32(const std::byte* p) noexcept {
    return std::to_integer<std::uint32_t>(p[3]) << 24 | std::to_integer<std::uint32_t>(p[2]) << 16 |
           std::to_integer<std::uint32_t>(p[1]) << 8 | std::to_integer<std::uint32_t>(p[0]);
}
std::uint16_t le16(const std::byte* p) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(p[1]) << 8 | std::to_integer<unsigned>(p[0]));
}
std::uint64_t le64(const std::byte* p) noexcept {
    return std::uint64_t{le32(p + 4)} << 32 | le32(p);
}
void put16(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xFFu));
}
void put32(Bytes& out, std::uint32_t value) {
    put16(out, value & 0xFFFFu);
    put16(out, value >> 16);
}
const std::uint8_t* u8(const std::byte* p) noexcept {
    return reinterpret_cast<const std::uint8_t*>(p);
}

std::uint32_t crc32_of(ByteSpan bytes) noexcept {
    uLong crc = crc32(0L, Z_NULL, 0);
    std::size_t at = 0;
    while (at < bytes.size()) {
        const std::size_t part = std::min<std::size_t>(bytes.size() - at, 1u << 30);
        crc = crc32(crc, u8(bytes.data() + at), static_cast<uInt>(part));
        at += part;
    }
    return static_cast<std::uint32_t>(crc);
}

std::string sha256_hex(ByteSpan bytes) {
    std::uint8_t digest[SHA256_DIGEST_LENGTH];
    SHA256(u8(bytes.data()), bytes.size(), digest);
    std::string hex;
    for (const std::uint8_t b : digest) {
        hex.push_back("0123456789abcdef"[b >> 4]);
        hex.push_back("0123456789abcdef"[b & 15u]);
    }
    return hex;
}

char lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
char upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}
std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), lower);
    return out;
}
bool same_text(std::string_view a, std::string_view b) noexcept {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return lower(x) == lower(y); });
}

std::string hex_of_code(std::string_view code) {
    std::string hex;
    for (const char c : code) {
        const unsigned b = static_cast<unsigned char>(c);
        hex.push_back("0123456789abcdef"[b >> 4]);
        hex.push_back("0123456789abcdef"[b & 15u]);
    }
    return hex;
}

// "524d4745" -> "RMGE"; empty when not 8 hex digits of a release.
std::string code_of_hex(std::string_view hex) {
    if (hex.size() != 8) {
        return {};
    }
    std::string code;
    for (std::size_t i = 0; i < 8; i += 2) {
        unsigned value = 0;
        for (std::size_t j = i; j < i + 2; ++j) {
            const char c = lower(hex[j]);
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0) {
                return {};
            }
            value = value * 16u + static_cast<unsigned>(digit);
        }
        code.push_back(static_cast<char>(value));
    }
    return code;
}

// A mod's file name in <code>/luma: letters, digits, '.', '_', '-', not starting with '.', at most 128 bytes.
bool is_mod_file_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128 || name.front() == '.') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
    });
}

std::vector<std::string_view> split_path(std::string_view path) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        parts.push_back(path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

// ---- checks of the files a save folder holds ----

struct GameDataCheck {
    Error error = Error::kUnsupported;
    GameDataFormat format = GameDataFormat::kNone;
};

GameDataCheck check_game_data(ByteSpan bytes) noexcept {
    NebulaSaveFormat::Header header;
    const NebulaSaveFormat::Error error = NebulaSaveFormat::validate(u8(bytes.data()), bytes.size(), header);
    switch (error) {
    case NebulaSaveFormat::Error::None:
        break;
    case NebulaSaveFormat::Error::Short:    // under 16 bytes: no version to know it by
    case NebulaSaveFormat::Error::Version:  // no GameData.bin version: another file
    case NebulaSaveFormat::Error::EncryptedExport:  // a data.bin that did not read as one
        return {Error::kUnsupported, GameDataFormat::kNone};
    default:
        return {Error::kDamaged, GameDataFormat::kNone};
    }
    switch (header.format) {
    case NebulaSaveFormat::Format::Wii2:
        return {Error::kNone, GameDataFormat::kWii};
    case NebulaSaveFormat::Format::Switch2:
        return {Error::kNone, GameDataFormat::kSwitch};
    case NebulaSaveFormat::Format::Native3:
        return {Error::kNone, GameDataFormat::kNative};
    default:
        return {Error::kUnsupported, GameDataFormat::kNone};
    }
}

bool is_native_magic(ByteSpan bytes) noexcept {
    return bytes.size() >= 4 && le32(bytes.data()) == kNativeMagic;
}

// As SaveDataHandler::readNative reads it: "NBSV", version 4, the CRC-32 of what follows the header, the rows' sizes
// inside the file.
Error check_native_save(ByteSpan bytes) noexcept {
    if (bytes.size() < kNativeHeader || !is_native_magic(bytes)) {
        return Error::kUnsupported;
    }
    const std::uint32_t version = le32(bytes.data() + 4);
    if (version > kNativeVersion) {
        return Error::kNewerVersion;
    }
    if (version != kNativeVersion) {
        return Error::kUnsupported;
    }
    const std::size_t count = le32(bytes.data() + 12);
    if (count > (bytes.size() - kNativeHeader) / kNativeRow ||
        le32(bytes.data() + 8) != crc32_of(bytes.subspan(kNativeHeader))) {
        return Error::kDamaged;
    }
    std::size_t place = kNativeHeader + count * kNativeRow;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t size = le32(bytes.data() + kNativeHeader + i * kNativeRow + 16);
        if (size > bytes.size() - place) {
            return Error::kDamaged;
        }
        place += size;
    }
    return Error::kNone;
}

// The save banner (WIBN), as 1.0.2 checked it.
bool is_banner(ByteSpan bytes) noexcept {
    if (bytes.size() != kBannerSize || be32(bytes.data()) != 0x5749424E || be32(bytes.data() + 4) != 0) {
        return false;
    }
    const std::uint16_t speed = be16(bytes.data() + 8);
    if (speed != 2 && speed != 3) {
        return false;
    }
    return std::all_of(bytes.begin() + 10, bytes.begin() + 32, [](std::byte b) { return b == std::byte{0}; });
}

// The Wii Mii database: its magics and its CRC-16 (0x1021) over the whole file, zero when intact.
bool is_mii_database(ByteSpan bytes) noexcept {
    if (bytes.size() != kMiiDatabaseSize || be32(bytes.data()) != 0x524E4F44 || be32(bytes.data() + kMiiHiddenOffset) != 0x524E4844) {
        return false;
    }
    unsigned crc = 0;
    for (const std::byte value : bytes) {
        unsigned data = std::to_integer<unsigned>(value);
        for (int bit = 0; bit < 8; ++bit, data <<= 1) {
            crc = (crc & 0x8000u) != 0 ? ((crc << 1) ^ 0x1021u) & 0xFFFFu : (crc << 1) & 0xFFFFu;
            if ((data & 0x80u) != 0) {
                crc ^= 1u;
            }
        }
    }
    return crc == 0;
}

bool is_miis_file(ByteSpan bytes) noexcept {
    return bytes.size() >= kMiisFileFormat.size() &&
           std::memcmp(bytes.data(), kMiisFileFormat.data(), kMiisFileFormat.size()) == 0;
}

bool is_datel(ByteSpan bytes) noexcept {
    return bytes.size() >= 14 && std::memcmp(bytes.data() + 4, "WIISAVE001", 10) == 0;
}

bool is_zip(ByteSpan bytes) noexcept {
    return bytes.size() >= 4 && be32(bytes.data()) == 0x504B0304u;
}

bool is_sd_save(ByteSpan bytes) noexcept {
    return bytes.size() >= kSdHeaderSize + kSdBkSize && be32(bytes.data() + kSdHeaderSize) == 0x70 &&
           be32(bytes.data() + kSdHeaderSize + 4) == kSdBkMagic;
}

// ---- a save file alone ----

struct SingleSave {
    Error error = Error::kUnsupported;
    Kind kind = Kind::kUnknown;
    GameDataFormat format = GameDataFormat::kNone;
    std::string code;  // from a Wii SD save's title
    Bytes native;      // a Nebula.sav
    Bytes game_data;   // or a GameData.bin
    Bytes banner;
    bool banner_left_out = false;
};

SingleSave read_sd_save(ByteSpan bytes) {
    SingleSave out;
    out.kind = Kind::kWiiSdSave;
    out.error = Error::kDamaged;
    std::vector<std::uint8_t> header(kSdHeaderSize);
    std::memcpy(header.data(), bytes.data(), kSdHeaderSize);
    detail::aes128_cbc_decrypt(kSdKey, kSdIv, header);
    std::uint8_t md5[16];
    std::memcpy(md5, header.data() + 0x0E, 16);
    std::memcpy(header.data() + 0x0E, kMd5Blanker, 16);
    std::uint8_t digest[MD5_DIGEST_LENGTH];
    MD5(header.data(), header.size(), digest);
    if (std::memcmp(digest, md5, 16) != 0) {
        return out;
    }
    const std::byte* const h = reinterpret_cast<const std::byte*>(header.data());
    const std::byte* const bk = bytes.data() + kSdHeaderSize;
    if (std::memcmp(h, bk + 0x60, 8) != 0) {
        return out;  // the header's title and the Bk header's differ
    }
    if (be32(h) != 0x00010000) {
        out.error = Error::kWrongGame;  // a channel or system title's data
        return out;
    }
    out.code.assign(reinterpret_cast<const char*>(header.data() + 4), 4);
    if (!is_release_code(out.code)) {
        out.error = Error::kWrongGame;
        return out;
    }
    const std::size_t banner_size = be32(h + 8);
    if (banner_size > kSdBannerMost) {
        return out;
    }
    const ByteSpan banner(h + 0x20, banner_size);
    if (is_banner(banner)) {
        out.banner.assign(banner.begin(), banner.end());
    } else {
        out.banner_left_out = true;
    }

    const std::uint32_t files = be32(bk + 12);
    if (files > kSdMostFiles) {
        return out;
    }
    std::size_t at = kSdHeaderSize + kSdBkSize;
    for (std::uint32_t i = 0; i < files; ++i) {
        if (bytes.size() - at < kSdFileHeaderSize) {
            return out;
        }
        const std::byte* const file = bytes.data() + at;
        at += kSdFileHeaderSize;
        if (be32(file) != kSdFileMagic) {
            return out;
        }
        const std::size_t size = be32(file + 4);
        const std::uint8_t type = std::to_integer<std::uint8_t>(file[10]);
        const char* const name_at = reinterpret_cast<const char*>(file + 11);
        const std::string_view name(name_at, strnlen(name_at, 0x45));
        if (name.empty() || name.size() == 0x45) {
            return out;
        }
        if (type == 2) {
            continue;  // a folder: no data
        }
        if (type != 1) {
            return out;
        }
        if (size > kMaxFileBytes) {
            out.error = Error::kTooLarge;
            return out;
        }
        const std::size_t padded = (size + 63u) & ~std::size_t{63};
        if (bytes.size() - at < padded) {
            return out;
        }
        if (name == kGameData) {
            std::vector<std::uint8_t> data(padded);
            std::memcpy(data.data(), bytes.data() + at, padded);
            std::uint8_t iv[16];
            std::memcpy(iv, file + 0x50, 16);
            detail::aes128_cbc_decrypt(kSdKey, iv, data);
            out.game_data.resize(size);
            std::memcpy(out.game_data.data(), data.data(), size);
        }
        at += padded;
    }
    if (out.game_data.empty()) {
        return out;
    }
    const GameDataCheck check = check_game_data(out.game_data);
    out.error = check.error == Error::kUnsupported ? Error::kDamaged : check.error;
    out.format = check.format;
    return out;
}

// A file's bytes as a save: a Nebula.sav, a GameData.bin, a Wii SD save; a Datel save recognised.
SingleSave read_single(ByteSpan bytes) {
    SingleSave out;
    if (is_datel(bytes)) {
        out.kind = Kind::kDatelSave;
        out.error = Error::kUnsupported;
        return out;
    }
    if (is_native_magic(bytes)) {
        out.kind = Kind::kNebulaSave;
        out.error = check_native_save(bytes);
        if (out.error == Error::kNone) {
            out.native.assign(bytes.begin(), bytes.end());
        }
        return out;
    }
    if (is_sd_save(bytes)) {
        return read_sd_save(bytes);
    }
    const GameDataCheck check = check_game_data(bytes);
    out.error = check.error;
    if (check.error != Error::kUnsupported) {
        out.kind = Kind::kGameData;
        out.format = check.format;
    }
    if (check.error == Error::kNone) {
        out.game_data.assign(bytes.begin(), bytes.end());
    }
    return out;
}

// ---- a save folder's files, by their place in it ----

// A save folder path in its canonical spelling, or empty for a path the import does not keep. `copy` takes the copy
// suffix (".1", ".2") of a Nebula.sav or GameData.bin.
struct Canonical {
    std::string path;
    std::string code;        // the release a per-release file belongs to
    std::string_view copy;   // "" for the file itself
    enum Role { kNone, kNative, kGameData, kBanner, kMod, kMiiDatabase, kMiisFile } role = kNone;
};

Canonical canonical(std::string_view path) {
    Canonical out;
    if (same_text(path, kMiisFilePath)) {
        out.path = kMiisFilePath;
        out.role = Canonical::kMiisFile;
        return out;
    }
    if (same_text(path, host::wii_mii_database_member())) {
        out.path = host::wii_mii_database_member();
        out.role = Canonical::kMiiDatabase;
        return out;
    }
    const std::vector<std::string_view> parts = split_path(path);
    const auto copy_of = [](std::string_view name, std::string_view base) -> std::optional<std::string_view> {
        for (const std::string_view copy : kCopies) {
            if (name.size() == base.size() + copy.size() && same_text(name.substr(0, base.size()), base) &&
                name.substr(base.size()) == copy) {
                return copy;
            }
        }
        return std::nullopt;
    };
    if (parts.size() >= 2 && parts[0].size() == 4) {
        std::string code;
        for (const char c : parts[0]) {
            code.push_back(upper(c));
        }
        if (!is_release_code(code)) {
            return out;
        }
        if (parts.size() == 2) {
            if (const auto copy = copy_of(parts[1], kNativeSave)) {
                out.code = code;
                out.copy = *copy;
                out.path = code + "/" + std::string(kNativeSave) + std::string(*copy);
                out.role = Canonical::kNative;
            }
            return out;
        }
        if (same_text(parts[1], kModFolder)) {
            if (parts.size() == 3 && is_mod_file_name(parts[2])) {
                out.path = code + "/" + std::string(kModFolder) + "/" + std::string(parts[2]);
            } else if (parts.size() == 4 && same_text(parts[2], kReplacedFolder) && is_mod_file_name(parts[3])) {
                out.path = code + "/" + std::string(kModFolder) + "/" + std::string(kReplacedFolder) + "/" + std::string(parts[3]);
            }
            if (!out.path.empty()) {
                out.code = code;
                out.role = Canonical::kMod;
            }
        }
        return out;
    }
    if (parts.size() == 5 && same_text(parts[0], "title") && parts[1] == "00010000" && same_text(parts[3], "data")) {
        const std::string code = code_of_hex(parts[2]);
        if (!is_release_code(code)) {
            return out;
        }
        const std::string folder = "title/00010000/" + hex_of_code(code) + "/data/";
        if (const auto copy = copy_of(parts[4], kGameData)) {
            out.code = code;
            out.copy = *copy;
            out.path = folder + std::string(kGameData) + std::string(*copy);
            out.role = Canonical::kGameData;
        } else if (same_text(parts[4], kBanner)) {
            out.code = code;
            out.path = folder + std::string(kBanner);
            out.role = Canonical::kBanner;
        }
    }
    return out;
}

// title/00010000/<hex>/data/... of a title that is no release of this game (another game's save in the folder).
bool is_other_title_path(std::string_view path) {
    const std::vector<std::string_view> parts = split_path(path);
    return parts.size() >= 4 && same_text(parts[0], "title") && parts[1] == "00010000" && parts[2].size() == 8 &&
           same_text(parts[3], "data") && !is_release_code(code_of_hex(parts[2]));
}

// A zip entry inside another title's save folder, at any depth.
bool in_other_title(std::string_view name) {
    for (std::size_t at = 0; at < name.size(); at = name.find('/', at) + 1) {
        if (is_other_title_path(name.substr(at))) {
            return true;
        }
        if (name.find('/', at) == std::string_view::npos) {
            break;
        }
    }
    return false;
}

// Files by canonical path.
struct Tree {
    std::map<std::string, Bytes> files;
    bool other_title = false;
};

std::string with_code(const Canonical& c, std::string_view code) {
    switch (c.role) {
    case Canonical::kNative:
        return std::string(code) + "/" + std::string(kNativeSave);
    case Canonical::kGameData:
        return game_data_path(code);
    case Canonical::kBanner:
        return "title/00010000/" + hex_of_code(code) + "/data/" + std::string(kBanner);
    case Canonical::kMod:
        return std::string(code) + c.path.substr(4);
    default:
        return c.path;
    }
}

// The plan for a save folder's files: each release's newest copy that passes the game's check, its banner and mods'
// files, the Mii files.
ImportPlan plan_tree(Tree tree, Kind kind, std::string_view target) {
    ImportPlan plan;
    plan.kind = kind;
    plan.target_code = target;
    std::map<std::string, std::pair<std::optional<Bytes>, std::optional<Bytes>>> saves;  // code: native, game data
    std::map<std::string, GameDataFormat> formats;
    std::set<std::string> damaged;
    for (const std::string_view code : kReleaseCodes) {
        for (const auto role : {Canonical::kNative, Canonical::kGameData}) {
            bool seen = false;
            for (const std::string_view copy : kCopies) {
                const std::string path = (role == Canonical::kNative ? native_save_path(code) : game_data_path(code)) + std::string(copy);
                const auto it = tree.files.find(path);
                if (it == tree.files.end()) {
                    continue;
                }
                seen = true;
                if (role == Canonical::kNative) {
                    if (check_native_save(it->second) == Error::kNone) {
                        saves[std::string(code)].first = it->second;
                        break;
                    }
                } else if (const GameDataCheck check = check_game_data(it->second); check.error == Error::kNone) {
                    saves[std::string(code)].second = it->second;
                    formats[std::string(code)] = check.format;
                    break;
                }
            }
            if (seen && !saves.contains(std::string(code))) {
                damaged.insert(std::string(code));
            }
        }
    }
    for (const std::string& code : damaged) {
        const auto it = saves.find(code);
        if (it == saves.end() || (!it->second.first && !it->second.second)) {
            plan.error = Error::kDamaged;
            return plan;
        }
    }
    if (saves.empty()) {
        plan.error = tree.other_title ? Error::kWrongGame : Error::kNoSaves;
        return plan;
    }
    // The releases kept, each to its own folder; a save of one other release alone is placed for the target.
    std::map<std::string, std::string> place;  // from: to
    if (saves.contains(std::string(target))) {
        for (const auto& [code, save] : saves) {
            place[code] = code;
        }
        plan.source_code = target;
    } else if (saves.size() == 1) {
        place[saves.begin()->first] = std::string(target);
        plan.source_code = saves.begin()->first;
    } else {
        plan.error = Error::kWrongRegion;
        return plan;
    }
    std::size_t total = 0;
    const auto add = [&plan, &total](std::string path, Bytes bytes) {
        total += bytes.size();
        plan.files.push_back({std::move(path), std::move(bytes)});
    };
    for (const auto& [from, to] : place) {
        plan.codes.push_back(to);
        auto& save = saves[from];
        if (save.first) {
            add(native_save_path(to), std::move(*save.first));
        }
        if (save.second) {
            add(game_data_path(to), std::move(*save.second));
            if (to == target && !save.first) {
                plan.format = formats[from];
            }
        }
    }
    for (auto& [path, bytes] : tree.files) {
        const Canonical c = canonical(path);
        if (c.role == Canonical::kBanner || c.role == Canonical::kMod) {
            const auto to = place.find(c.code);
            if (to == place.end()) {
                continue;
            }
            if (c.role == Canonical::kBanner && !is_banner(bytes)) {
                plan.banner_left_out = true;
                continue;
            }
            add(with_code(c, to->second), std::move(bytes));
        } else if (c.role == Canonical::kMiiDatabase || c.role == Canonical::kMiisFile) {
            if (c.role == Canonical::kMiiDatabase ? !is_mii_database(bytes) : !is_miis_file(bytes)) {
                plan.miis_left_out = true;
                continue;
            }
            plan.mii_library |= c.role == Canonical::kMiisFile;
            add(c.path, std::move(bytes));
        }
    }
    if (total > kMaxSourceBytes) {
        plan.files.clear();
        plan.error = Error::kTooLarge;
    }
    return plan;
}

ImportPlan plan_single(SingleSave save, Kind kind, std::string_view target) {
    ImportPlan plan;
    plan.kind = kind;
    plan.error = save.error;
    plan.target_code = target;
    plan.source_code = save.code;
    if (save.error != Error::kNone) {
        return plan;
    }
    plan.codes.push_back(std::string(target));
    plan.banner_left_out = save.banner_left_out;
    if (!save.native.empty()) {
        plan.files.push_back({native_save_path(target), std::move(save.native)});
        return plan;
    }
    plan.format = save.format;
    plan.files.push_back({game_data_path(target), std::move(save.game_data)});
    if (!save.banner.empty()) {
        plan.files.push_back({"title/00010000/" + hex_of_code(target) + "/data/" + std::string(kBanner), std::move(save.banner)});
    }
    return plan;
}

// ---- JSON (the archive's manifest) ----

struct Json {
    enum Type { kNull, kBool, kNumber, kString, kArray, kObject } type = kNull;
    bool boolean = false;
    std::int64_t number = 0;
    std::string text;
    std::vector<Json> items;
    // An object's members: names[i] names values[i]. Two vectors, not one of pairs: a vector of the type being
    // defined is allowed (C++17), a pair holding it is not (libstdc++ refuses it).
    std::vector<std::string> names;
    std::vector<Json> values;

    const Json* member(std::string_view name) const {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return &values[i];
            }
        }
        return nullptr;
    }
};

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : text_(text) {}

    std::optional<Json> read() {
        Json value;
        if (!value_of(value, 0)) {
            return std::nullopt;
        }
        space();
        return at_ == text_.size() ? std::optional<Json>(std::move(value)) : std::nullopt;
    }

private:
    static constexpr int kMostDepth = 32;
    static constexpr std::size_t kMostNodes = 200000;

    void space() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
            ++at_;
        }
    }
    bool take(char c) {
        space();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }
    bool word(std::string_view w) {
        if (text_.substr(at_, w.size()) != w) {
            return false;
        }
        at_ += w.size();
        return true;
    }
    static void put_utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    bool hex4(std::uint32_t& out) {
        if (text_.size() - at_ < 4) {
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = lower(text_[at_++]);
            const int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (digit < 0) {
                return false;
            }
            out = out * 16u + static_cast<std::uint32_t>(digit);
        }
        return true;
    }
    bool string_of(std::string& out) {
        if (!take('"')) {
            return false;
        }
        while (at_ < text_.size()) {
            const char c = text_[at_++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (at_ >= text_.size()) {
                return false;
            }
            const char e = text_[at_++];
            switch (e) {
            case '"': case '\\': case '/': out.push_back(e); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!hex4(cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp < 0xDC00) {
                    std::uint32_t low = 0;
                    if (!word("\\u") || !hex4(low) || low < 0xDC00 || low >= 0xE000) {
                        return false;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp < 0xE000) {
                    return false;
                }
                put_utf8(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }
    bool value_of(Json& out, int depth) {
        if (depth > kMostDepth || ++nodes_ > kMostNodes) {
            return false;
        }
        space();
        if (at_ >= text_.size()) {
            return false;
        }
        const char c = text_[at_];
        if (c == '{') {
            ++at_;
            out.type = Json::kObject;
            if (take('}')) {
                return true;
            }
            do {
                std::string key;
                Json value;
                if (!string_of(key) || !take(':') || !value_of(value, depth + 1)) {
                    return false;
                }
                out.names.push_back(std::move(key));
                out.values.push_back(std::move(value));
            } while (take(','));
            return take('}');
        }
        if (c == '[') {
            ++at_;
            out.type = Json::kArray;
            if (take(']')) {
                return true;
            }
            do {
                Json value;
                if (!value_of(value, depth + 1)) {
                    return false;
                }
                out.items.push_back(std::move(value));
            } while (take(','));
            return take(']');
        }
        if (c == '"') {
            out.type = Json::kString;
            return string_of(out.text);
        }
        if (word("true")) {
            out.type = Json::kBool;
            out.boolean = true;
            return true;
        }
        if (word("false")) {
            out.type = Json::kBool;
            return true;
        }
        if (word("null")) {
            return true;
        }
        // Integers only (the manifest's sizes and version).
        const bool negative = c == '-';
        at_ += negative ? 1u : 0u;
        std::size_t digits = 0;
        std::int64_t value = 0;
        while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            if (++digits > 15) {
                return false;
            }
            value = value * 10 + (text_[at_++] - '0');
        }
        if (digits == 0 || (at_ < text_.size() && (text_[at_] == '.' || text_[at_] == 'e' || text_[at_] == 'E'))) {
            return false;
        }
        out.type = Json::kNumber;
        out.number = negative ? -value : value;
        return true;
    }

    std::string_view text_;
    std::size_t at_ = 0;
    std::size_t nodes_ = 0;
};

void json_string(std::string& out, std::string_view text) {
    out.push_back('"');
    for (const char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (u < 0x20) {
            out += "\\u00";
            out.push_back("0123456789abcdef"[u >> 4]);
            out.push_back("0123456789abcdef"[u & 15u]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
}

// ---- sources ----

// An exported archive's files, checked against its manifest. `read`: an entry's bytes by name (any case), or null.
template <typename Read>
Error read_export(const Read& read, Tree* tree) {
    const Bytes* const manifest_bytes = read(kManifestName);
    if (manifest_bytes == nullptr) {
        return Error::kDamaged;
    }
    if (manifest_bytes->size() > kMaxFileBytes) {
        return Error::kTooLarge;
    }
    std::string_view text(reinterpret_cast<const char*>(manifest_bytes->data()), manifest_bytes->size());
    if (text.starts_with("\xEF\xBB\xBF")) {
        text.remove_prefix(3);  // a byte-order mark
    }
    const std::optional<Json> manifest = JsonReader(text).read();
    if (!manifest || manifest->type != Json::kObject) {
        return Error::kDamaged;
    }
    const Json* const format = manifest->member("format");
    if (format == nullptr || format->type != Json::kString || format->text != kFormatName) {
        return Error::kUnsupported;
    }
    const Json* const version = manifest->member("version");
    if (version == nullptr || version->type != Json::kNumber) {
        return Error::kDamaged;
    }
    if (version->number > kFormatVersion) {
        return Error::kNewerVersion;
    }
    const Json* const items = manifest->member("items");
    if (version->number != kFormatVersion || items == nullptr || items->type != Json::kArray) {
        return Error::kDamaged;
    }
    if (items->items.size() > kMaxEntries) {
        return Error::kTooLarge;
    }
    std::set<std::string> listed;
    for (const Json& item : items->items) {
        const Json* const type = item.member("type");
        const Json* const path = item.member("path");
        if (item.type != Json::kObject || type == nullptr || type->type != Json::kString || path == nullptr ||
            path->type != Json::kString || (type->text != "file" && type->text != "folder")) {
            return Error::kDamaged;
        }
        if (type->text == "folder") {
            continue;  // 1.0.2's folders with their streams: the game no longer keeps them
        }
        if (!listed.insert(lowered(path->text)).second) {
            return Error::kDamaged;
        }
        if (is_other_title_path(path->text)) {
            tree->other_title = true;
        }
        const Canonical c = canonical(path->text);
        if (c.role == Canonical::kNone) {
            continue;  // a file this version does not keep (1.0.2's SYSCONF, setting.txt, ...)
        }
        const Json* const size = item.member("size");
        const Json* const digest = item.member("sha256");
        const Bytes* const bytes = read("saves/" + path->text);
        if (size == nullptr || size->type != Json::kNumber || digest == nullptr || digest->type != Json::kString ||
            bytes == nullptr) {
            return Error::kDamaged;
        }
        if (size->number < 0 || static_cast<std::uint64_t>(size->number) != bytes->size() ||
            !same_text(digest->text, sha256_hex(*bytes))) {
            return Error::kDamaged;
        }
        tree->files[c.path] = *bytes;
    }
    return Error::kNone;
}

// A plain zip of a save folder (at its top or below a folder of its own), or one holding a save file.
ImportPlan plan_zip(ByteSpan bytes, std::string_view target) {
    ImportPlan plan;
    plan.kind = Kind::kZip;
    plan.target_code = target;
    std::vector<detail::ZipEntry> entries;
    plan.error = detail::read_zip(bytes, &entries);
    if (plan.error != Error::kNone) {
        return plan;
    }
    std::map<std::string, const Bytes*> by_name;
    for (const detail::ZipEntry& entry : entries) {
        by_name[lowered(entry.name)] = &entry.bytes;
    }
    const auto read = [&by_name](std::string_view name) -> const Bytes* {
        const auto it = by_name.find(lowered(name));
        return it == by_name.end() ? nullptr : it->second;
    };

    Tree tree;
    if (read(kManifestName) != nullptr) {
        plan.error = read_export(read, &tree);
        if (plan.error != Error::kNone) {
            plan.kind = Kind::kNebulaArchive;
            return plan;
        }
        plan = plan_tree(std::move(tree), Kind::kNebulaArchive, target);
        return plan;
    }

    // The save folder's top: the shortest prefix before a release's Nebula.sav or GameData.bin.
    std::optional<std::string> prefix;
    for (const detail::ZipEntry& entry : entries) {
        const std::vector<std::string_view> parts = split_path(entry.name);
        for (std::size_t skip = 0; skip < parts.size(); ++skip) {
            std::string rest;
            for (std::size_t i = skip; i < parts.size(); ++i) {
                rest += (i == skip ? "" : "/") + std::string(parts[i]);
            }
            if (is_other_title_path(rest)) {
                tree.other_title = true;
            }
            const Canonical c = canonical(rest);
            if (c.role == Canonical::kNative || c.role == Canonical::kGameData) {
                const std::string head = entry.name.substr(0, entry.name.size() - rest.size());
                if (!prefix || head.size() < prefix->size()) {
                    prefix = head;
                }
                break;
            }
        }
    }
    if (prefix) {
        for (const detail::ZipEntry& entry : entries) {
            if (entry.name.size() > prefix->size() && same_text(std::string_view(entry.name).substr(0, prefix->size()), *prefix)) {
                const Canonical c = canonical(std::string_view(entry.name).substr(prefix->size()));
                if (c.role != Canonical::kNone) {
                    tree.files[c.path] = entry.bytes;
                }
            }
        }
        return plan_tree(std::move(tree), Kind::kZip, target);
    }

    // A save file inside: the one nearest the top (a banner.bin beside a GameData.bin comes with it).
    std::vector<const detail::ZipEntry*> order;
    for (const detail::ZipEntry& entry : entries) {
        order.push_back(&entry);
    }
    std::stable_sort(order.begin(), order.end(), [](const detail::ZipEntry* a, const detail::ZipEntry* b) {
        return std::count(a->name.begin(), a->name.end(), '/') < std::count(b->name.begin(), b->name.end(), '/');
    });
    std::optional<ImportPlan> refused;
    for (const detail::ZipEntry* entry : order) {
        if (in_other_title(entry->name)) {
            continue;  // another game's file, whatever it reads as
        }
        SingleSave save = read_single(entry->bytes);
        if (save.kind == Kind::kUnknown) {
            continue;
        }
        if (save.error != Error::kNone) {
            if (!refused) {
                refused = plan_single(std::move(save), Kind::kZip, target);
            }
            continue;
        }
        if (save.kind == Kind::kGameData) {
            const std::size_t slash = entry->name.rfind('/');
            const std::string folder = slash == std::string::npos ? std::string() : entry->name.substr(0, slash + 1);
            if (const Bytes* const banner = read(folder + std::string(kBanner))) {
                if (is_banner(*banner)) {
                    save.banner = *banner;
                } else {
                    save.banner_left_out = true;
                }
            }
        }
        return plan_single(std::move(save), Kind::kZip, target);
    }
    if (refused) {
        return *refused;
    }
    plan.error = tree.other_title ? Error::kWrongGame : Error::kNoSaves;
    return plan;
}

// A whole file, bounded. kNone with its bytes.
Error read_file(const fs::path& path, std::size_t most, Bytes* out) {
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error) {
        return Error::kIo;
    }
    if (size > most) {
        return Error::kTooLarge;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return Error::kIo;
    }
    out->resize(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(out->data()), static_cast<std::streamsize>(size));
    return in && static_cast<std::uintmax_t>(in.gcount()) == size ? Error::kNone : Error::kIo;
}

bool is_file(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(fs::symlink_status(path, error)) && !error;
}
bool is_folder(const fs::path& path) {
    std::error_code error;
    return fs::is_directory(path, error) && !error;
}

// Whether a save folder holds another title's save folder (title/00010000/<not a release>/data).
bool holds_other_title(const fs::path& root) {
    std::error_code error;
    const fs::path titles = root / "title" / "00010000";
    if (!is_folder(titles)) {
        return false;
    }
    for (fs::directory_iterator it(titles, error), end; !error && it != end; it.increment(error)) {
        const std::string name = path_text(it->path().filename());
        if (name.size() == 8 && !is_release_code(code_of_hex(name)) && is_folder(it->path() / "data")) {
            return true;
        }
    }
    return false;
}

// The files of a save folder on disk that the import keeps (save_root: a save folder, or one to export).
Error read_save_folder(const fs::path& root, Tree* tree) {
    std::size_t total = 0;
    const auto take = [&](const std::string& path) -> Error {
        const fs::path file = root / text_path(path);
        if (!is_file(file)) {
            return Error::kNone;
        }
        Bytes bytes;
        const Error error = read_file(file, kMaxFileBytes, &bytes);
        if (error != Error::kNone) {
            return error;
        }
        total += bytes.size();
        if (total > kMaxSourceBytes) {
            return Error::kTooLarge;
        }
        tree->files[path] = std::move(bytes);
        return Error::kNone;
    };
    for (const std::string_view code : kReleaseCodes) {
        for (const std::string_view copy : kCopies) {
            for (const std::string& path : {native_save_path(code) + std::string(copy), game_data_path(code) + std::string(copy)}) {
                if (const Error error = take(path); error != Error::kNone) {
                    return error;
                }
            }
        }
        if (const Error error = take("title/00010000/" + hex_of_code(code) + "/data/" + std::string(kBanner)); error != Error::kNone) {
            return error;
        }
        std::size_t mods = 0;
        for (const std::string& folder : {std::string(code) + "/" + std::string(kModFolder),
                                         std::string(code) + "/" + std::string(kModFolder) + "/" + std::string(kReplacedFolder)}) {
            std::error_code error;
            if (!is_folder(root / text_path(folder))) {
                continue;
            }
            for (fs::directory_iterator it(root / text_path(folder), error), end; !error && it != end; it.increment(error)) {
                const std::string name = path_text(it->path().filename());
                if (!is_mod_file_name(name) || !is_file(it->path())) {
                    continue;
                }
                if (++mods > kMaxModFiles) {
                    return Error::kTooLarge;
                }
                if (const Error read = take(folder + "/" + name); read != Error::kNone) {
                    return read;
                }
            }
            if (error) {
                return Error::kIo;
            }
        }
    }
    for (const std::string_view path : {host::wii_mii_database_member(), kMiisFilePath}) {
        if (const Error error = take(std::string(path)); error != Error::kNone) {
            return error;
        }
    }
    tree->other_title = holds_other_title(root);
    return Error::kNone;
}

bool holds_save(const fs::path& root) {
    for (const std::string_view code : kReleaseCodes) {
        for (const std::string_view copy : kCopies) {
            if (is_file(root / text_path(native_save_path(code) + std::string(copy))) ||
                is_file(root / text_path(game_data_path(code) + std::string(copy)))) {
                return true;
            }
        }
    }
    return false;
}

ImportPlan plan_folder(const fs::path& folder, std::string_view target) {
    ImportPlan plan;
    plan.kind = Kind::kFolder;
    plan.target_code = target;
    Tree tree;
    if (is_file(folder / kManifestName)) {
        // An archive unpacked: its entries are files below the folder.
        std::map<std::string, Bytes> cache;
        std::size_t total = 0;
        Error failure = Error::kNone;
        const auto read = [&](std::string_view name) -> const Bytes* {
            const std::string key = lowered(name);
            if (const auto it = cache.find(key); it != cache.end()) {
                return &it->second;
            }
            if (!detail::is_safe_entry_name(name)) {
                failure = Error::kDamaged;
                return nullptr;
            }
            const fs::path file = folder / text_path(std::string(name));
            if (!is_file(file)) {
                return nullptr;
            }
            Bytes bytes;
            if (const Error error = read_file(file, kMaxFileBytes, &bytes); error != Error::kNone) {
                failure = error;
                return nullptr;
            }
            total += bytes.size();
            if (total > kMaxSourceBytes) {
                failure = Error::kTooLarge;
                return nullptr;
            }
            return &cache.emplace(key, std::move(bytes)).first->second;
        };
        plan.error = read_export(read, &tree);
        if (failure != Error::kNone) {
            plan.error = failure;
        }
        if (plan.error != Error::kNone) {
            plan.kind = Kind::kNebulaArchive;
            return plan;
        }
        return plan_tree(std::move(tree), Kind::kNebulaArchive, target);
    }
    // A save folder, also one level down as earlier builds and Dolphin name it.
    const std::array<fs::path, 5> candidates = {folder, folder / "saves", folder / "save-data", folder / "nand", folder / "Wii"};
    for (const fs::path& candidate : candidates) {
        if (holds_save(candidate)) {
            plan.error = read_save_folder(candidate, &tree);
            if (plan.error != Error::kNone) {
                return plan;
            }
            return plan_tree(std::move(tree), Kind::kFolder, target);
        }
    }
    // A folder holding a save file: GameData.bin or Nebula.sav by name first, then any file that reads as a save.
    std::vector<fs::path> files;
    std::error_code error;
    for (fs::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (is_file(it->path())) {
            files.push_back(it->path());
            if (files.size() > kMaxEntries) {
                plan.error = Error::kTooLarge;
                return plan;
            }
        }
    }
    if (error) {
        plan.error = Error::kIo;
        return plan;
    }
    std::stable_sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        const auto rank = [](const fs::path& p) {
            const std::string name = path_text(p.filename());
            return same_text(name, kNativeSave) || same_text(name, kGameData) ? 0 : 1;
        };
        return rank(a) < rank(b);
    });
    std::optional<ImportPlan> refused;
    for (const fs::path& file : files) {
        Bytes bytes;
        const Error read = read_file(file, kMaxSourceBytes, &bytes);
        if (read != Error::kNone) {
            if (read == Error::kIo) {
                plan.error = read;
                return plan;
            }
            continue;
        }
        SingleSave save = read_single(bytes);
        if (save.kind == Kind::kUnknown) {
            continue;
        }
        if (save.error != Error::kNone) {
            if (!refused) {
                refused = plan_single(std::move(save), Kind::kFolder, target);
            }
            continue;
        }
        if (save.kind == Kind::kGameData) {
            Bytes banner;
            if (is_file(folder / kBanner) && read_file(folder / kBanner, kMaxFileBytes, &banner) == Error::kNone) {
                if (is_banner(banner)) {
                    save.banner = std::move(banner);
                } else {
                    save.banner_left_out = true;
                }
            }
        }
        return plan_single(std::move(save), Kind::kFolder, target);
    }
    if (refused) {
        return *refused;
    }
    plan.error = Error::kNoSaves;
    if (std::any_of(candidates.begin(), candidates.end(), holds_other_title)) {
        plan.error = Error::kWrongGame;  // another game's save folder only
    }
    return plan;
}

// ---- moves ----

std::string local_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
}

fs::path clean_root(const fs::path& root) {
    fs::path clean = root.lexically_normal();
    if (clean.filename().empty()) {
        clean = clean.parent_path();
    }
    return clean;
}

// A sibling of `root` named <root name><suffix>, not taken.
fs::path free_sibling(const fs::path& root, const std::string& suffix) {
    const fs::path base = root.parent_path() / text_path(path_text(root.filename()) + suffix);
    fs::path candidate = base;
    std::error_code error;
    for (int number = 2; fs::exists(fs::symlink_status(candidate, error)); ++number) {
        candidate = base;
        candidate += "-" + std::to_string(number);
    }
    return candidate;
}

bool move_file(const fs::path& from, const fs::path& to) {
    if (const int fault = gCommitFault.load(); fault > 0 && gCommitFault.fetch_sub(1) == 1) {
        return false;
    }
    std::error_code error;
    fs::create_directories(to.parent_path(), error);
    if (error) {
        return false;
    }
    fs::rename(from, to, error);
    return !error;
}

bool same_file_bytes(const fs::path& file, ByteSpan expected) {
    Bytes bytes;
    return read_file(file, kMaxFileBytes, &bytes) == Error::kNone && bytes.size() == expected.size() &&
           std::equal(bytes.begin(), bytes.end(), expected.begin());
}

// The paths an import moves aside: each release's whole save and the copies the game reads, and each file it writes.
std::vector<std::string> replaced_paths(const ImportPlan& plan, const fs::path& root) {
    std::set<std::string> paths;
    for (const std::string& code : plan.codes) {
        for (const std::string_view copy : kCopies) {
            paths.insert(native_save_path(code) + std::string(copy));
            paths.insert(game_data_path(code) + std::string(copy));
        }
    }
    for (const SaveFile& file : plan.files) {
        paths.insert(file.path);
    }
    std::vector<std::string> present;
    for (const std::string& path : paths) {
        if (is_file(root / text_path(path))) {
            present.push_back(path);
        }
    }
    return present;
}

}  // namespace

// ---- public ----

bool is_release_code(std::string_view code) noexcept {
    return std::find(kReleaseCodes.begin(), kReleaseCodes.end(), code) != kReleaseCodes.end();
}

std::string native_save_path(std::string_view code) {
    return std::string(code) + "/" + std::string(kNativeSave);
}

std::string game_data_path(std::string_view code) {
    return "title/00010000/" + hex_of_code(code) + "/data/" + std::string(kGameData);
}

ImportPlan plan_import(std::span<const std::byte> bytes, std::string_view target_code) noexcept {
    try {
        if (!is_release_code(target_code)) {
            ImportPlan plan;
            plan.error = Error::kWrongGame;
            return plan;
        }
        if (bytes.size() > kMaxSourceBytes) {
            ImportPlan plan;
            plan.error = Error::kTooLarge;
            plan.target_code = target_code;
            return plan;
        }
        if (is_zip(bytes)) {
            return plan_zip(bytes, target_code);
        }
        SingleSave save = read_single(bytes);
        const Kind kind = save.kind;
        return plan_single(std::move(save), kind, target_code);
    } catch (...) {
        ImportPlan plan;
        plan.error = Error::kIo;  // out of memory
        return plan;
    }
}

ImportPlan plan_import(const std::filesystem::path& source, std::string_view target_code) noexcept {
    try {
        if (!is_release_code(target_code)) {
            ImportPlan plan;
            plan.error = Error::kWrongGame;
            return plan;
        }
        if (is_folder(source)) {
            return plan_folder(source, target_code);
        }
        Bytes bytes;
        if (const Error error = read_file(source, kMaxSourceBytes, &bytes); error != Error::kNone) {
            ImportPlan plan;
            plan.error = error;
            plan.target_code = target_code;
            return plan;
        }
        return plan_import(ByteSpan(bytes), target_code);
    } catch (...) {
        ImportPlan plan;
        plan.error = Error::kIo;
        return plan;
    }
}

bool replaces_saves(const ImportPlan& plan, const std::filesystem::path& save_root) noexcept {
    try {
        const fs::path root = clean_root(save_root);
        for (const std::string& code : plan.codes) {
            for (const std::string_view copy : kCopies) {
                if (is_file(root / text_path(native_save_path(code) + std::string(copy))) ||
                    is_file(root / text_path(game_data_path(code) + std::string(copy)))) {
                    return true;
                }
            }
        }
        return false;
    } catch (...) {
        return true;
    }
}

bool is_save_source(const std::filesystem::path& source, const ImportPlan& plan) noexcept {
    try {
        const std::string name = path_text(source.filename());
        if (same_text(name, kGameData) || same_text(name, kNativeSave)) {
            return true;
        }
    } catch (...) {
        // A name that does not convert: the contents decide.
    }
    switch (plan.kind) {
    case Kind::kNebulaArchive:
    case Kind::kNebulaSave:
    case Kind::kWiiSdSave:
    case Kind::kDatelSave:
        return true;
    case Kind::kGameData:
        // Its form is known by one version word only: a file that fails the game's check may be anything.
        return plan.error == Error::kNone;
    case Kind::kZip:
    case Kind::kFolder:
        return plan.error == Error::kNone || plan.error == Error::kNewerVersion || plan.error == Error::kWrongGame ||
               plan.error == Error::kWrongRegion;
    case Kind::kUnknown:
        break;
    }
    return false;
}

ImportResult commit_import(const ImportPlan& plan, const std::filesystem::path& save_root) noexcept {
    ImportResult result;
    if (plan.error != Error::kNone || plan.files.empty()) {
        result.error = plan.error != Error::kNone ? plan.error : Error::kNoSaves;
        return result;
    }
    fs::path staging;
    try {
        const fs::path root = clean_root(save_root);
        std::error_code error;
        fs::create_directories(root, error);
        if (error || !is_folder(root)) {
            result.error = Error::kIo;
            return result;
        }
        // Every file written beside the save root first, and read back.
        staging = free_sibling(root, ".import");
        for (const SaveFile& file : plan.files) {
            const fs::path to = staging / text_path(file.path);
            fs::create_directories(to.parent_path(), error);
            const std::string_view contents(reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size());
            if (error || !io::write_file_atomically(to, contents) || !same_file_bytes(to, file.bytes)) {
                fs::remove_all(staging, error);
                result.error = Error::kIo;
                return result;
            }
        }
        // The replaced files aside, then the new ones in; on a failure, each move undone in reverse.
        const std::vector<std::string> replaced = replaced_paths(plan, root);
        const fs::path backup = replaced.empty() ? fs::path() : free_sibling(root, "-backup-" + local_stamp());
        std::vector<std::pair<fs::path, fs::path>> moved;
        bool failed = false;
        for (const std::string& path : replaced) {
            const fs::path from = root / text_path(path);
            const fs::path to = backup / text_path(path);
            if (!move_file(from, to)) {
                failed = true;
                break;
            }
            moved.emplace_back(from, to);
        }
        for (std::size_t i = 0; !failed && i < plan.files.size(); ++i) {
            const fs::path from = staging / text_path(plan.files[i].path);
            const fs::path to = root / text_path(plan.files[i].path);
            if (!move_file(from, to)) {
                failed = true;
                break;
            }
            moved.emplace_back(from, to);
        }
        if (failed) {
            for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
                fs::rename(it->second, it->first, error);
            }
            fs::remove_all(staging, error);
            if (!backup.empty()) {
                fs::remove_all(backup, error);  // empty again: every file went back
            }
            result.error = Error::kIo;
            return result;
        }
        fs::remove_all(staging, error);
        result.backup = backup;
        result.written = plan.files.size();
        return result;
    } catch (...) {
        std::error_code error;
        if (!staging.empty()) {
            fs::remove_all(staging, error);
        }
        result.error = Error::kIo;
        return result;
    }
}

Error make_export(const std::filesystem::path& save_root, std::string_view nebula_version, std::vector<std::byte>* zip) noexcept {
    try {
        zip->clear();
        const fs::path root = clean_root(save_root);
        Tree tree;
        if (is_folder(root)) {
            if (const Error error = read_save_folder(root, &tree); error != Error::kNone) {
                return error;
            }
        }
        // Each release's newest copy that passes the game's check, by its own name; a release whose copies all fail
        // is damaged. The rest as read, where it passes its check.
        std::vector<detail::ZipEntry> files;
        bool any = false;
        for (const std::string_view code : kReleaseCodes) {
            bool present = false;
            bool valid = false;
            for (const bool native : {true, false}) {
                const std::string base = native ? native_save_path(code) : game_data_path(code);
                for (const std::string_view copy : kCopies) {
                    const auto it = tree.files.find(base + std::string(copy));
                    if (it == tree.files.end()) {
                        continue;
                    }
                    present = true;
                    if (native ? check_native_save(it->second) == Error::kNone : check_game_data(it->second).error == Error::kNone) {
                        files.push_back({base, it->second});
                        valid = true;
                        break;
                    }
                }
            }
            if (present && !valid) {
                return Error::kDamaged;
            }
            any |= valid;
        }
        if (!any) {
            return Error::kNoSaves;
        }
        for (const auto& [path, bytes] : tree.files) {
            const Canonical c = canonical(path);
            const bool keep = (c.role == Canonical::kBanner && is_banner(bytes)) || c.role == Canonical::kMod ||
                              (c.role == Canonical::kMiiDatabase && is_mii_database(bytes)) ||
                              (c.role == Canonical::kMiisFile && is_miis_file(bytes));
            if (keep) {
                files.push_back({path, bytes});
            }
        }

        std::string manifest = "{\"format\":";
        json_string(manifest, kFormatName);
        manifest += ",\"version\":" + std::to_string(kFormatVersion) + ",\"nebula\":";
        json_string(manifest, nebula_version);
        manifest += ",\"items\":[";
        for (std::size_t i = 0; i < files.size(); ++i) {
            manifest += i == 0 ? "{\"path\":" : ",{\"path\":";
            json_string(manifest, files[i].name);
            manifest += ",\"type\":\"file\",\"size\":" + std::to_string(files[i].bytes.size()) + ",\"sha256\":";
            json_string(manifest, sha256_hex(files[i].bytes));
            manifest += ",\"streams\":[]}";
        }
        manifest += "]}\n";

        std::vector<detail::ZipEntry> entries;
        entries.push_back({std::string(kManifestName), Bytes(reinterpret_cast<const std::byte*>(manifest.data()),
                                                             reinterpret_cast<const std::byte*>(manifest.data()) + manifest.size())});
        for (detail::ZipEntry& file : files) {
            entries.push_back({"saves/" + file.name, std::move(file.bytes)});
        }
        if (!detail::write_zip(entries, zip)) {
            zip->clear();
            return Error::kTooLarge;
        }
        // Read back: the same entries.
        std::vector<detail::ZipEntry> back;
        if (detail::read_zip(*zip, &back) != Error::kNone || back.size() != entries.size() ||
            !std::equal(back.begin(), back.end(), entries.begin(), [](const detail::ZipEntry& a, const detail::ZipEntry& b) {
                return a.name == b.name && a.bytes == b.bytes;
            })) {
            zip->clear();
            return Error::kIo;
        }
        return Error::kNone;
    } catch (...) {
        zip->clear();
        return Error::kIo;
    }
}

ExportResult export_saves(const std::filesystem::path& save_root, const std::filesystem::path& folder,
                          std::string_view nebula_version) noexcept {
    ExportResult result;
    try {
        Bytes zip;
        result.error = make_export(save_root, nebula_version, &zip);
        if (result.error != Error::kNone) {
            return result;
        }
        std::error_code error;
        fs::create_directories(folder, error);
        if (!is_folder(folder)) {
            result.error = Error::kIo;
            return result;
        }
        const std::string stamp = "Nebula-Saves-" + local_stamp();
        fs::path file = folder / text_path(stamp + ".zip");
        for (int number = 2; fs::exists(fs::symlink_status(file, error)); ++number) {
            file = folder / text_path(stamp + "-" + std::to_string(number) + ".zip");
        }
        fs::path partial = file;
        partial += ".partial";
        const std::string_view contents(reinterpret_cast<const char*>(zip.data()), zip.size());
        if (!io::write_file_atomically(partial, contents) || !same_file_bytes(partial, zip)) {
            fs::remove(partial, error);
            result.error = Error::kIo;
            return result;
        }
        fs::rename(partial, file, error);
        if (error) {
            fs::remove(partial, error);
            result.error = Error::kIo;
            return result;
        }
        result.file = file;
        return result;
    } catch (...) {
        result.error = Error::kIo;
        return result;
    }
}

namespace {
std::mutex gGameSavesMutex;
fs::path gGameSaveRoot;
std::string gGameCode;
}  // namespace

void set_game_saves(const std::filesystem::path& save_root, std::string_view code) {
    const std::lock_guard<std::mutex> lock(gGameSavesMutex);
    gGameSaveRoot = save_root;
    gGameCode = std::string(code);
}

std::filesystem::path game_save_root() {
    const std::lock_guard<std::mutex> lock(gGameSavesMutex);
    return gGameSaveRoot;
}

std::string game_code() {
    const std::lock_guard<std::mutex> lock(gGameSavesMutex);
    return gGameCode;
}

// ---- the per-file import ----

namespace {

std::atomic<NebulaSaveImportRead> gGameReader{nullptr};

constexpr std::array<std::string_view, 3> kFileKinds = {"mario", "luigi", "config"};  // SaveDataHandler.cpp cKinds

// A Nebula.sav's rows by name (the 16-byte name without its padding), each its data's span; a later row of a name
// wins, as the game reads them. False: not a Nebula.sav the game reads.
bool read_native_rows(ByteSpan save, std::map<std::string, ByteSpan>* rows) {
    if (check_native_save(save) != Error::kNone) {
        return false;
    }
    const std::size_t count = le32(save.data() + 12);
    std::size_t place = kNativeHeader + count * kNativeRow;
    for (std::size_t i = 0; i < count; ++i) {
        const std::byte* const row = save.data() + kNativeHeader + i * kNativeRow;
        std::size_t length = 0;
        while (length < 16 && row[length] != std::byte{0}) {
            ++length;
        }
        const std::size_t size = le32(row + 16);
        (*rows)[std::string(reinterpret_cast<const char*>(row), length)] = save.subspan(place, size);
        place += size;
    }
    return true;
}

std::string file_name(std::string_view kind, std::size_t user) {
    return std::string(kind) + std::to_string(user);
}

}  // namespace

const SaveFile* release_save(const ImportPlan& plan) noexcept {
    if (plan.error != Error::kNone) {
        return nullptr;
    }
    try {
        const std::string native = native_save_path(plan.target_code);
        const std::string game_data = game_data_path(plan.target_code);
        for (const SaveFile& file : plan.files) {
            if (file.path == native || file.path == game_data) {
                return &file;
            }
        }
    } catch (...) {
    }
    return nullptr;
}

bool places_more_than_save(const ImportPlan& plan) noexcept {
    const SaveFile* const save = release_save(plan);
    return std::any_of(plan.files.begin(), plan.files.end(), [save](const SaveFile& file) {
        return &file != save && canonical(file.path).role != Canonical::kBanner;
    });
}

Error merge_saves(std::span<const std::byte> current, std::span<const std::byte> imported,
                  std::span<const std::uint8_t, kUserFiles> targets, std::vector<std::byte>* merged) noexcept {
    try {
        merged->clear();
        std::array<bool, kUserFiles + 1> taken{};
        bool any = false;
        for (const std::uint8_t target : targets) {
            if (target > kUserFiles || (target != 0 && taken[target])) {
                return Error::kUnsupported;
            }
            taken[target] = target != 0;
            any |= target != 0;
        }
        if (!any) {
            return Error::kNoSaves;
        }
        std::map<std::string, ByteSpan> rows;
        std::map<std::string, ByteSpan> from;
        if (!read_native_rows(current, &rows) || !read_native_rows(imported, &from)) {
            return Error::kDamaged;
        }
        for (std::size_t i = 0; i < kUserFiles; ++i) {
            if (targets[i] == 0) {
                continue;
            }
            for (const std::string_view kind : kFileKinds) {
                const auto it = from.find(file_name(kind, i + 1));
                rows[file_name(kind, targets[i])] = it != from.end() ? it->second : ByteSpan();
            }
        }
        // The game's own order and rows: each user's game data, Mario's and Luigi's, then its config.
        constexpr std::size_t kRows = kUserFiles * kFileKinds.size();
        Bytes out;
        put32(out, kNativeMagic);
        put32(out, kNativeVersion);
        put32(out, 0);  // the CRC-32, once the rest is made
        put32(out, static_cast<std::uint32_t>(kRows));
        std::vector<ByteSpan> data;
        for (std::size_t user = 1; user <= kUserFiles; ++user) {
            for (const std::string_view kind : kFileKinds) {
                const std::string name = file_name(kind, user);
                const auto it = rows.find(name);
                const ByteSpan bytes = it != rows.end() ? it->second : ByteSpan();
                std::array<std::byte, 16> padded{};
                std::memcpy(padded.data(), name.data(), name.size());
                out.insert(out.end(), padded.begin(), padded.end());
                put32(out, static_cast<std::uint32_t>(bytes.size()));
                data.push_back(bytes);
            }
        }
        for (const ByteSpan bytes : data) {
            out.insert(out.end(), bytes.begin(), bytes.end());
        }
        if (out.size() > kMaxFileBytes) {
            return Error::kTooLarge;
        }
        const std::uint32_t crc = crc32_of(ByteSpan(out).subspan(kNativeHeader));
        for (std::size_t i = 0; i < 4; ++i) {
            out[8 + i] = static_cast<std::byte>((crc >> (8 * i)) & 0xFFu);
        }
        *merged = std::move(out);
        return Error::kNone;
    } catch (...) {
        merged->clear();
        return Error::kIo;  // out of memory
    }
}

ImportPlan plan_native_save(std::vector<std::byte> save, std::string_view code) noexcept {
    ImportPlan plan;
    try {
        plan.kind = Kind::kNebulaSave;
        plan.target_code = code;
        if (!is_release_code(code)) {
            plan.error = Error::kWrongGame;
            return plan;
        }
        if (const Error error = check_native_save(save); error != Error::kNone) {
            plan.error = error == Error::kUnsupported ? Error::kDamaged : error;
            return plan;
        }
        plan.codes.push_back(std::string(code));
        plan.files.push_back({native_save_path(code), std::move(save)});
    } catch (...) {
        plan.error = Error::kIo;
    }
    return plan;
}

void set_game_reader(NebulaSaveImportRead read) noexcept {
    gGameReader.store(read, std::memory_order_release);
}

NebulaSaveImportRead game_reader() noexcept {
    return gGameReader.load(std::memory_order_acquire);
}

namespace detail {

void aes128_cbc_decrypt(const std::uint8_t key[16], const std::uint8_t iv[16], std::span<std::uint8_t> bytes) noexcept {
    AES_KEY schedule;
    AES_set_decrypt_key(key, 128, &schedule);
    std::uint8_t chain[16];
    std::memcpy(chain, iv, 16);
    const std::size_t whole = bytes.size() & ~std::size_t{15};
    if (whole != 0) {
        AES_cbc_encrypt(bytes.data(), bytes.data(), whole, &schedule, chain, AES_DECRYPT);
    }
}

bool is_safe_entry_name(std::string_view name) noexcept {
    if (name.empty() || name.front() == '/' || name.find(':') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos || name.find('\0') != std::string_view::npos) {
        return false;
    }
    if (name.back() == '/') {
        name.remove_suffix(1);  // a folder entry
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t end = name.find('/', start);
        const std::string_view part = name.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        if (end == std::string_view::npos) {
            return true;
        }
        start = end + 1;
    }
}

// The ZIP64 extra field (APPNOTE 4.5.3): the full values of the central record's fields that hold 0xFFFFFFFF, in the
// order size, packed size, local header offset. False when the field is missing or too short.
static bool read_zip64_extra(std::span<const std::byte> extra, std::uint64_t* size, std::uint64_t* packed,
                             std::uint64_t* local) noexcept {
    while (extra.size() >= 4) {
        const std::uint16_t id = le16(extra.data());
        const std::size_t length = le16(extra.data() + 2);
        if (length > extra.size() - 4) {
            return false;
        }
        if (id == 0x0001u) {
            std::span<const std::byte> field = extra.subspan(4, length);
            for (std::uint64_t* value : {size, packed, local}) {
                if (*value != 0xFFFFFFFFu) {
                    continue;
                }
                if (field.size() < 8) {
                    return false;
                }
                *value = le64(field.data());
                field = field.subspan(8);
            }
            return true;
        }
        extra = extra.subspan(4 + length);
    }
    return false;
}

Error read_zip(std::span<const std::byte> zip, std::vector<ZipEntry>* entries) noexcept {
    try {
        entries->clear();
        constexpr std::size_t kEnd = 22;
        if (zip.size() < kEnd) {
            return Error::kDamaged;
        }
        // The end record: within the last 64 KiB and its own size (its comment).
        std::size_t end = std::string::npos;
        const std::size_t lowest = zip.size() > kEnd + 0xFFFF ? zip.size() - kEnd - 0xFFFF : 0;
        for (std::size_t at = zip.size() - kEnd + 1; at-- > lowest;) {
            if (le32(zip.data() + at) == 0x06054B50u && at + kEnd + le16(zip.data() + at + 20) == zip.size()) {
                end = at;
                break;
            }
        }
        if (end == std::string::npos) {
            return Error::kDamaged;
        }
        const std::byte* const e = zip.data() + end;
        if (le16(e + 4) != 0 || le16(e + 6) != 0 || le16(e + 8) != le16(e + 10)) {
            return Error::kUnsupported;  // a zip in parts
        }
        std::uint64_t count = le16(e + 10);
        std::uint64_t directory_size = le32(e + 12);
        std::uint64_t directory_at = le32(e + 16);
        // ZIP64 (APPNOTE 4.3.14, 4.3.15): a locator just before the end record points at a larger end record, whose
        // count, size and offset replace the ones that did not fit.
        std::size_t limit = end;  // the central directory ends at or before this
        if (end >= 20 && le32(e - 20) == 0x07064B50u) {
            const std::byte* const l = e - 20;
            if (le32(l + 4) != 0 || le32(l + 16) != 1) {
                return Error::kUnsupported;  // a zip in parts
            }
            const std::uint64_t record = le64(l + 8);
            if (record > end - 20 || end - 20 - record < 56 || le32(zip.data() + record) != 0x06064B50u) {
                return Error::kDamaged;
            }
            const std::byte* const r = zip.data() + record;
            if (le64(r + 4) < 44 || le64(r + 4) > end - 20 - record - 12) {
                return Error::kDamaged;
            }
            if (le32(r + 16) != 0 || le32(r + 20) != 0 || le64(r + 24) != le64(r + 32)) {
                return Error::kUnsupported;  // a zip in parts
            }
            count = le64(r + 32);
            directory_size = le64(r + 40);
            directory_at = le64(r + 48);
            limit = static_cast<std::size_t>(record);
        }
        if (count > kMaxEntries) {
            return Error::kTooLarge;
        }
        if (directory_at > limit || directory_size > limit - directory_at) {
            return Error::kDamaged;
        }
        const std::size_t directory = static_cast<std::size_t>(directory_at);
        const std::size_t directory_end = directory + static_cast<std::size_t>(directory_size);
        std::set<std::string> names;
        std::uint64_t total = 0;
        std::size_t at = directory;
        for (std::uint64_t i = 0; i < count; ++i) {
            if (directory_end - at < 46 || le32(zip.data() + at) != 0x02014B50u) {
                return Error::kDamaged;
            }
            const std::byte* const c = zip.data() + at;
            const std::uint16_t flags = le16(c + 8);
            const std::uint16_t method = le16(c + 10);
            const std::uint32_t crc = le32(c + 16);
            std::uint64_t packed = le32(c + 20);
            std::uint64_t size = le32(c + 24);
            const std::size_t name_size = le16(c + 28);
            const std::size_t extra_size = le16(c + 30);
            const std::size_t comment_size = le16(c + 32);
            std::uint64_t local = le32(c + 42);
            if (directory_end - at - 46 < name_size + extra_size + comment_size) {
                return Error::kDamaged;
            }
            const std::span<const std::byte> extra(c + 46 + name_size, extra_size);
            std::string name(reinterpret_cast<const char*>(c + 46), name_size);
            at += 46 + name_size + extra_size + comment_size;
            std::replace(name.begin(), name.end(), '\\', '/');
            if (!is_safe_entry_name(name)) {
                return Error::kDamaged;
            }
            if (!names.insert(lowered(name)).second) {
                return Error::kDamaged;
            }
            if (name.back() == '/') {
                continue;
            }
            if ((flags & 1u) != 0) {
                return Error::kUnsupported;  // encrypted
            }
            if ((packed == 0xFFFFFFFFu || size == 0xFFFFFFFFu || local == 0xFFFFFFFFu) &&
                !read_zip64_extra(extra, &size, &packed, &local)) {
                return Error::kDamaged;
            }
            if (method != 0 && method != 8) {
                return Error::kUnsupported;
            }
            if (size > kMaxFileBytes) {
                return Error::kTooLarge;
            }
            total += size;
            if (total > kMaxSourceBytes) {
                return Error::kTooLarge;
            }
            if (local > directory || directory - local < 30 || le32(zip.data() + local) != 0x04034B50u) {
                return Error::kDamaged;
            }
            const std::size_t data = local + 30 + le16(zip.data() + local + 26) + le16(zip.data() + local + 28);
            if (data > directory || packed > directory - data) {
                return Error::kDamaged;
            }
            ZipEntry entry;
            entry.name = std::move(name);
            entry.bytes.resize(size);
            if (method == 0) {
                if (packed != size) {
                    return Error::kDamaged;
                }
                std::memcpy(entry.bytes.data(), zip.data() + data, size);
            } else {
                z_stream stream{};
                if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
                    return Error::kIo;
                }
                stream.next_in = const_cast<Bytef*>(u8(zip.data() + data));
                stream.avail_in = static_cast<uInt>(packed);
                // One byte of room past the size: an entry that inflates to more than it says is damaged.
                Bytes out(size + 1);
                stream.next_out = reinterpret_cast<Bytef*>(out.data());
                stream.avail_out = static_cast<uInt>(out.size());
                const int status = inflate(&stream, Z_FINISH);
                const std::size_t produced = stream.total_out;
                inflateEnd(&stream);
                if (status != Z_STREAM_END || produced != size) {
                    return Error::kDamaged;
                }
                std::memcpy(entry.bytes.data(), out.data(), size);
            }
            if (crc32_of(entry.bytes) != crc) {
                return Error::kDamaged;
            }
            entries->push_back(std::move(entry));
        }
        return Error::kNone;
    } catch (...) {
        entries->clear();
        return Error::kIo;
    }
}

bool write_zip(std::span<const ZipEntry> entries, std::vector<std::byte>* zip) noexcept {
    try {
        zip->clear();
        if (entries.size() >= 0xFFFF) {
            return false;
        }
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        const std::uint32_t time = static_cast<std::uint32_t>(local.tm_hour << 11 | local.tm_min << 5 | local.tm_sec / 2);
        const std::uint32_t date =
            static_cast<std::uint32_t>(std::max(local.tm_year - 80, 0) << 9 | (local.tm_mon + 1) << 5 | local.tm_mday);
        Bytes directory;
        for (const ZipEntry& entry : entries) {
            if (entry.name.size() > 0xFFFF || entry.bytes.size() >= 0xFFFFFFFFu || zip->size() >= 0xFFFFFFFFu) {
                return false;
            }
            const std::uint32_t crc = crc32_of(entry.bytes);
            const std::uint32_t size = static_cast<std::uint32_t>(entry.bytes.size());
            const std::uint32_t offset = static_cast<std::uint32_t>(zip->size());
            const std::uint32_t name_size = static_cast<std::uint32_t>(entry.name.size());
            const auto* const name = reinterpret_cast<const std::byte*>(entry.name.data());
            // Local header: version 2.0, UTF-8 names, stored.
            put32(*zip, 0x04034B50u);
            put16(*zip, 20);
            put16(*zip, 0x0800);
            put16(*zip, 0);
            put16(*zip, time);
            put16(*zip, date);
            put32(*zip, crc);
            put32(*zip, size);
            put32(*zip, size);
            put16(*zip, name_size);
            put16(*zip, 0);
            zip->insert(zip->end(), name, name + name_size);
            zip->insert(zip->end(), entry.bytes.begin(), entry.bytes.end());
            put32(directory, 0x02014B50u);
            put16(directory, 20);
            put16(directory, 20);
            put16(directory, 0x0800);
            put16(directory, 0);
            put16(directory, time);
            put16(directory, date);
            put32(directory, crc);
            put32(directory, size);
            put32(directory, size);
            put16(directory, name_size);
            put16(directory, 0);
            put16(directory, 0);
            put16(directory, 0);
            put16(directory, 0);
            put32(directory, 0);
            put32(directory, offset);
            directory.insert(directory.end(), name, name + name_size);
        }
        if (zip->size() + directory.size() >= 0xFFFFFFFFu) {
            return false;
        }
        const std::uint32_t directory_at = static_cast<std::uint32_t>(zip->size());
        zip->insert(zip->end(), directory.begin(), directory.end());
        put32(*zip, 0x06054B50u);
        put16(*zip, 0);
        put16(*zip, 0);
        put16(*zip, static_cast<std::uint32_t>(entries.size()));
        put16(*zip, static_cast<std::uint32_t>(entries.size()));
        put32(*zip, static_cast<std::uint32_t>(directory.size()));
        put32(*zip, directory_at);
        put16(*zip, 0);
        return true;
    } catch (...) {
        zip->clear();
        return false;
    }
}

void set_commit_fault(int step) noexcept {
    gCommitFault.store(step);
}

}  // namespace detail

}  // namespace galaxy::save_transfer
