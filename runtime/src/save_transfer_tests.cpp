// SPDX-License-Identifier: GPL-3.0-only
// Tests for galaxy/save_transfer.h. Every input is built here: GameData.bin in the Wii's, the Switch's and Nebula 1.0's
// layouts, Nebula.sav, the banner, the Mii database, a Wii SD card save under the SD key, zips (stored, and one deflated
// by zlib), folders; then the import's checks, its placing and backup, a failed commit put back, and an export read back.
#include "galaxy/save_transfer.h"
#include "galaxy/mii_formats.h"
#include "galaxy/test_temp.h"

#include <openssl/aes.h>
#include <openssl/md5.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace st = galaxy::save_transfer;
using Bytes = std::vector<std::byte>;
using st::Error;

int failures = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::cerr << "FAILED line " << __LINE__ << ": " << #condition << '\n';          \
            ++failures;                                                                     \
        }                                                                                   \
    } while (false)

// ---- bytes ----

void put_be32(Bytes& out, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(value >> (24 - 8 * i));
    }
}
void put_le32(Bytes& out, std::size_t at, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[at + static_cast<std::size_t>(i)] = static_cast<std::byte>(value >> (8 * i));
    }
}
void put_32(Bytes& out, std::size_t at, std::uint32_t value, bool big) {
    big ? put_be32(out, at, value) : put_le32(out, at, value);
}
std::uint16_t get_16(const Bytes& in, std::size_t at, bool big) {
    const unsigned a = std::to_integer<unsigned>(in[at]);
    const unsigned b = std::to_integer<unsigned>(in[at + 1]);
    return static_cast<std::uint16_t>(big ? (a << 8 | b) : (b << 8 | a));
}
void put_text(Bytes& out, std::size_t at, std::string_view text) {
    std::memcpy(out.data() + at, text.data(), text.size());
}
Bytes bytes_of(std::string_view text) {
    Bytes out(text.size());
    std::memcpy(out.data(), text.data(), text.size());
    return out;
}

// CRC-32 (zlib's), written out here so the test does not share the code it checks.
std::uint32_t crc32_of(const std::byte* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= std::to_integer<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
    }
    return ~crc;
}

// ---- save files ----

// A GameData.bin of two entries ("mario1", "config1"), each one chunk of 4 bytes, with its checksum.
Bytes make_game_data(bool big, std::uint32_t version, std::uint8_t seed = 1) {
    constexpr std::uint32_t kSize = 16 + 2 * 16 + 2 * 20;
    Bytes out((kSize + 31u) & ~31u);
    put_32(out, 4, version, big);
    put_32(out, 8, 2, big);
    put_32(out, 12, kSize, big);
    const char* const names[2] = {"mario1", "config1"};
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::size_t entry = 16 + i * 16;
        put_text(out, entry, names[i]);
        const std::uint32_t start = 48 + i * 20;
        put_32(out, entry + 12, start, big);
        out[start] = std::byte{1};      // the file's version
        out[start + 1] = std::byte{1};  // one chunk
        put_32(out, start + 4, 0x504C4159u, big);
        put_32(out, start + 8, 0x0027C90Fu + i, big);
        put_32(out, start + 12, 16, big);
        put_32(out, start + 16, 0x01020304u * (seed + i), big);
    }
    std::uint32_t sum = 0;
    std::uint32_t inverse = 0;
    for (std::size_t i = 4; i + 1 < kSize; i += 2) {
        const std::uint16_t word = get_16(out, i, big);
        sum += word;
        inverse += static_cast<std::uint16_t>(~word);
    }
    put_32(out, 0, (sum & 0xFFFFu) << 16 | (inverse & 0xFFFFu), big);
    return out;
}

// A Nebula.sav: "NBSV", version, the CRC-32 of what follows the header, rows of a 16-byte name and a size, the data.
Bytes make_native_save(std::uint32_t version = 4, std::uint8_t seed = 7) {
    const std::vector<std::pair<std::string, std::size_t>> rows = {{"mario1", 24}, {"config1", 8}};
    Bytes out(16 + rows.size() * 20);
    put_le32(out, 0, 0x5653424Eu);
    put_le32(out, 4, version);
    put_le32(out, 12, static_cast<std::uint32_t>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        put_text(out, 16 + i * 20, rows[i].first);
        put_le32(out, 16 + i * 20 + 16, static_cast<std::uint32_t>(rows[i].second));
        for (std::size_t j = 0; j < rows[i].second; ++j) {
            out.push_back(static_cast<std::byte>(seed + i * 31 + j));
        }
    }
    put_le32(out, 8, crc32_of(out.data() + 16, out.size() - 16));
    return out;
}

Bytes make_banner(std::uint8_t seed = 3) {
    Bytes out(0x72A0);
    put_be32(out, 0, 0x5749424Eu);  // "WIBN"
    out[9] = std::byte{2};          // animation speed
    for (std::size_t i = 32; i < out.size(); ++i) {
        out[i] = static_cast<std::byte>(i * seed);
    }
    return out;
}

// The Wii Mii database: "RNOD", "RNHD" at 0x1D00, and a CRC-16 (0x1021) in its last two bytes that makes the whole
// file's CRC zero.
Bytes make_mii_database() {
    Bytes out(0x1F1E0);
    for (std::size_t i = 4; i < out.size() - 2; ++i) {
        out[i] = static_cast<std::byte>(i * 13);
    }
    put_be32(out, 0, 0x524E4F44u);
    put_be32(out, 0x1D00, 0x524E4844u);
    out[out.size() - 2] = std::byte{0};
    out[out.size() - 1] = std::byte{0};
    unsigned crc = 0;
    for (const std::byte value : out) {
        unsigned data = std::to_integer<unsigned>(value);
        for (int bit = 0; bit < 8; ++bit, data <<= 1) {
            crc = (crc & 0x8000u) != 0 ? ((crc << 1) ^ 0x1021u) & 0xFFFFu : (crc << 1) & 0xFFFFu;
            if ((data & 0x80u) != 0) {
                crc ^= 1u;
            }
        }
    }
    out[out.size() - 2] = static_cast<std::byte>(crc >> 8);
    out[out.size() - 1] = static_cast<std::byte>(crc & 0xFFu);
    return out;
}

constexpr std::uint8_t kSdKey[16] = {0xab, 0x01, 0xb9, 0xd8, 0xe1, 0x62, 0x2b, 0x08,
                                     0xaf, 0xba, 0xd8, 0x4d, 0xbf, 0xc2, 0xa5, 0x5d};
constexpr std::uint8_t kSdIv[16] = {0x21, 0x67, 0x12, 0xe6, 0xaa, 0x1f, 0x68, 0x9f,
                                    0x95, 0xc5, 0xa2, 0x23, 0x24, 0xdc, 0x6a, 0x98};
constexpr std::uint8_t kMd5Blanker[16] = {0x0e, 0x65, 0x37, 0x81, 0x99, 0xbe, 0x45, 0x17,
                                          0xab, 0x06, 0xec, 0x22, 0x45, 0x1a, 0x57, 0x93};

