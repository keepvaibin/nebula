// M-001: the guard's reading of a mod library's unwind tables on any bytes (libFuzzer + AddressSanitizer). A table
// that lies must give 0, never a read outside its bytes.
// clang++ -std=c++20 -g -O1 -fsanitize=fuzzer,address -I native/host tools/fuzz/luma_unwind_fuzz.cpp -o luma_unwind_fuzz
#include "luma_guard_unwind.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 9u) {
        return 0;
    }
    // The first 8 bytes choose where the reading starts (pc, entry); the rest are the table, in a block of exactly
    // their size.
    std::uint32_t pc = 0;
    std::uint32_t entry = 0;
    std::memcpy(&pc, data, 4u);
    std::memcpy(&entry, data + 4u, 4u);
    const std::size_t n = size - 8u;
    const auto bytes = std::make_unique<std::uint8_t[]>(n);
    std::memcpy(bytes.get(), data + 8u, n);
    constexpr std::uintptr_t kAt = 0x40000000u;
    // Kept, so the reads are not dropped as unused.
    static volatile std::uintptr_t sink = 0;
    sink = sink + galaxy::luma_guard::eh_frame_hdr_function(bytes.get(), n, kAt, kAt + pc);
    sink = sink + galaxy::luma_guard::pe_function_begin(bytes.get(), n, entry);
    sink = sink + static_cast<std::uintptr_t>(galaxy::luma_guard::pe_frame_register(bytes.get(), n, entry));
    return 0;
}
