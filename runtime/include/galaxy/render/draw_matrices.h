#pragma once

// The matrices and lights draws read, as the game's records give them (E-05): a draw's position and normal matrices
// by place (its record carries the ones its corners read), texture and post-transform matrices (from the material or
// draw record that gave each last) and the eight lights. Each kind is uploaded as a block of its own when it changed,
// and a draw names its blocks by their first rows (VertexConstants::pos_base, row_bases).
//
// The rows of a draw's matrices, in the numbering the shaders and the frame interpolation address them by (draw_rows):
// positions and textures 0-63 (position place k at 3k, texture place j at 30 + 3j, the identity's place at 60),
// normals from kNormal (place k at kNormal + 3k), post-transform matrices from kPost (place p at kPost + 3p, the
// identity's at kPost + 61), lights from kLight (light n at kLight + 4n: its colour in the first row's w, then the
// angle and distance terms, position and direction).

#include <array>
#include <cstdint>

namespace galaxy::render {

namespace draw_rows {
inline constexpr std::uint32_t kPosition = 0u;
inline constexpr std::uint32_t kTexture = 30u;
inline constexpr std::uint32_t kNormal = 64u;
inline constexpr std::uint32_t kPost = 104u;
inline constexpr std::uint32_t kLight = 168u;
inline constexpr std::uint32_t kRows = 208u;
// Rows of each kind's block.
inline constexpr std::uint32_t kPositionRows = kTexture - kPosition;
inline constexpr std::uint32_t kTextureRows = kNormal - kTexture;
inline constexpr std::uint32_t kNormalRows = 36u;
inline constexpr std::uint32_t kPostRows = kLight - kPost;
inline constexpr std::uint32_t kLightRows = kRows - kLight;
// A light's 16 words (4 rows): the colour, then the angle (3), distance (3), position (3) and direction (3) terms.
inline constexpr std::uint32_t kLightWords = 16u;
inline constexpr std::uint32_t kLightColorWord = 3u;
inline constexpr std::uint32_t kLightAngleWord = 4u;
inline constexpr std::uint32_t kLightDistanceWord = 7u;
inline constexpr std::uint32_t kLightPositionWord = 10u;
inline constexpr std::uint32_t kLightDirectionWord = 13u;
}  // namespace draw_rows

// A light as the game loads it: position and direction (eye space), the angle and distance attenuation terms
// (a0-a2, k0-k2) and the colour (RGBA).
struct NativeLight {
    float position[3], direction[3];
    float angle[3], distance[3];
    std::uint8_t color[4];
};

// The colour as one RGBA8 word (R in the top byte), as a light's rows hold it.
[[nodiscard]] constexpr std::uint32_t light_color_word(const NativeLight& light) noexcept {
    return static_cast<std::uint32_t>(light.color[0]) << 24u | static_cast<std::uint32_t>(light.color[1]) << 16u |
           static_cast<std::uint32_t>(light.color[2]) << 8u | light.color[3];
}

// A light's 16 words (draw_rows::kLightWords).
void light_words(const NativeLight& light, std::uint32_t* out) noexcept;

class DrawMatrices {
public:
    using Row = std::array<std::uint32_t, 4>;
    enum Kind : std::uint32_t { kPositions = 1u, kTextures = 2u, kNormals = 4u, kPosts = 8u, kLights = 16u };
    static constexpr std::uint64_t kLoadSignatureSeed = 0x6D6C6F6164736967ull;

    // A draw's position matrix at `place` (0-9: 12 values) and its normal matrix (9 values); `model_matrix` names a
    // model's draw matrix (index + 1) for the load signature, 0 another.
    void set_position(unsigned place, const float* values, std::uint16_t model_matrix);
    void set_normal(unsigned place, const float* values, std::uint16_t model_matrix);
    // Texture matrix `place` (0-9, 10 the identity's) of `rows` rows (2 or 3) and post-transform matrix `place`
    // (0-19, 20 the identity's).
    void set_texture(unsigned place, const float* values, unsigned rows);
    void set_post(unsigned place, const float* values);
    // A material's texture matrix by the program's matrix id (three apart: 30-60 textures, 64-125 post-transform).
    void set_material_texture(unsigned id, const float* values, unsigned rows);

    // Light n (0-7) whole (a material's lights), its colour and attenuation terms, or its position and direction
    // (the light ops).
    void set_light(unsigned n, const NativeLight& light);
    void set_light_attenuation(unsigned n, const std::uint8_t (&color)[4], const float (&angle)[3],
                               const float (&distance)[3]);
    void set_light_position(unsigned n, const float (&position)[3], const float (&direction)[3]);
    [[nodiscard]] const NativeLight& light(unsigned n) const noexcept { return lights_[n & 7u]; }
    [[nodiscard]] const std::array<NativeLight, 8>& lights() const noexcept { return lights_; }

    // Row r (draw_rows numbering) as words.
    [[nodiscard]] const Row& row(std::uint32_t r) const noexcept;
    // Value `column` of row `r` of the position and texture rows (0-63) as a float.
    [[nodiscard]] float matrix_value(std::uint32_t r, std::uint32_t column) const noexcept;
    // A kind's rows (its block as uploaded).
    [[nodiscard]] const Row* rows(Kind kind) const noexcept;
    [[nodiscard]] static std::uint32_t row_count(Kind kind) noexcept;

    // Kinds changed since the consumer last took them, and the consumer taking them.
    [[nodiscard]] std::uint32_t changed() const noexcept { return changed_; }
    void clear_changed() noexcept { changed_ = 0u; }
    void mark_all_changed() noexcept { changed_ = kPositions | kTextures | kNormals | kPosts | kLights; }
    // Whether a row changed since the last call (all at the start), for the drawing state's dirty flags.
    [[nodiscard]] bool consume_any_change() noexcept {
        const bool c = any_changed_;
        any_changed_ = false;
        return c;
    }

    // Loads made, changed or not: the same object loads the same sequence every frame, so a count since its
    // draw-identity marker names each of its transform states.
    [[nodiscard]] std::uint32_t write_count() const noexcept { return writes_; }
    // A hash of the loads' places (and model matrices) since the last reset, while tracking is on (draw-identity
    // scenes): two records with the same count but other loads differ here.
    void set_load_tracking(bool on) noexcept { track_loads_ = on; }
    void reset_load_signature() noexcept { load_signature_ = kLoadSignatureSeed; }
    [[nodiscard]] std::uint64_t load_signature() const noexcept { return load_signature_; }

private:
    // Writes `count` rows from `first` (a kind's rows); `values` 4 floats a row (`width` of them given, the rest 0).
    void write(Kind kind, std::uint32_t first, const float* values, std::uint32_t count, std::uint32_t width,
               std::uint32_t source);
    void write_light(unsigned n);

    std::array<Row, draw_rows::kRows> rows_{};
    std::array<NativeLight, 8> lights_{};
    std::uint64_t load_signature_ = kLoadSignatureSeed;
    std::uint32_t writes_ = 0u;
    std::uint32_t changed_ = kPositions | kTextures | kNormals | kPosts | kLights;
    bool any_changed_ = true;
    bool track_loads_ = false;
};

}  // namespace galaxy::render
