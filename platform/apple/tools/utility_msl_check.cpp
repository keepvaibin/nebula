// SPDX-License-Identifier: GPL-3.0-only
// Build check: the texts utility_msl.bin carries (utility_msl.cpp wrote it; compile_metallib.py compiles it into
// utility.metallib) are the texts the Metal renderer makes (runtime/src/render/mtl/utility_msl.cpp renderer_msl_texts),
// each under the name the runtime asks for. Every runtime text is in the file with its own hash and bytes, the file holds
// no text the runtime does not make, and each hash in the file is its text's. A text that differs would be compiled on a
// player's machine from its text in place of being taken from the library.
//
//   utility_msl_check <utility_msl.bin> <stamp file written when it holds>
#include "galaxy/render/mtl/utility_msl.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::uint64_t read_le(const std::vector<std::uint8_t>& bytes, std::size_t at, unsigned size) {
    std::uint64_t value = 0u;
    for (unsigned i = 0u; i < size; ++i) {
        value |= std::uint64_t{bytes[at + i]} << (8u * i);
    }
    return value;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace galaxy::render::mtl;
    if (argc != 3) {
        std::fprintf(stderr, "usage: utility_msl_check <utility_msl.bin> <stamp file>\n");
        return 2;
    }
    std::vector<std::uint8_t> bytes;
    if (std::FILE* in = std::fopen(argv[1], "rb"); in != nullptr) {
        std::uint8_t chunk[65536];
        for (std::size_t got; (got = std::fread(chunk, 1u, sizeof(chunk), in)) != 0u;) {
            bytes.insert(bytes.end(), chunk, chunk + got);
        }
        std::fclose(in);
    }
    if (bytes.size() < 16u || std::memcmp(bytes.data(), "NBUMSL01", 8u) != 0) {
        std::fprintf(stderr, "utility_msl_check: %s is not a texts file\n", argv[1]);
        return 1;
    }
    const auto count = static_cast<std::uint32_t>(read_le(bytes, 8u, 4u));
    std::map<std::uint64_t, std::string_view> file;
    std::size_t at = 16u;
    unsigned faults = 0u;
    for (std::uint32_t i = 0u; i < count; ++i) {
        if (at + 16u > bytes.size()) {
            std::fprintf(stderr, "utility_msl_check: the file ends inside text %u\n", i);
            return 1;
        }
        const std::uint64_t hash = read_le(bytes, at, 8u);
        const auto size = static_cast<std::size_t>(read_le(bytes, at + 12u, 4u));
        at += 16u;
        if (at + size > bytes.size()) {
            std::fprintf(stderr, "utility_msl_check: the file ends inside text %u\n", i);
            return 1;
        }
        const std::string_view text(reinterpret_cast<const char*>(bytes.data() + at), size);
        at += size;
        if (utility_msl_hash(text) != hash) {
            std::fprintf(stderr, "utility_msl_check: text %u is filed as %016llx, its bytes hash to %016llx\n", i,
                         static_cast<unsigned long long>(hash), static_cast<unsigned long long>(utility_msl_hash(text)));
            ++faults;
        }
        if (!file.emplace(hash, text).second) {
            std::fprintf(stderr, "utility_msl_check: %016llx is filed twice\n", static_cast<unsigned long long>(hash));
            ++faults;
        }
    }
    if (at != bytes.size()) {
        std::fprintf(stderr, "utility_msl_check: %zu bytes after the last text\n", bytes.size() - at);
        ++faults;
    }
    std::map<std::uint64_t, bool> made;
    for (const char* const runtime : renderer_msl_texts()) {
        const std::string_view text(runtime);
        const std::uint64_t hash = utility_msl_hash(text);
        made[hash] = true;
        const auto found = file.find(hash);
        if (found == file.end()) {
            std::fprintf(stderr, "utility_msl_check: the runtime makes %s, which the file lacks\n", utility_function_name(text).text);
            ++faults;
        } else if (found->second != text) {
            std::fprintf(stderr, "utility_msl_check: %s's text in the file is not the runtime's\n", utility_function_name(text).text);
            ++faults;
        }
    }
    for (const auto& [hash, text] : file) {
        if (made.find(hash) == made.end()) {
            std::fprintf(stderr, "utility_msl_check: the file holds u%016llx::m, which the runtime never makes\n",
                         static_cast<unsigned long long>(hash));
            ++faults;
        }
    }
    if (faults != 0u) {
        std::fprintf(stderr, "utility_msl_check: %u fault(s)\n", faults);
        return 1;
    }
    std::FILE* stamp = std::fopen(argv[2], "wb");
    if (stamp == nullptr) {
        return 2;
    }
    std::fprintf(stamp, "%u texts\n", count);
    std::fclose(stamp);
    std::printf("utility_msl_check: %u texts, each the runtime's under its name in utility.metallib\n", count);
    return 0;
}