void encrypt(const std::uint8_t iv_in[16], std::byte* data, std::size_t size) {
    AES_KEY key;
    AES_set_encrypt_key(kSdKey, 128, &key);
    std::uint8_t iv[16];
    std::memcpy(iv, iv_in, 16);
    auto* const p = reinterpret_cast<std::uint8_t*>(data);
    AES_cbc_encrypt(p, p, size, &key, iv, AES_ENCRYPT);
}

// A Wii SD card save (data.bin) of the title 00010000-<code> holding one GameData.bin, as the Wii writes it.
Bytes make_sd_save(std::string_view code, const Bytes& game_data, const Bytes& banner, bool break_md5 = false) {
    Bytes header(0xF0C0);
    put_be32(header, 0, 0x00010000u);
    put_text(header, 4, code);
    put_be32(header, 8, static_cast<std::uint32_t>(banner.size()));
    header[12] = std::byte{0x3c};
    std::memcpy(header.data() + 0x20, banner.data(), banner.size());
    std::memcpy(header.data() + 0x0E, kMd5Blanker, 16);
    std::uint8_t digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const std::uint8_t*>(header.data()), header.size(), digest);
    if (break_md5) {
        digest[3] ^= 0x40u;
    }
    std::memcpy(header.data() + 0x0E, digest, 16);
    const Bytes title(header.begin(), header.begin() + 8);
    encrypt(kSdIv, header.data(), header.size());

    Bytes out = header;
    Bytes bk(0x80);
    put_be32(bk, 0, 0x70);
    put_be32(bk, 4, 0x426B0001u);
    put_be32(bk, 12, 1);
    const std::size_t padded = (game_data.size() + 63u) & ~std::size_t{63};
    put_be32(bk, 16, static_cast<std::uint32_t>(0x80 + padded));
    std::memcpy(bk.data() + 0x60, title.data(), 8);
    out.insert(out.end(), bk.begin(), bk.end());

    Bytes file(0x80);
    put_be32(file, 0, 0x03ADF17Eu);
    put_be32(file, 4, static_cast<std::uint32_t>(game_data.size()));
    file[8] = std::byte{0x3c};
    file[10] = std::byte{1};
    put_text(file, 11, "GameData.bin");
    std::uint8_t iv[16];
    for (int i = 0; i < 16; ++i) {
        iv[i] = static_cast<std::uint8_t>(0x50 + i * 7);
    }
    std::memcpy(file.data() + 0x50, iv, 16);
    out.insert(out.end(), file.begin(), file.end());
    Bytes data = game_data;
    data.resize(padded);
    encrypt(iv, data.data(), data.size());
    out.insert(out.end(), data.begin(), data.end());
    out.resize(out.size() + 0x340);  // the console's signature and certificates
    return out;
}

Bytes make_zip(const std::vector<st::detail::ZipEntry>& entries) {
    Bytes zip;
    CHECK(st::detail::write_zip(entries, &zip));
    return zip;
}

// ---- folders ----

