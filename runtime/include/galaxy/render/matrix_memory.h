#pragma once

// The matrices and lights the game loads (shape records' matrices, materials' texture matrices and lights, light
// ops), in the sections the shaders read as float4 rows (requirement E-05 replaces the snapshot with a per-frame
// arena): position and texture matrices (64 rows: a matrix id is its first row), normal matrices (row = the
// position matrix's row; three rows each, x y z and an unused w), post-transform matrices (64 rows: ids 64-127) and
// eight lights. The lights are kept typed (light()); their section in the palette is written from them, 16 words a
// light (kLightColorWord, kLightAngleWord, kLightDistanceWord, kLightPositionWord, kLightDirectionWord).

#include <array>
#include <cstdint>

namespace galaxy::render {

// A light as the game loads it: position and direction (eye space), the angle and distance attenuation terms
// (a0-a2, k0-k2) and the colour (RGBA).
struct NativeLight {
    float position[3], direction[3];
    float angle[3], distance[3];
    std::uint8_t color[4];
};

// The colour as one RGBA8 word (R in the top byte), as the light section holds it.
[[nodiscard]] constexpr std::uint32_t light_color_word(const NativeLight& light) noexcept {
    return static_cast<std::uint32_t>(light.color[0]) << 24u | static_cast<std::uint32_t>(light.color[1]) << 16u |
           static_cast<std::uint32_t>(light.color[2]) << 8u | light.color[3];
}

class MatrixMemory {
public:
    static constexpr std::uint32_t kWords = 0x400u;
    // Sections in words, each a whole number of 32-word upload chunks from its start.
    static constexpr std::uint32_t kMatrixBase = 0x000u;
    static constexpr std::uint32_t kNormalBase = 0x100u;  // 36 rows (position rows 0-33)
    static constexpr std::uint32_t kPostBase = 0x1A0u;    // 64 rows
    static constexpr std::uint32_t kLightBase = 0x2A0u;   // 8 lights x 16 words
    static constexpr std::uint32_t kLightWords = 16u;
    // A light's words in its 16 (the first three unused).
    static constexpr std::uint32_t kLightColorWord = 3u;
    static constexpr std::uint32_t kLightAngleWord = 4u;
    static constexpr std::uint32_t kLightDistanceWord = 7u;
    static constexpr std::uint32_t kLightPositionWord = 10u;
    static constexpr std::uint32_t kLightDirectionWord = 13u;
    // The rows the shaders read (float4s): each row a draw reads is found through its row table (RenderCore's
    // matrix arena), so only rows that changed are uploaded.
    static constexpr std::uint32_t kPaletteWords = 0x0340u;  // a whole number of 256-byte blocks
    static constexpr std::uint32_t kRows = kPaletteWords / 4u;  // 208
    static constexpr std::uint32_t kRowTableRows = kRows / 4u;  // the table's float4s: one uint a row
    static constexpr std::uint64_t kLoadSignatureSeed = 0x6D6C6F6164736967ull;

    // `count` words from `base`; `source` names an indexed load's array slot and element (slot << 16 | element) in
    // the load signature, 0xFFFFFFFF a direct one. Words at or past kWords are ignored.
    void load(std::uint16_t base, const std::uint32_t* values, std::uint16_t count, std::uint32_t source = 0xFFFFFFFFu);

    // Light n (0-7) whole (a material's lights), its colour and attenuation terms, or its position and direction
    // (the light ops); each part's words are loaded into the light section as the part arrives.
    void load_light(unsigned n, const NativeLight& light);
    void load_light_attenuation(unsigned n, const std::uint8_t (&color)[4], const float (&angle)[3],
                                const float (&distance)[3]);
    void load_light_position(unsigned n, const float (&position)[3], const float (&direction)[3]);
    [[nodiscard]] const NativeLight& light(unsigned n) const noexcept { return lights_[n & 7u]; }
    [[nodiscard]] const std::array<NativeLight, 8>& lights() const noexcept { return lights_; }

    [[nodiscard]] std::uint32_t word(std::uint32_t address) const noexcept { return words_[address & (kWords - 1u)]; }
    [[nodiscard]] const std::uint32_t* raw() const noexcept { return words_.data(); }

    // Rows written since the consumer last took them (one bit a row), and the consumer taking them.
    using RowMask = std::array<std::uint64_t, (kRows + 63u) / 64u>;
    [[nodiscard]] const RowMask& dirty_rows() const noexcept { return dirty_rows_; }
    void clear_dirty_rows() noexcept { dirty_rows_ = {}; }

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
    std::array<NativeLight, 8> lights_{};
    RowMask dirty_rows_{};
    std::uint64_t load_signature_ = kLoadSignatureSeed;
    std::uint32_t writes_ = 0u;
    bool changed_ = true;
    bool track_loads_ = false;
};

}  // namespace galaxy::render
