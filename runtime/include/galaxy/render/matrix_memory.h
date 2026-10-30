#pragma once

// The matrices and lights the game loads (shape records' matrices, materials' texture matrices and lights, light
// ops), in the sections the shaders read as float4 rows (requirement E-05 replaces the snapshot with a per-frame
// arena): position and texture matrices (64 rows: a matrix id is its first row), normal matrices (row = the
// position matrix's row; three rows each, x y z and an unused w), post-transform matrices (64 rows: ids 64-127) and
// eight lights of 16 words (colour at 3, angle and distance terms at 4-9, position at 10-12, direction at 13-15).

#include <array>
#include <cstdint>

namespace galaxy::render {

class MatrixMemory {
public:
    static constexpr std::uint32_t kWords = 0x400u;
    // Sections in words, each a whole number of 32-word upload chunks from its start.
    static constexpr std::uint32_t kMatrixBase = 0x000u;
    static constexpr std::uint32_t kNormalBase = 0x100u;  // 36 rows (position rows 0-33)
    static constexpr std::uint32_t kPostBase = 0x1A0u;    // 64 rows
    static constexpr std::uint32_t kLightBase = 0x2A0u;   // 8 lights x 16 words
    static constexpr std::uint32_t kLightWords = 16u;
    // The palette the vertex shader reads, uploaded in 32-word chunks (a whole number of the float4s the shader
    // indexes, so no chunk boundary splits one).
    static constexpr std::uint32_t kPaletteWords = 0x0340u;  // a whole number of 256-byte blocks
    static constexpr std::uint32_t kPaletteChunkWords = 32u;
    static constexpr std::uint32_t kPaletteChunkCount = kPaletteWords / kPaletteChunkWords;  // 26
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

}  // namespace galaxy::render