void write_all(const fs::path& file, const Bytes& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
Bytes read_all(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<char> chars((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Bytes out(chars.size());
    std::memcpy(out.data(), chars.data(), chars.size());
    return out;
}
// Every file below `root` by its '/' path.
std::vector<std::pair<std::string, Bytes>> listing(const fs::path& root) {
    std::vector<std::pair<std::string, Bytes>> files;
    if (!fs::exists(root)) {
        return files;
    }
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            files.emplace_back(fs::relative(entry.path(), root).generic_string(), read_all(entry.path()));
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}
// The folders beside `root` whose name starts with its own (a staging or backup folder).
std::vector<std::string> siblings(const fs::path& root) {
    std::vector<std::string> names;
    const std::string own = root.filename().string();
    for (const auto& entry : fs::directory_iterator(root.parent_path())) {
        const std::string name = entry.path().filename().string();
        if (name != own && name.starts_with(own)) {
            names.push_back(name);
        }
    }
    return names;
}

const st::SaveFile* file_in(const st::ImportPlan& plan, std::string_view path) {
    for (const st::SaveFile& file : plan.files) {
        if (file.path == path) {
            return &file;
        }
    }
    return nullptr;
}

const std::string kGameDataE = "title/00010000/524d4745/data/GameData.bin";
const std::string kGameDataP = "title/00010000/524d4750/data/GameData.bin";
const std::string kBannerP = "title/00010000/524d4750/data/banner.bin";
const std::string kMiiDatabaseP{galaxy::host::wii_mii_database_member()};

// ---- tests ----

void test_aes() {
    // NIST SP 800-38A F.2.2, CBC-AES128 decryption, two blocks.
    const std::uint8_t key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
    const std::uint8_t iv[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    std::uint8_t data[32] = {0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
                             0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2};
    const std::uint8_t plain[32] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
                                    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51};
    st::detail::aes128_cbc_decrypt(key, iv, data);
    CHECK(std::memcmp(data, plain, 32) == 0);
}

void test_names() {
    using st::detail::is_safe_entry_name;
    CHECK(is_safe_entry_name("a"));
    CHECK(is_safe_entry_name("a/b.bin"));
    CHECK(is_safe_entry_name("folder/"));
    CHECK(is_safe_entry_name("a..b/c"));
    CHECK(!is_safe_entry_name(""));
    CHECK(!is_safe_entry_name("../a"));
    CHECK(!is_safe_entry_name("a/../b"));
    CHECK(!is_safe_entry_name("a/.."));
    CHECK(!is_safe_entry_name("./a"));
    CHECK(!is_safe_entry_name("/a"));
    CHECK(!is_safe_entry_name("C:/a"));
    CHECK(!is_safe_entry_name("C:a"));
    CHECK(!is_safe_entry_name("a\\b"));
    CHECK(!is_safe_entry_name("a//b"));
    CHECK(!is_safe_entry_name(std::string_view("a\0b", 3)));
    CHECK(st::is_release_code("RMGE") && st::is_release_code("RMGK") && !st::is_release_code("RMGX") && !st::is_release_code("rmge"));
    CHECK(st::native_save_path("RMGJ") == "RMGJ/Nebula.sav");
    CHECK(st::game_data_path("RMGE") == kGameDataE);
}

void test_zip() {
    // A zip deflated by zlib (Python's zipfile): dir/hello.txt, "Nebula save transfer test. " 20 times.
    const unsigned char deflated[] = {
        0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x60, 0x6e, 0x5d, 0x73, 0xf5,
        0x6b, 0xc0, 0x22, 0x00, 0x00, 0x00, 0x1c, 0x02, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x64, 0x69,
        0x72, 0x2f, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78, 0x74, 0xf3, 0x4b, 0x4d, 0x2a, 0xcd,
        0x49, 0x54, 0x28, 0x4e, 0x2c, 0x4b, 0x55, 0x28, 0x29, 0x4a, 0xcc, 0x2b, 0x4e, 0x4b, 0x2d, 0x52,
        0x28, 0x49, 0x2d, 0x2e, 0xd1, 0x53, 0xf0, 0x1b, 0x95, 0x1a, 0xd9, 0x52, 0x00, 0x50, 0x4b, 0x01,
        0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x60, 0x6e, 0x5d, 0x73, 0xf5, 0x6b,
        0xc0, 0x22, 0x00, 0x00, 0x00, 0x1c, 0x02, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x00, 0x64, 0x69, 0x72, 0x2f, 0x68,
        0x65, 0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78, 0x74, 0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x3b, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    Bytes zip(sizeof(deflated));
    std::memcpy(zip.data(), deflated, sizeof(deflated));
    std::string expected;
    for (int i = 0; i < 20; ++i) {
        expected += "Nebula save transfer test. ";
    }
    std::vector<st::detail::ZipEntry> entries;
    CHECK(st::detail::read_zip(zip, &entries) == Error::kNone);
    CHECK(entries.size() == 1 && entries[0].name == "dir/hello.txt" && entries[0].bytes == bytes_of(expected));
    constexpr std::size_t kDirectory = 0x4d;
    Bytes bad_crc = zip;
    bad_crc[kDirectory + 16] ^= std::byte{1};
    CHECK(st::detail::read_zip(bad_crc, &entries) == Error::kDamaged);
    Bytes short_size = zip;  // inflates to one byte more than it says
    short_size[kDirectory + 24] = std::byte{0x1b};
    CHECK(st::detail::read_zip(short_size, &entries) == Error::kDamaged);
    Bytes encrypted = zip;
    encrypted[kDirectory + 8] |= std::byte{1};
    CHECK(st::detail::read_zip(encrypted, &entries) == Error::kUnsupported);
    Bytes other_method = zip;
    other_method[kDirectory + 10] = std::byte{12};  // bzip2
    CHECK(st::detail::read_zip(other_method, &entries) == Error::kUnsupported);
    CHECK(st::detail::read_zip(Bytes(zip.begin(), zip.end() - 1), &entries) == Error::kDamaged);
    CHECK(st::detail::read_zip(bytes_of("PK\x03\x04 not a zip at all, no end record here"), &entries) == Error::kDamaged);

    // The same archive as ZIP64: the sizes and offset in the central record's extra field, then a ZIP64 end record,
    // its locator, and an end record whose fields all say "see ZIP64".
    const auto zip64_of = [&zip](std::uint16_t extra_id, std::uint32_t disks) {
        Bytes out(zip.begin(), zip.begin() + kDirectory + 46 + 13);  // the local entry, the central record and its name
        const auto le = [&out](std::uint64_t value, int bytes) {
            for (int i = 0; i < bytes; ++i) {
                out.push_back(static_cast<std::byte>(value >> (8 * i) & 0xFFu));
            }
        };
        for (const std::size_t field : {std::size_t{20}, std::size_t{24}, std::size_t{42}}) {
            put_le32(out, kDirectory + field, 0xFFFFFFFFu);
        }
        out[kDirectory + 30] = std::byte{28};
        le(extra_id, 2), le(24, 2), le(0x21C, 8), le(0x22, 8), le(0, 8);
        const std::size_t record = out.size();
        le(0x06064B50u, 4), le(44, 8), le(45, 2), le(45, 2), le(0, 4), le(0, 4), le(1, 8), le(1, 8);
        le(record - kDirectory, 8), le(kDirectory, 8);
        le(0x07064B50u, 4), le(0, 4), le(record, 8), le(disks, 4);
        le(0x06054B50u, 4), le(0, 4), le(0xFFFF, 2), le(0xFFFF, 2), le(0xFFFFFFFFu, 4), le(0xFFFFFFFFu, 4), le(0, 2);
        return out;
    };
    CHECK(st::detail::read_zip(zip64_of(0x0001, 1), &entries) == Error::kNone);
    CHECK(entries.size() == 1 && entries[0].name == "dir/hello.txt" && entries[0].bytes == bytes_of(expected));
    CHECK(st::detail::read_zip(zip64_of(0x5455, 1), &entries) == Error::kDamaged);  // no ZIP64 field
    CHECK(st::detail::read_zip(zip64_of(0x0001, 2), &entries) == Error::kUnsupported);  // a zip in parts

    // Written and read back: names, an empty file, a folder entry left out.
    const std::vector<st::detail::ZipEntry> written = {
        {"a.txt", bytes_of("alpha")}, {"empty.bin", {}}, {"deep/er/b.bin", make_banner()}, {"folder/", {}}};
    const Bytes stored = make_zip(written);
    CHECK(st::detail::read_zip(stored, &entries) == Error::kNone);
    CHECK(entries.size() == 3 && entries[0].name == "a.txt" && entries[0].bytes == bytes_of("alpha") &&
          entries[1].bytes.empty() && entries[2].bytes == make_banner());

    for (const char* name : {"../evil.bin", "a/../../evil.bin", "/abs.bin", "C:/drive.bin", "a\\..\\evil.bin"}) {
        CHECK(st::detail::read_zip(make_zip({{"ok.bin", bytes_of("x")}, {name, bytes_of("y")}}), &entries) == Error::kDamaged);
        CHECK(st::plan_import(make_zip({{kGameDataE, make_game_data(true, 2)}, {name, bytes_of("y")}}), "RMGE").error ==
              Error::kDamaged);
    }
    CHECK(st::detail::read_zip(make_zip({{"Same.bin", bytes_of("x")}, {"same.BIN", bytes_of("y")}}), &entries) == Error::kDamaged);

    std::vector<st::detail::ZipEntry> many;
    for (std::size_t i = 0; i <= st::kMaxEntries; ++i) {
        many.push_back({"f" + std::to_string(i), {}});
    }
    CHECK(st::detail::read_zip(make_zip(many), &entries) == Error::kTooLarge);
    CHECK(st::detail::read_zip(make_zip({{"big.bin", Bytes(st::kMaxFileBytes + 1)}}), &entries) == Error::kTooLarge);
}

void test_single_files() {
    struct Case {
        Bytes bytes;
        st::GameDataFormat format;
    };
    const Case cases[] = {{make_game_data(true, 2), st::GameDataFormat::kWii},
                          {make_game_data(false, 2), st::GameDataFormat::kSwitch},
                          {make_game_data(false, 3), st::GameDataFormat::kNative}};
    for (const Case& c : cases) {
        const st::ImportPlan plan = st::plan_import(c.bytes, "RMGE");
        CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kGameData && plan.format == c.format);
        CHECK(plan.files.size() == 1 && plan.files[0].path == kGameDataE && plan.files[0].bytes == c.bytes);
        CHECK(plan.codes == std::vector<std::string>{"RMGE"} && plan.target_code == "RMGE");
    }
    // Placed for the target, whatever release it came from.
    CHECK(st::plan_import(make_game_data(true, 2), "RMGP").files[0].path == kGameDataP);

    Bytes bad_sum = make_game_data(true, 2);
    bad_sum[66] ^= std::byte{0x10};
    st::ImportPlan plan = st::plan_import(bad_sum, "RMGE");
    CHECK(plan.error == Error::kDamaged && plan.kind == st::Kind::kGameData && plan.files.empty());
    Bytes bad_length = make_game_data(false, 2);
    bad_length.push_back(std::byte{0});
    CHECK(st::plan_import(bad_length, "RMGE").error == Error::kDamaged);

    const Bytes native = make_native_save();
    plan = st::plan_import(native, "RMGJ");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kNebulaSave && plan.format == st::GameDataFormat::kNone);
    CHECK(plan.files.size() == 1 && plan.files[0].path == "RMGJ/Nebula.sav" && plan.files[0].bytes == native);
    Bytes bad_crc = native;
    bad_crc.back() ^= std::byte{1};
    CHECK(st::plan_import(bad_crc, "RMGE").error == Error::kDamaged);
    CHECK(st::plan_import(make_native_save(5), "RMGE").error == Error::kNewerVersion);
    Bytes bad_rows = native;
    put_le32(bad_rows, 16 + 16, 4096);  // a row past the end
    put_le32(bad_rows, 8, crc32_of(bad_rows.data() + 16, bad_rows.size() - 16));
    CHECK(st::plan_import(bad_rows, "RMGE").error == Error::kDamaged);

    Bytes datel(0x2000);
    put_text(datel, 4, "WIISAVE001");
    plan = st::plan_import(datel, "RMGE");
    CHECK(plan.error == Error::kUnsupported && plan.kind == st::Kind::kDatelSave);

    plan = st::plan_import(bytes_of("not a save, only some text that is long enough"), "RMGE");
    CHECK(plan.error == Error::kUnsupported && plan.kind == st::Kind::kUnknown);
    plan = st::plan_import(bytes_of("hi"), "RMGE");
    CHECK(plan.error == Error::kUnsupported && plan.kind == st::Kind::kUnknown);
    CHECK(st::plan_import(Bytes{}, "RMGE").error == Error::kUnsupported);
    CHECK(st::plan_import(make_game_data(true, 2), "SB4E").error == Error::kWrongGame);
    CHECK(st::plan_import(Bytes(st::kMaxSourceBytes + 1), "RMGE").error == Error::kTooLarge);
}

void test_sd_save() {
    const Bytes game_data = make_game_data(true, 2, 9);
    const Bytes banner = make_banner();
    st::ImportPlan plan = st::plan_import(make_sd_save("RMGP", game_data, banner), "RMGE");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kWiiSdSave && plan.format == st::GameDataFormat::kWii);
    CHECK(plan.source_code == "RMGP" && plan.target_code == "RMGE" && !plan.banner_left_out);
    CHECK(plan.files.size() == 2 && file_in(plan, kGameDataE) != nullptr && file_in(plan, kGameDataE)->bytes == game_data);
    CHECK(file_in(plan, "title/00010000/524d4745/data/banner.bin") != nullptr &&
          file_in(plan, "title/00010000/524d4745/data/banner.bin")->bytes == banner);

    Bytes bad_banner = banner;
    bad_banner[0] = std::byte{'X'};
    plan = st::plan_import(make_sd_save("RMGE", game_data, bad_banner), "RMGE");
    CHECK(plan.error == Error::kNone && plan.banner_left_out && plan.files.size() == 1);

    CHECK(st::plan_import(make_sd_save("RMGE", game_data, banner, true), "RMGE").error == Error::kDamaged);
    plan = st::plan_import(make_sd_save("SB4E", game_data, banner), "RMGE");
    CHECK(plan.error == Error::kWrongGame && plan.kind == st::Kind::kWiiSdSave);
    Bytes bad_inner = game_data;
    bad_inner[70] ^= std::byte{4};
    CHECK(st::plan_import(make_sd_save("RMGE", bad_inner, banner), "RMGE").error == Error::kDamaged);
    Bytes cut = make_sd_save("RMGE", game_data, banner);
    cut.resize(0xF0C0 + 0x80 + 0x80 + 32);  // the file's data cut short
    CHECK(st::plan_import(cut, "RMGE").error == Error::kDamaged);
}

void test_zip_sources() {
    const Bytes wii = make_game_data(true, 2);
    const Bytes banner = make_banner();
    // A Dolphin save folder below a folder of its own.
    st::ImportPlan plan = st::plan_import(
        make_zip({{"readme.txt", bytes_of("hello")},
                  {"Wii/title/00010000/524d4745/data/GameData.bin", wii},
                  {"Wii/title/00010000/524d4745/data/banner.bin", banner},
                  {"Wii/title/00010000/524d4745/data/other.bin", bytes_of("left out")}}),
        "RMGE");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kZip && plan.format == st::GameDataFormat::kWii);
    CHECK(plan.files.size() == 2 && file_in(plan, kGameDataE) != nullptr && file_in(plan, "title/00010000/524d4745/data/banner.bin") != nullptr);

    // One other release alone: placed for the target.
    plan = st::plan_import(make_zip({{kGameDataP, wii}, {kBannerP, banner}}), "RMGE");
    CHECK(plan.error == Error::kNone && plan.source_code == "RMGP" && plan.codes == std::vector<std::string>{"RMGE"});
    CHECK(file_in(plan, kGameDataE) != nullptr && file_in(plan, kGameDataP) == nullptr);
    // Two other releases: which one is meant is not known.
    CHECK(st::plan_import(make_zip({{kGameDataP, wii}, {"RMGJ/Nebula.sav", make_native_save()}}), "RMGE").error ==
          Error::kWrongRegion);
    // The target's and another's: each keeps its own.
    plan = st::plan_import(make_zip({{kGameDataP, wii}, {"RMGE/Nebula.sav", make_native_save()}}), "RMGE");
    CHECK(plan.error == Error::kNone && plan.codes == (std::vector<std::string>{"RMGE", "RMGP"}));
    CHECK(file_in(plan, "RMGE/Nebula.sav") != nullptr && file_in(plan, kGameDataP) != nullptr && plan.format == st::GameDataFormat::kNone);

    // Copies: the newest that passes the check; all failing is damaged.
    Bytes damaged = make_native_save();
    damaged.back() ^= std::byte{1};
    const Bytes older = make_native_save(4, 40);
    plan = st::plan_import(make_zip({{"rmge/nebula.sav", damaged}, {"RMGE/Nebula.sav.1", older}}), "RMGE");
    CHECK(plan.error == Error::kNone && plan.files.size() == 1 && plan.files[0].path == "RMGE/Nebula.sav" &&
          plan.files[0].bytes == older);
    CHECK(st::plan_import(make_zip({{"RMGE/Nebula.sav", damaged}}), "RMGE").error == Error::kDamaged);

    // Mods' files and the Mii files, each by its own check.
    plan = st::plan_import(make_zip({{"RMGE/Nebula.sav", make_native_save()},
                                     {"RMGE/luma/abc_1.bin", bytes_of("mod")},
                                     {"RMGE/luma/replaced/x.bin", bytes_of("old")},
                                     {"RMGE/luma/bad name.bin", bytes_of("no")},
                                     {"RMGE/luma/.hidden", bytes_of("no")},
                                     {"nebula-miis.txt", bytes_of("nebula-mii-library 1\n")},
                                     {kMiiDatabaseP, make_mii_database()}}),
                           "RMGE");
    CHECK(plan.error == Error::kNone && plan.files.size() == 5 && plan.mii_library && !plan.miis_left_out);
    CHECK(file_in(plan, "RMGE/luma/abc_1.bin") != nullptr && file_in(plan, "RMGE/luma/replaced/x.bin") != nullptr);
    Bytes bad_database = make_mii_database();
    bad_database[100] ^= std::byte{1};
    plan = st::plan_import(make_zip({{"RMGE/Nebula.sav", make_native_save()}, {kMiiDatabaseP, bad_database}}), "RMGE");
    CHECK(plan.error == Error::kNone && plan.miis_left_out && plan.files.size() == 1);

    // A loose save file in a folder of the zip, its banner beside it.
    plan = st::plan_import(make_zip({{"notes.txt", bytes_of("text")}, {"My Save/GameData.bin", make_game_data(false, 2)},
                                     {"My Save/banner.bin", banner}}),
                           "RMGE");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kZip && plan.format == st::GameDataFormat::kSwitch && plan.files.size() == 2);
    // A Wii SD save inside a zip.
    plan = st::plan_import(make_zip({{"private/wii/title/RMGE/data.bin", make_sd_save("RMGE", wii, banner)}}), "RMGE");
    CHECK(plan.error == Error::kNone && plan.files.size() == 2);
    // A damaged save alone is reported as damaged.
    CHECK(st::plan_import(make_zip({{"x/GameData.bin", damaged}}), "RMGE").error == Error::kDamaged);
    // Another game's save; nothing at all.
    CHECK(st::plan_import(make_zip({{"title/00010000/53423445/data/GameData.bin", wii}}), "RMGE").error == Error::kWrongGame);
    CHECK(st::plan_import(make_zip({{"a.txt", bytes_of("nothing")}}), "RMGE").error == Error::kNoSaves);
}

