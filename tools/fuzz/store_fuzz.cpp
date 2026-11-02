// P-04: the host's store index on any bytes (libFuzzer + AddressSanitizer). A file whose index lies must be refused or answered inside its bounds.
// clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address -I runtime/include tools/fuzz/store_fuzz.cpp runtime/src/disc_files.cpp -o store_fuzz
#include "galaxy/disc_files.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // The magic is given, so the runs reach the index.
    std::vector<std::uint8_t> image(size + 8u);
    std::memcpy(image.data(), galaxy::disc::kStoreMagic, 8u);
    if (size != 0u) {
        std::memcpy(image.data() + 8u, data, size);
    }
    galaxy::disc::AssetStore store;
    if (!store.load(image.data(), image.size())) {
        return 0;
    }
    (void)store.game_id();
    (void)store.language_folders();
    std::array<std::uint8_t, 64> out{};
    std::size_t names = 0u;
    for (std::int32_t index = -1; index < 64; ++index) {
        if (const auto item = store.entry(index)) {
            names += std::strlen(item->name);
            if (item->length <= out.size()) {
                (void)store.read(item->position, static_cast<std::size_t>(item->length), out.data());
            }
            for (std::uint32_t c = 0u; c < 8u; ++c) {
                (void)store.child(index, c);
            }
        }
    }
    for (const char* const path : {"", "/", "a", "a/b", "AudioRes/SMR.szs", "../x", "//", "a//b"}) {
        (void)store.find(path);
    }
    return static_cast<int>(names & 0u);
}
