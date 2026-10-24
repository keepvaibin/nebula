// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Per-scene transform history: one record per transform state uploaded
// inside the guest's 3D scene (GameScene::draw3D), matched to the previous
// scene's records so every draw knows its previous-frame transform. Motion
// vectors and the interpolated redraw read the matches.
//
// Identity comes from the game: draw-identity markers name the object that
// draws (J3D shape packet, NameObj, JPA emitter or particle) and a record's
// identity is that marker with the number of matrix loads since it. Records
// with an identity match the previous record of the same identity exactly;
// records without one match by render signature (vertex/pixel shader keys,
// position array base, vertex descriptor) and the nearest previous record by
// eye-space translation.
// No allocation in steady state; no Windows/D3D12 includes (unit-tested).

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

// Row-major 3x4 affine in GX layout: p' = M * [p, 1].
using Affine34 = std::array<float, 12>;

[[nodiscard]] Affine34 affine_identity() noexcept;
[[nodiscard]] Affine34 affine_multiply(const Affine34& a, const Affine34& b) noexcept;
// False (and identity) when the 3x3 part is singular.
[[nodiscard]] bool affine_inverse(const Affine34& m, Affine34& out) noexcept;
void affine_transform_point(const Affine34& m, const float p[3], float out[3]) noexcept;
// |v| without the squares overflowing (a GX light's direction can hold 1e31-scale values) or underflowing.
[[nodiscard]] float vector_length(const float v[3]) noexcept;
// The transform between a (t = 0) and b (t = 1): the pair's decomposition
// once, then any number of t (a sequence of sub-frames evaluates each pair
// several times). Each 3x3 part is polar-decomposed, M = R S (R a rotation,
// S the stretch: scale and shear, and the reflection of a mirrored matrix):
// R slerps, S and the translation lerp. Rigid, scaled and blended (envelope)
// matrices thus interpolate alike; a pair that does not decompose or differs
// in handedness lerps element-wise.
struct AffineMotion {
    Affine34 a{};
    Affine34 b{};
    float qa[4]{};          // rotation of a (x, y, z, w)
    float qb[4]{};          // rotation of b, on qa's hemisphere
    float stretch_a[9]{};   // S of a, row-major 3x3
    float stretch_b[9]{};
    float theta = 0.0f;     // slerp angle; 0 lerps the quaternions
    float inv_sin_theta = 0.0f;
    float cos_turn = 1.0f;  // |qa . qb| when both decompose, else 1
    bool rigid = false;     // both decomposed; false: element-wise lerp of a and b
};
void affine_motion(const Affine34& a, const Affine34& b, AffineMotion& out) noexcept;

[[nodiscard]] Affine34 affine_motion_at(const AffineMotion& m, float t) noexcept;
inline constexpr std::uint32_t kNoPaletteOffset = 0xFFFFFFFFu;

struct TransformRecord {
    std::uint64_t signature = 0;
    // Draw identity (0: none, signature matching).
    std::uint64_t identity = 0;
    // Vertex shader key hash (texgen and lighting setup): an identity match
    // must have the same one.
    std::uint64_t layout = 0;
    // Matching fingerprint: eye-space origin of the draw's object (inline
    // position matrix translation, or PNMTX0's for per-vertex palettes).
    float translation[3]{};
    // Inline position matrix (meaningful when !palette).
    Affine34 position{};
    // GX projection registers A..F and type, without the scene jitter.
    std::array<float, 7> projection{};
    // Palette snapshot, in float4 elements from the matrix ring base.
    std::uint32_t palette_offset = kNoPaletteOffset;
    bool palette = false;
    // Hash of the XF matrix-load destinations since the record's marker
    // (GxState::matrix_load_signature): an identity match must have the same
    // sequence of loads, not only the same count.
    std::uint64_t load_signature = 0;
    // Draw phase and category (galaxy/motion_signals.h encode_category).
    std::uint32_t category = 0;
    // A particle record carries its JPA age: an identity match must continue
    // it (galaxy::motion::particle_age_continues), else it is a new life.
    std::uint32_t continuity = 0;
    bool particle = false;
};