void test_commit(const fs::path& base) {
    const fs::path root = base / "commit" / "saves";
    const Bytes old_native = make_native_save(4, 1);
    const Bytes old_copy = make_native_save(4, 2);
    const Bytes old_game_data = make_game_data(true, 2, 3);
    const Bytes keep = bytes_of("another release's save");
    write_all(root / "RMGE" / "Nebula.sav", old_native);
    write_all(root / "RMGE" / "Nebula.sav.1", old_copy);
    write_all(root / fs::path(kGameDataE), old_game_data);
    write_all(root / "RMGP" / "Nebula.sav", keep);
    const auto before = listing(root);

    const Bytes new_game_data = make_game_data(false, 2, 5);
    const st::ImportPlan plan = st::plan_import(new_game_data, "RMGE");
    CHECK(plan.error == Error::kNone && st::replaces_saves(plan, root));
    CHECK(!st::replaces_saves(st::plan_import(new_game_data, "RMGJ"), root));

    // A failure at any move puts every file back and leaves no folder beside the root.
    for (const int step : {1, 2, 3, 4}) {
        st::detail::set_commit_fault(step);
        const st::ImportResult failed = st::commit_import(plan, root);
        st::detail::set_commit_fault(0);
        CHECK(failed.error == Error::kIo && failed.backup.empty());
        CHECK(listing(root) == before);
        CHECK(siblings(root).empty());
    }

    // The process ends at any move (P-081: before, the save folder could be left with no save, and nothing at the
    // start found the import's folders): its journal stays, and the next start's recover_import puts the save back.
    CHECK(st::recover_import(root).empty());
    for (const int step : {1, 2, 3, 4}) {
        st::detail::set_commit_crash(step);
        const st::ImportResult crashed = st::commit_import(plan, root);
        st::detail::set_commit_crash(0);
        CHECK(crashed.error == Error::kIo);
        CHECK(fs::exists(root.parent_path() / "saves.import-journal"));
        const std::string said = st::recover_import(root);
        CHECK(said.starts_with("a save import that stopped half way was undone"));
        CHECK(listing(root) == before);
        CHECK(siblings(root).empty());
        CHECK(st::recover_import(root).empty());
    }
    // A journal this Nebula did not write moves nothing and stays.
    write_all(root.parent_path() / "saves.import-journal",
              bytes_of("nebula save import journal 1\nstaging\tsaves.import\nbackup\t../elsewhere\nreplaced\tRMGE/Nebula.sav\n"));
    CHECK(st::recover_import(root).find("is not one this Nebula wrote") != std::string::npos);
    CHECK(listing(root) == before);
    fs::remove(root.parent_path() / "saves.import-journal");

    const st::ImportResult result = st::commit_import(plan, root);
    CHECK(result.error == Error::kNone && result.written == 1 && !result.backup.empty());
    const auto after = listing(root);
    CHECK(after.size() == 2 && after[0].first == "RMGP/Nebula.sav" && after[0].second == keep && after[1].first == kGameDataE &&
          after[1].second == new_game_data);
    const auto backup = listing(result.backup);
    CHECK(backup.size() == 3 && backup[0].first == "RMGE/Nebula.sav" && backup[0].second == old_native &&
          backup[1].first == "RMGE/Nebula.sav.1" && backup[1].second == old_copy && backup[2].first == kGameDataE &&
          backup[2].second == old_game_data);
    CHECK(result.backup.parent_path() == root.parent_path() && result.backup.filename().string().starts_with("saves-backup-"));
    std::vector<std::string> beside = siblings(root);
    CHECK(beside.size() == 1);

    // Into a root that does not exist yet: nothing replaced, no backup.
    const fs::path fresh = base / "commit" / "fresh";
    const st::ImportResult first = st::commit_import(st::plan_import(make_native_save(), "RMGK"), fresh);
    CHECK(first.error == Error::kNone && first.backup.empty() && listing(fresh).size() == 1 && listing(fresh)[0].first == "RMGK/Nebula.sav");
    // A plan that failed writes nothing.
    CHECK(st::commit_import(st::plan_import(bytes_of("no"), "RMGE"), fresh).error == Error::kUnsupported);
}

