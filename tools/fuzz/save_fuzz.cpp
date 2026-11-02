// P-04: the save format's check on any bytes (libFuzzer + AddressSanitizer). The input's size is the file's: it decides the Length check, so the seed's first bytes also pick one.
// clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address -I native/petari/include tools/fuzz/save_fuzz.cpp -o save_fuzz
#include "Game/System/SaveFileFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    // The header's size made to agree with the file, so most runs pass the Length check and reach the table.
    std::vector<std::uint8_t> file(data, data + size);
    if (file.size() >= 32u) {
        file.resize(file.size() & ~std::size_t(31u));
        const std::uint32_t bytes = static_cast<std::uint32_t>(file.size());
        std::memcpy(file.data() + 12, &bytes, sizeof(bytes));
        const std::uint32_t version = 3u;
        std::memcpy(file.data() + 4, &version, sizeof(version));
    }
    NebulaSaveFormat::Header header;
    (void)NebulaSaveFormat::validate(file.data(), file.size(), header);
    return 0;
}