struct TransformHistoryStats {
    std::uint32_t records = 0;
    std::uint32_t matched = 0;
    std::uint32_t previous_records = 0;
    std::uint32_t identified = 0;        // records with an identity
    std::uint32_t identity_matched = 0;  // matched by identity
    std::uint32_t identity_rejected = 0; // same identity, other draw form
    std::uint32_t signature_rejected = 0;   // same identity, other matrix loads
    std::uint32_t continuity_rejected = 0;  // same particle key, a new life
    // Particle identity hits by age step: <0, 0, 1, 2, >2.
    std::array<std::uint32_t, 5> particle_age_steps{};
};

class TransformHistory {
public:
    // Capacity hint for the per-scene vectors (no reallocation below it).
    void reserve(std::size_t records);
    // Starts a scene recorded in renderer frame `frame`. The previous scene
    // (if any) becomes the match source. `view` is the game camera's
    // world-to-eye matrix, or null when unknown.
    void begin_scene(std::uint64_t frame, const Affine34* view);
    // Closes the current scene; a later begin_scene matches against it.
    void end_scene() noexcept;
    // Forgets every scene (scene cut, mode change).
    void reset() noexcept;

    // Appends a record to the open scene and returns its match in the
    // previous scene, or null. Records outside a scene are ignored.
    const TransformRecord* add(const TransformRecord& record);

    [[nodiscard]] bool has_previous() const noexcept { return previous_valid_; }
    [[nodiscard]] std::uint64_t previous_frame() const noexcept { return previous_frame_; }
    [[nodiscard]] std::uint64_t current_frame() const noexcept { return current_frame_; }
    // V_prev * inverse(V_cur): maps a current eye-space point of static
    // geometry to its previous eye-space position. Identity when unknown.
    [[nodiscard]] const Affine34& camera_delta() const noexcept { return camera_delta_; }
    [[nodiscard]] bool camera_known() const noexcept { return camera_known_; }
    [[nodiscard]] TransformHistoryStats stats() const noexcept;
    // Matched previous index per current record (-1 unmatched).
    [[nodiscard]] const std::vector<std::int32_t>& current_matches() const noexcept {
        return current_match_;
    }
    [[nodiscard]] const std::vector<TransformRecord>& current() const noexcept { return current_; }
    [[nodiscard]] const std::vector<TransformRecord>& previous() const noexcept { return previous_; }

private:
    void build_previous_index();
    void build_identity_index();
    [[nodiscard]] std::int32_t find_identity(std::uint64_t identity) const noexcept;
    // Previous records by identity (first record of each), open-addressed.
    std::vector<std::uint64_t> identity_keys_;
    std::vector<std::int32_t> identity_values_;
    std::size_t identity_mask_ = 0;
    bool previous_identified_ = false;
    std::uint32_t identified_ = 0;
    std::uint32_t identity_matched_ = 0;
    std::uint32_t identity_rejected_ = 0;
    std::uint32_t signature_rejected_ = 0;
    std::uint32_t continuity_rejected_ = 0;
    std::array<std::uint32_t, 5> particle_age_steps_{};

    std::vector<TransformRecord> current_;
    std::vector<TransformRecord> previous_;
    std::vector<std::int32_t> current_match_;
    // Previous records grouped by signature (draw order inside a group);
    // the open-addressed table maps a signature to its group.
    struct Group {
        std::uint64_t signature = 0;
        std::uint32_t begin = 0;
        std::uint32_t end = 0;
        std::uint32_t first_unclaimed = 0;
        bool used = false;
    };
    [[nodiscard]] std::size_t find_group(std::uint64_t signature) const noexcept;
    std::vector<std::int32_t> grouped_;         // previous indices by group
    std::vector<Group> table_;
    std::vector<std::uint8_t> claimed_;
    std::size_t table_mask_ = 0;
    std::uint64_t current_frame_ = 0;
    std::uint64_t previous_frame_ = 0;
    Affine34 current_view_{};
    Affine34 previous_view_{};
    Affine34 camera_delta_ = affine_identity();
    bool current_view_known_ = false;
    bool previous_view_known_ = false;
    bool camera_known_ = false;
    bool scene_open_ = false;
    bool current_valid_ = false;
    bool previous_valid_ = false;
    std::uint32_t matched_ = 0;
};

}  // namespace galaxy::gx