void test_export(const fs::path& base) {
    const fs::path root = base / "export" / "saves";
    std::vector<st::detail::ZipEntry> entries;
    Bytes zip;
    CHECK(st::make_export(root, "2.0.0", &zip) == Error::kNoSaves && zip.empty());
    fs::create_directories(root);
    CHECK(st::make_export(root, "2.0.0", &zip) == Error::kNoSaves);

    Bytes damaged = make_native_save();
    damaged.back() ^= std::byte{1};
    write_all(root / "RMGE" / "Nebula.sav", damaged);
    CHECK(st::make_export(root, "2.0.0", &zip) == Error::kDamaged);

    const Bytes native = make_native_save(4, 11);
    const Bytes game_data = make_game_data(true, 2, 12);
    const Bytes banner = make_banner(5);
    const Bytes database = make_mii_database();
    const Bytes library = bytes_of("nebula-mii-library 1\nmii data\n");
    write_all(root / "RMGE" / "Nebula.sav", native);
    write_all(root / "RMGE" / "Nebula.sav.1", damaged);
    write_all(root / fs::path(kGameDataP), game_data);
    write_all(root / fs::path(kBannerP), banner);
    write_all(root / "RMGE" / "luma" / "abc.bin", bytes_of("mod value"));
    write_all(root / "RMGE" / "luma" / "replaced" / "def.bin", bytes_of("replaced value"));
    write_all(root / "RMGE" / "luma" / "not a mod file.txt", bytes_of("left out"));
    write_all(root / "nebula-miis.txt", library);
    write_all(root / fs::path(kMiiDatabaseP), database);
    write_all(root / "unrelated.txt", bytes_of("left out"));

    CHECK(st::make_export(root, "2.0.0 \"beta\"", &zip) == Error::kNone);
    CHECK(st::detail::read_zip(zip, &entries) == Error::kNone && entries.size() == 8 && entries[0].name == "nebula-saves.json");

    const st::ImportPlan plan = st::plan_import(zip, "RMGE");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kNebulaArchive && plan.files.size() == 7 && plan.mii_library);
    CHECK(plan.codes == (std::vector<std::string>{"RMGE", "RMGP"}));
    CHECK(file_in(plan, "RMGE/Nebula.sav") != nullptr && file_in(plan, "RMGE/Nebula.sav")->bytes == native);

    // Read back into a new root: the same files.
    const fs::path copy = base / "export" / "copy";
    CHECK(st::commit_import(plan, copy).error == Error::kNone);
    auto expected = listing(root);
    std::erase_if(expected, [](const auto& file) {
        return file.first == "RMGE/Nebula.sav.1" || file.first == "unrelated.txt" || file.first.ends_with("not a mod file.txt");
    });
    CHECK(listing(copy) == expected);

    // The install's achievements and stats travel with the saves (P-119); not written into the save root.
    {
        std::vector<std::byte> with_progress;
        CHECK(st::make_export(root, "2.0.0", &with_progress, st::Progress{"comets=2026-01-01T00:00:00Z\n", "coins=5\n"}) ==
              Error::kNone);
        std::vector<st::detail::ZipEntry> progress_entries;
        CHECK(st::detail::read_zip(with_progress, &progress_entries) == Error::kNone && progress_entries.size() == 10);
        const st::ImportPlan carried = st::plan_import(with_progress, "RMGE");
        CHECK(carried.error == Error::kNone && carried.files.size() == 7);
        CHECK(carried.achievements == "comets=2026-01-01T00:00:00Z\n" && carried.stats == "coins=5\n");
        CHECK(plan.achievements.empty() && plan.stats.empty());
        std::vector<std::byte> stats_only;
        CHECK(st::make_export(root, "2.0.0", &stats_only, st::Progress{"", "coins=5\n"}) == Error::kNone);
        CHECK(st::detail::read_zip(stats_only, &progress_entries) == Error::kNone && progress_entries.size() == 9);
        // A changed progress file fails the archive's digest check, as a save would.
        std::vector<st::detail::ZipEntry> tampered;
        CHECK(st::detail::read_zip(with_progress, &tampered) == Error::kNone);
        for (st::detail::ZipEntry& entry : tampered) {
            if (entry.name == "saves/nebula-stats.ini") entry.bytes = bytes_of("coins=9\n");
        }
        CHECK(st::plan_import(make_zip(tampered), "RMGE").error == Error::kDamaged);
    }

    // The archive's own checks.
    const auto with_manifest = [&entries](std::string manifest) {
        std::vector<st::detail::ZipEntry> changed = entries;
        changed[0].bytes = bytes_of(manifest);
        return make_zip(changed);
    };
    const std::string manifest(reinterpret_cast<const char*>(entries[0].bytes.data()), entries[0].bytes.size());
    CHECK(manifest.find("\"nebula\":\"2.0.0 \\\"beta\\\"\"") != std::string::npos);
    std::string newer = manifest;
    newer.replace(newer.find("\"version\":1"), 11, "\"version\":2");
    CHECK(st::plan_import(with_manifest(newer), "RMGE").error == Error::kNewerVersion);
    std::string other = manifest;
    other.replace(other.find("nebula-saves"), 12, "other-format");
    CHECK(st::plan_import(with_manifest(other), "RMGE").error == Error::kUnsupported);
    std::string wrong_digest = manifest;
    const std::size_t digest = wrong_digest.find("\"sha256\":\"") + 10;
    wrong_digest[digest] = wrong_digest[digest] == '0' ? '1' : '0';
    CHECK(st::plan_import(with_manifest(wrong_digest), "RMGE").error == Error::kDamaged);
    CHECK(st::plan_import(with_manifest("{\"format\":\"nebula-saves\""), "RMGE").error == Error::kDamaged);
    CHECK(st::plan_import(with_manifest("\xEF\xBB\xBF" + manifest), "RMGE").error == Error::kNone);
    std::vector<st::detail::ZipEntry> missing = entries;
    missing.pop_back();
    CHECK(st::plan_import(make_zip(missing), "RMGE").error == Error::kDamaged);
    // 1.0.2's folder items and files this version does not keep are passed over.
    std::string older = manifest;
    older.replace(older.find("\"items\":[") + 9, 0,
                  "{\"path\":\"shared2/sys/SYSCONF\",\"type\":\"file\",\"size\":1,\"sha256\":\"00\",\"streams\":[]},"
                  "{\"path\":\"title/00010000/524d4745/data/sub\",\"type\":\"folder\",\"streams\":[]},");
    CHECK(st::plan_import(with_manifest(older), "RMGE").error == Error::kNone);

    // An archive unpacked to a folder reads the same.
    const fs::path unpacked = base / "export" / "unpacked";
    for (const st::detail::ZipEntry& entry : entries) {
        write_all(unpacked / fs::path(entry.name), entry.bytes);
    }
    const st::ImportPlan from_folder = st::plan_import(unpacked, "RMGE");
    CHECK(from_folder.error == Error::kNone && from_folder.kind == st::Kind::kNebulaArchive && from_folder.files.size() == 7);

    // To a file, named by the time, a second one beside it, each read back by the import.
    const fs::path out = base / "export" / "out";
    const st::ExportResult a = st::export_saves(root, out, "2.0.0");
    const st::ExportResult b = st::export_saves(root, out, "2.0.0");
    CHECK(a.error == Error::kNone && b.error == Error::kNone && a.file != b.file);
    CHECK(a.file.filename().string().starts_with("Nebula-Saves-") && a.file.extension() == ".zip");
    CHECK(st::plan_import(a.file, "RMGE").error == Error::kNone && st::plan_import(b.file, "RMGE").error == Error::kNone);
    std::size_t partial = 0;
    for (const auto& entry : fs::directory_iterator(out)) {
        partial += entry.path().extension() == ".partial" ? 1u : 0u;
    }
    CHECK(partial == 0);
}

