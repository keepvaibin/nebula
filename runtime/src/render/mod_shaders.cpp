// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/mod_shaders.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <memory>
#include <new>
#include <unordered_map>
#include <vector>

namespace galaxy::render {
namespace {

constexpr std::size_t kHeaderBytes = 56u;
constexpr std::size_t kEntryBytes = 32u;

struct Key {
    std::uint64_t id;
    std::uint32_t form;
    bool operator==(const Key&) const = default;
};
struct KeyHash {
    std::size_t operator()(const Key& key) const noexcept {
        return static_cast<std::size_t>(key.id ^ (std::uint64_t{key.form} * 0x9E3779B97F4A7C15ull));
    }
};

struct Registry {
    std::vector<std::unique_ptr<std::vector<std::uint64_t>>> files;  // words: SPIR-V and DXIL both align
    std::unordered_map<Key, ProgramBytes, KeyHash> programs;
    std::unordered_map<Key, ModProgramFlags, KeyHash> flags;  // form: 1 a pixel stage, 0 a vertex stage
};

Registry& registry() {
    static Registry r;
    return r;
}

Key key_of(std::uint64_t id, unsigned variant, bool pixel) noexcept {
    return {id, variant << 1u | (pixel ? 1u : 0u)};
}

}  // namespace

ModShadersResult load_mod_shaders(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary | std::ios::ate);
    if (!in) {
        return ModShadersResult::Unreadable;
    }
    const std::streamoff size = in.tellg();
    if (size < static_cast<std::streamoff>(kHeaderBytes)) {
        return ModShadersResult::Unreadable;
    }
    std::unique_ptr<std::vector<std::uint64_t>> words;
    try {
        words = std::make_unique<std::vector<std::uint64_t>>(static_cast<std::size_t>(size + 7) / 8u);
    } catch (const std::bad_alloc&) {
        return ModShadersResult::Unreadable;  // a file larger than the memory there is
    }
    in.seekg(0);
    const auto bytes = reinterpret_cast<const unsigned char*>(words->data());
    if (!in.read(reinterpret_cast<char*>(words->data()), size) || std::memcmp(bytes, "NBSHADER", 8) != 0) {
        return ModShadersResult::Unreadable;
    }
    std::uint32_t version = 0u;
    std::uint32_t count = 0u;
    std::memcpy(&version, bytes + 8, 4);
    std::memcpy(&count, bytes + 12, 4);
    const auto total = static_cast<std::uint64_t>(size);
    if (version != 3u || kHeaderBytes + std::uint64_t{count} * kEntryBytes > total) {
        return ModShadersResult::Unreadable;
    }
    if (std::memcmp(bytes + 16, program_interface(), 40) != 0) {
        return ModShadersResult::OtherInterface;
    }
    // Nothing of a file that is wrong anywhere is used.
    std::vector<std::pair<Key, ProgramBytes>> found;
    found.reserve(count);
    for (std::uint32_t i = 0u; i < count; ++i) {
        const unsigned char* entry = bytes + kHeaderBytes + i * kEntryBytes;
        std::uint64_t id = 0u;
        std::uint64_t offset = 0u;
        std::uint64_t length = 0u;
        std::memcpy(&id, entry, 8);
        std::memcpy(&offset, entry + 16, 8);
        std::memcpy(&length, entry + 24, 8);
        // A program starts on a word of 8 bytes (the writer pads to it) and is whole 32-bit words: SPIR-V is read as words.
        if (entry[8] >= kProgramStored || entry[9] > 1u || offset > total || length > total - offset || length == 0u ||
            (offset & 7u) != 0u || (length & 3u) != 0u) {
            return ModShadersResult::Unreadable;
        }
        found.emplace_back(key_of(id, entry[8], entry[9] != 0u), ProgramBytes{bytes + offset, static_cast<std::size_t>(length)});
    }
    Registry& r = registry();
    // The same text in two mods is the same program: the first stays.
    for (const auto& [key, program] : found) {
        r.programs.emplace(key, program);
    }
    r.files.push_back(std::move(words));
    return ModShadersResult::Loaded;
}


ProgramBytes mod_program_bytes(std::uint64_t id, unsigned variant, bool pixel) noexcept {
    const Registry& r = registry();
    const auto it = r.programs.find(key_of(id, variant, pixel));
    return it != r.programs.end() ? it->second : ProgramBytes{};
}

bool load_mod_program_flags(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return false;
    }
    Registry& r = registry();
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // v0123456789abcdef<TAB>flags<TAB>coords
        if (line.size() < 21u || (line[0] != 'v' && line[0] != 'p') || line[17] != '\t') {
            continue;
        }
        bool hex = true;
        for (std::size_t i = 1u; i < 17u; ++i) {
            const char c = line[i];
            hex = hex && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
        const char* const flags_text = line.c_str() + 18;
        char* flags_end = nullptr;
        const unsigned long flags = std::strtoul(flags_text, &flags_end, 10);
        if (!hex || flags_end == flags_text || *flags_end != '\t' || flags > 7u) {
            continue;
        }
        char* coords_end = nullptr;
        const unsigned long coords = std::strtoul(flags_end + 1, &coords_end, 10);
        if (coords_end == flags_end + 1 || *coords_end != '\0' || coords > 8u) {
            continue;
        }
        const std::uint64_t id = std::strtoull(line.substr(1, 16).c_str(), nullptr, 16);
        const bool pixel = line[0] == 'p';
        // The same text in two cooks is the same program: the first stays.
        r.flags.emplace(Key{id, pixel ? 1u : 0u},
                        ModProgramFlags{true, static_cast<std::uint32_t>(flags), static_cast<std::uint32_t>(coords)});
    }
    return true;
}

ModProgramFlags mod_program_flags(std::uint64_t id, bool pixel) noexcept {
    const Registry& r = registry();
    const auto it = r.flags.find(Key{id, pixel ? 1u : 0u});
    return it != r.flags.end() ? it->second : ModProgramFlags{};
}

}  // namespace galaxy::render
