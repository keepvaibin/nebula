// The matrices and lights the game loads (matrix_memory.h).

#include "galaxy/gx/matrix_memory.h"

namespace galaxy::gx {

void MatrixMemory::load(std::uint16_t base, const std::uint32_t* values, std::uint16_t count, std::uint32_t source) {
    if (base >= kWords || count == 0u) {
        return;
    }
    if (static_cast<std::uint32_t>(base) + count > kWords) {
        count = static_cast<std::uint16_t>(kWords - base);
    }
    ++writes_;
    if (track_loads_) [[unlikely]] {
        std::uint64_t h = load_signature_ ^ ((static_cast<std::uint64_t>(source) << 32u) |
                                             (static_cast<std::uint64_t>(count) << 16u) | base);
        h *= 0x9E3779B97F4A7C15ull;
        load_signature_ = h ^ (h >> 29u);
    }
    // In order: `values` may alias the memory.
    bool changed = false;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint32_t address = static_cast<std::uint32_t>(base) + i;
        if (words_[address] != values[i]) {
            words_[address] = values[i];
            changed = true;
        }
    }
    if (!changed) {
        return;
    }
    changed_ = true;
    const std::uint32_t first = base / kPaletteChunkWords;
    const std::uint32_t last = (static_cast<std::uint32_t>(base) + count - 1u) / kPaletteChunkWords;
    if (first < kPaletteChunkCount) {
        const std::uint32_t end = last < kPaletteChunkCount ? last : kPaletteChunkCount - 1u;
        const std::uint64_t span = end + 1u >= 64u ? ~std::uint64_t{0} : ((std::uint64_t{1} << (end + 1u)) - 1u);
        palette_dirty_ |= span & ~((std::uint64_t{1} << first) - 1u);
    }
}

}  // namespace galaxy::gx