void test_folders(const fs::path& base) {
    const Bytes wii = make_game_data(true, 2);
    // Dolphin's Wii folder, below the folder chosen.
    const fs::path dolphin = base / "folders" / "dolphin";
    write_all(dolphin / "Wii" / fs::path(kGameDataE), wii);
    write_all(dolphin / "Wii" / "title" / "00010000" / "524d4745" / "data" / "banner.bin", make_banner());
    st::ImportPlan plan = st::plan_import(dolphin, "RMGE");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kFolder && plan.files.size() == 2);

    // A folder holding a GameData.bin and its banner (Dolphin's Open Wii Save Folder).
    const fs::path loose = base / "folders" / "loose";
    write_all(loose / "GameData.bin", wii);
    write_all(loose / "banner.bin", make_banner());
    write_all(loose / "notes.txt", bytes_of("x"));
    plan = st::plan_import(loose, "RMGP");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kFolder && plan.files.size() == 2 && file_in(plan, kGameDataP) != nullptr);
    plan = st::plan_import(loose / "GameData.bin", "RMGE");
    CHECK(plan.error == Error::kNone && plan.kind == st::Kind::kGameData);

    // A folder holding another game's save; an empty folder; a path that is not there.
    const fs::path other = base / "folders" / "other";
    write_all(other / "title" / "00010000" / "53423445" / "data" / "GameData.bin", wii);
    write_all(other / "RMGE" / "notes.txt", bytes_of("x"));
    CHECK(st::plan_import(other, "RMGE").error == Error::kWrongGame);
    const fs::path empty = base / "folders" / "empty";
    fs::create_directories(empty);
    CHECK(st::plan_import(empty, "RMGE").error == Error::kNoSaves);
    CHECK(st::plan_import(base / "folders" / "missing.bin", "RMGE").error == Error::kIo);

    // A save folder whose own file system copy is damaged.
    const fs::path damaged = base / "folders" / "damaged";
    Bytes bad = wii;
    bad[66] ^= std::byte{1};
    write_all(damaged / fs::path(kGameDataE), bad);
    CHECK(st::plan_import(damaged, "RMGE").error == Error::kDamaged);
}

}  // namespace

