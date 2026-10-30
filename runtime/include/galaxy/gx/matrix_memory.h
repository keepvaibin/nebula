#pragma once

// The matrices and lights the game loads (shape records' matrices, materials' texture matrices and lights, light
// ops), kept as the vertex shader's palette reads them: 0x000-0x0FF position and texture matrices (64 rows of four
// words), 0x400-0x45F normal matrices, 0x500-0x5FF post-transform matrices, 0x600-0x67F eight lights of 16 words
// (requirement E-05 replaces this with a per-frame arena).

#include <array>
#include <cstdint>

namespace galaxy::gx {

class MatrixMemory {
public:
    static constexpr std::uint32_t kWords = 0x1000u;
    // The palette the vertex shader reads: words 0x000-0x67F, uploaded in 32-word chunks (a whole number of the
    // float4s the shader indexes, so no chunk boundary splits one).
    static constexpr std::uint32_t kPaletteWords = 0x0680u;
    static constexpr std::uint32_t kPaletteChunkWords = 32u;
    static constexpr std::uint32_t kPaletteChunkCount = kPaletteWords / kPaletteChunkWords;  // 52
    static constexpr std::uint64_t kLoadSignatureSeed = 0x6D6C6F6164736967ull;

    // `count` words from `base`; `source` names an indexed load's array slot and element (slot << 16 | element) in
    // the load signature, 0xFFFFFFFF a direct one. Words at or past kWords are ignored.
    void load(std::uint16_t base, const std::uint32_t* values, std::uint16_t count, std::uint32_t source = 0xFFFFFFFFu);

    [[nodiscard]] std::uint32_t word(std::uint32_t address) const noexcept { return words_[address & (kWords - 1u)]; }
    [[nodiscard]] const std::uint32_t* raw() const noexcept { return words_.data(); }

    // Palette chunks written since the consumer's last snapshot (one bit each), and the consumer clearing them.
    [[nodiscard]] std::uint64_t palette_dirty() const noexcept { return palette_dirty_; }
    static constexpr void clear_palette_bits(std::uint64_t& mask, std::uint32_t first, std::uint32_t last) noexcept {
        const std::uint64_t span = last + 1u >= 64u ? ~std::uint64_t{0} : ((std::uint64_t{1} << (last + 1u)) - 1u);
        const std::uint64_t below = first >= 64u ? ~std::uint64_t{0} : ((std::uint64_t{1} << first) - 1u);
        mask &= ~(span & ~below);
    }
    void clear_palette_dirty(std::uint32_t first, std::uint32_t last) noexcept {
        clear_palette_bits(palette_dirty_, first, last);
    }

    // Whether a word changed since the last call (all at the start and after mark_changed).
    [[nodiscard]] bool consume_changed() noexcept {
        const bool c = changed_;
        changed_ = false;
        return c;
    }
    void mark_changed() noexcept { changed_ = true; }

    // Loads made, changed or not: the same object loads the same sequence every frame, so a count since its
    // draw-identity marker names each of its transform states.
    [[nodiscard]] std::uint32_t write_count() const noexcept { return writes_; }
    // A hash of the loads' destinations (and indexed loads' sources) since the last reset, while tracking is on
    // (draw-identity scenes): two records with the same count but other loads differ here.
    void set_load_tracking(bool on) noexcept { track_loads_ = on; }
    void reset_load_signature() noexcept { load_signature_ = kLoadSignatureSeed; }
    [[nodiscard]] std::uint64_t load_signature() const noexcept { return load_signature_; }

private:
    std::array<std::uint32_t, kWords> words_{};
    std::uint64_t palette_dirty_ = 0u;
    std::uint64_t load_signature_ = kLoadSignatureSeed;
    std::uint32_t writes_ = 0u;
    bool changed_ = true;
    bool track_loads_ = false;
};

}  // namespace galaxy::gx