// ---- the per-file import ----

// A Nebula.sav with these rows, in this order (a row's data: `seed`, its index and its place).
Bytes make_rows(const std::vector<std::pair<std::string, std::size_t>>& rows, std::uint8_t seed) {
    Bytes out(16 + rows.size() * 20);
    put_le32(out, 0, 0x5653424Eu);
    put_le32(out, 4, 4);
    put_le32(out, 12, static_cast<std::uint32_t>(rows.size()));
    for (std::size_t i = 0; i < rows.size(); ++i) {
        put_text(out, 16 + i * 20, rows[i].first);
        put_le32(out, 16 + i * 20 + 16, static_cast<std::uint32_t>(rows[i].second));
        for (std::size_t j = 0; j < rows[i].second; ++j) {
            out.push_back(static_cast<std::byte>(seed * 17 + i * 31 + j));
        }
    }
    put_le32(out, 8, crc32_of(out.data() + 16, out.size() - 16));
    return out;
}

// The game's 18 rows (each user's Mario, Luigi, config), each sized by its user and kind.
Bytes make_full_save(std::uint8_t seed) {
    std::vector<std::pair<std::string, std::size_t>> rows;
    for (int user = 1; user <= 6; ++user) {
        for (const char* kind : {"mario", "luigi", "config"}) {
            rows.push_back({kind + std::to_string(user), static_cast<std::size_t>(8 + user * 3 + std::strlen(kind))});
        }
    }
    return make_rows(rows, seed);
}

std::vector<std::pair<std::string, Bytes>> native_rows(const Bytes& save) {
    std::vector<std::pair<std::string, Bytes>> rows;
    const std::size_t count = static_cast<std::size_t>(std::to_integer<unsigned>(save[12])) |
                              static_cast<std::size_t>(std::to_integer<unsigned>(save[13])) << 8;
    std::size_t place = 16 + count * 20;
    for (std::size_t i = 0; i < count; ++i) {
        const char* const name = reinterpret_cast<const char*>(save.data() + 16 + i * 20);
        const std::size_t size = static_cast<std::size_t>(std::to_integer<unsigned>(save[16 + i * 20 + 16])) |
                                 static_cast<std::size_t>(std::to_integer<unsigned>(save[16 + i * 20 + 17])) << 8;
        rows.push_back({std::string(name, strnlen(name, 16)), Bytes(save.begin() + static_cast<std::ptrdiff_t>(place),
                                                                    save.begin() + static_cast<std::ptrdiff_t>(place + size))});
        place += size;
    }
    return rows;
}

Bytes row_of(const Bytes& save, std::string_view name) {
    for (auto& [row, bytes] : native_rows(save)) {
        if (row == name) return bytes;
    }
    return bytes_of("absent");
}

std::int32_t no_read(const std::uint8_t*, std::uint32_t, NebulaSaveImportDone, void*) { return NEBULA_SAVE_IMPORT_BUSY; }

void test_by_file(const fs::path& base) {
    const Bytes current = make_full_save(1);
    const Bytes imported = make_full_save(2);
    Bytes merged;
    const std::array<std::uint8_t, 6> targets = {3, 0, 0, 0, 0, 1};
    CHECK(st::merge_saves(current, imported, targets, &merged) == Error::kNone);
    // The game's order, all 18 rows; imported 1 in 3 and imported 6 in 1, the rest as they were.
    const auto rows = native_rows(merged);
    CHECK(rows.size() == 18);
    std::size_t at = 0;
    for (int user = 1; user <= 6; ++user) {
        for (const char* kind : {"mario", "luigi", "config"}) {
            const std::string name = kind + std::to_string(user);
            CHECK(at < rows.size() && rows[at].first == name);
            const int from = user == 3 ? 1 : user == 1 ? 6 : 0;
            const Bytes expected = from != 0 ? row_of(imported, kind + std::to_string(from)) : row_of(current, name);
            CHECK(at < rows.size() && rows[at].second == expected);
            ++at;
        }
    }
    CHECK(st::plan_native_save(merged, "RMGE").error == Error::kNone);  // its CRC and sizes check

    // An imported save without a user's every row: the missing row is empty in the target.
    const Bytes partial = make_rows({{"mario1", 24}, {"config1", 8}}, 9);
    CHECK(st::merge_saves(current, partial, std::array<std::uint8_t, 6>{4, 0, 0, 0, 0, 0}, &merged) == Error::kNone);
    CHECK(row_of(merged, "mario4") == row_of(partial, "mario1") && row_of(merged, "luigi4").empty() &&
          row_of(merged, "config4") == row_of(partial, "config1") && row_of(merged, "mario5") == row_of(current, "mario5"));

    // A choice never sends two files to one, nor past the sixth; one at least; both saves the game's.
    CHECK(st::merge_saves(current, imported, std::array<std::uint8_t, 6>{2, 2, 0, 0, 0, 0}, &merged) == Error::kUnsupported);
    CHECK(st::merge_saves(current, imported, std::array<std::uint8_t, 6>{7, 0, 0, 0, 0, 0}, &merged) == Error::kUnsupported);
    CHECK(st::merge_saves(current, imported, std::array<std::uint8_t, 6>{}, &merged) == Error::kNoSaves && merged.empty());
    Bytes damaged = current;
    damaged.back() ^= std::byte{1};
    CHECK(st::merge_saves(damaged, imported, targets, &merged) == Error::kDamaged);
    CHECK(st::merge_saves(current, make_game_data(true, 2), targets, &merged) == Error::kDamaged);
    CHECK(st::merge_saves(current, Bytes{}, targets, &merged) == Error::kDamaged);

    // The plan that writes it: the release's save whole, its copies and an old GameData.bin to the backup.
    CHECK(st::plan_native_save(bytes_of("not a save"), "RMGE").error == Error::kDamaged);
    CHECK(st::plan_native_save(merged, "XXXX").error == Error::kWrongGame);
    CHECK(st::merge_saves(current, imported, targets, &merged) == Error::kNone);
    const st::ImportPlan plan = st::plan_native_save(merged, "RMGE");
    CHECK(plan.error == Error::kNone && plan.files.size() == 1 && plan.files[0].path == "RMGE/Nebula.sav" &&
          plan.codes == std::vector<std::string>{"RMGE"});
    const fs::path root = base / "by-file" / "saves";
    const Bytes keep = bytes_of("another release's save");
    write_all(root / "RMGE" / "Nebula.sav", current);
    write_all(root / "RMGE" / "Nebula.sav.2", make_native_save(4, 5));
    write_all(root / fs::path(kGameDataE), make_game_data(true, 2, 3));
    write_all(root / "RMGP" / "Nebula.sav", keep);
    const auto before = listing(root);
    // A quit or a failure at any move: every file as it was.
    for (const int step : {1, 2, 3, 4}) {
        st::detail::set_commit_fault(step);
        CHECK(st::commit_import(plan, root).error == Error::kIo);
        st::detail::set_commit_fault(0);
        CHECK(listing(root) == before && siblings(root).empty());
    }
    const st::ImportResult result = st::commit_import(plan, root);
    CHECK(result.error == Error::kNone && result.written == 1);
    const auto after = listing(root);
    CHECK(after.size() == 2 && after[0].first == "RMGE/Nebula.sav" && after[0].second == merged &&
          after[1].first == "RMGP/Nebula.sav" && after[1].second == keep);
    CHECK(listing(result.backup).size() == 3);

    // The release's save in a plan, and whether the plan holds more.
    st::ImportPlan single = st::plan_import(make_game_data(true, 2, 3), "RMGE");
    CHECK(st::release_save(single) != nullptr && st::release_save(single)->path == kGameDataE &&
          !st::places_more_than_save(single));
    st::ImportPlan with_miis = st::plan_import(
        make_zip({{"RMGE/Nebula.sav", make_native_save()}, {kMiiDatabaseP, make_mii_database()}}), "RMGE");
    CHECK(with_miis.error == Error::kNone && st::release_save(with_miis) != nullptr &&
          st::release_save(with_miis)->path == "RMGE/Nebula.sav" && st::places_more_than_save(with_miis));
    st::ImportPlan other_release = st::plan_import(make_zip({{"RMGJ/Nebula.sav", make_native_save()}}), "RMGE");
    CHECK(other_release.error == Error::kNone && st::release_save(other_release) != nullptr);
    CHECK(st::release_save(st::plan_import(bytes_of("no"), "RMGE")) == nullptr);

    // The game's reader, as the host gives it.
    CHECK(st::game_reader() == nullptr);
    st::set_game_reader(no_read);
    CHECK(st::game_reader() == no_read);
    st::set_game_reader(nullptr);
    CHECK(st::game_reader() == nullptr);
}

// A dropped file is a save by its name or by its contents; a mod's zip, a Mii file and any other file are not.
void test_save_source(const fs::path& base) {
    const auto source = [](const Bytes& bytes, const fs::path& name) {
        return st::is_save_source(fs::path("drop") / name, st::plan_import(bytes, "RMGE"));
    };
    CHECK(source(make_game_data(true, 2), "a.bin") && source(make_game_data(false, 3), "save"));
    Bytes broken = make_game_data(true, 2);
    broken[40] ^= std::byte{1};
    CHECK(!source(broken, "a.bin") && source(broken, "GameData.bin") && source(broken, "gamedata.BIN"));
    CHECK(source(make_native_save(), "x") && source(make_native_save(9), "x"));  // a later version: still a save
    Bytes broken_native = make_native_save();
    broken_native.back() ^= std::byte{1};
    CHECK(source(broken_native, "x") && source(bytes_of("anything"), "Nebula.sav"));
    CHECK(source(make_sd_save("RMGE", make_game_data(true, 2), make_banner()), "data.bin"));
    CHECK(source(make_zip({{"RMGE/Nebula.sav", make_native_save()}}), "saves.zip"));
    CHECK(source(make_zip({{"nebula-saves.json", bytes_of("{")}}), "export.zip"));  // a damaged Nebula saves archive
    CHECK(!source(make_zip({{"mod.json", bytes_of("{}")}, {"textures/a.dds", bytes_of("dds")}}), "Cool Mod.zip"));
    CHECK(!source(make_mii_database(), fs::path(kMiiDatabaseP).filename()) && !source(bytes_of("not a save"), "a.mii") &&
          !source(Bytes{}, "x"));

    const fs::path folder = base / "drop-folder";
    write_all(folder / "saves" / "RMGE" / "Nebula.sav", make_native_save());
    CHECK(st::is_save_source(folder, st::plan_import(folder, "RMGE")));
    const fs::path mod = base / "drop-mod";
    write_all(mod / "mod.json", bytes_of("{}"));
    write_all(mod / "textures" / "a.dds", bytes_of("dds"));
    CHECK(!st::is_save_source(mod, st::plan_import(mod, "RMGE")));
}

int main() {
    const fs::path base = galaxy::test_temp_path("galaxy_save_transfer_tests");
    std::error_code error;
    fs::remove_all(base, error);
    fs::create_directories(base);
    test_aes();
    test_names();
    test_zip();
    test_single_files();
    test_sd_save();
    test_zip_sources();
    test_commit(base);
    test_export(base);
    test_folders(base);
    test_by_file(base);
    test_save_source(base);
    fs::remove_all(base, error);
    if (failures != 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "save_transfer tests passed\n";
    return 0;
}
