// The matrices and lights draws read (draw_matrices.h).

#include "galaxy/render/draw_matrices.h"

#include <bit>
#include <cstring>

namespace galaxy::render {

namespace {

// Each kind's first row and rows in the draw_rows numbering.
struct Range {
    std::uint32_t first, count;
};

constexpr Range range_of(DrawMatrices::Kind kind) noexcept {
    switch (kind) {
    case DrawMatrices::kPositions:
        return {draw_rows::kPosition, draw_rows::kPositionRows};
    case DrawMatrices::kTextures:
        return {draw_rows::kTexture, draw_rows::kTextureRows};
    case DrawMatrices::kNormals:
        return {draw_rows::kNormal, draw_rows::kNormalRows};
    case DrawMatrices::kPosts:
        return {draw_rows::kPost, draw_rows::kPostRows};
    default:
        return {draw_rows::kLight, draw_rows::kLightRows};
    }
}

}  // namespace

void light_words(const NativeLight& light, std::uint32_t* out) noexcept {
    std::memset(out, 0, draw_rows::kLightWords * sizeof(std::uint32_t));
    out[draw_rows::kLightColorWord] = light_color_word(light);
    for (unsigned i = 0; i < 3u; ++i) {
        out[draw_rows::kLightAngleWord + i] = std::bit_cast<std::uint32_t>(light.angle[i]);
        out[draw_rows::kLightDistanceWord + i] = std::bit_cast<std::uint32_t>(light.distance[i]);
        out[draw_rows::kLightPositionWord + i] = std::bit_cast<std::uint32_t>(light.position[i]);
        out[draw_rows::kLightDirectionWord + i] = std::bit_cast<std::uint32_t>(light.direction[i]);
    }
}

void DrawMatrices::write(Kind kind, std::uint32_t first, const float* values, std::uint32_t count,
                         std::uint32_t width, std::uint32_t source) {
    ++writes_;
    if (track_loads_) [[unlikely]] {
        std::uint64_t h = load_signature_ ^ ((static_cast<std::uint64_t>(source) << 32u) |
                                             (static_cast<std::uint64_t>(kind) << 16u) | first);
        h *= 0x9E3779B97F4A7C15ull;
        load_signature_ = h ^ (h >> 29u);
    }
    bool changed = false;
    for (std::uint32_t r = 0; r < count; ++r) {
        Row next{};
        for (std::uint32_t c = 0; c < width; ++c) {
            next[c] = std::bit_cast<std::uint32_t>(values[r * width + c]);
        }
        Row& row = rows_[first + r];
        if (row != next) {
            row = next;
            changed = true;
        }
    }
    if (changed) {
        changed_ |= kind;
        any_changed_ = true;
    }
}

void DrawMatrices::set_position(unsigned place, const float* values, std::uint16_t model_matrix) {
    write(kPositions, draw_rows::kPosition + 3u * (place % 10u), values, 3u, 4u, 0x10000u | model_matrix);
}

void DrawMatrices::set_normal(unsigned place, const float* values, std::uint16_t model_matrix) {
    write(kNormals, draw_rows::kNormal + 3u * (place % 10u), values, 3u, 3u, 0x20000u | model_matrix);
}

void DrawMatrices::set_texture(unsigned place, const float* values, unsigned rows) {
    write(kTextures, draw_rows::kTexture + 3u * (place > 10u ? 10u : place), values, rows == 2u ? 2u : 3u, 4u,
          0x30000u);
}

void DrawMatrices::set_post(unsigned place, const float* values) {
    write(kPosts, draw_rows::kPost + (place >= 20u ? 61u : 3u * place), values, 3u, 4u, 0x40000u);
}

void DrawMatrices::set_material_texture(unsigned id, const float* values, unsigned rows) {
    if (id >= 64u) {
        write(kPosts, draw_rows::kPost + (id - 64u > 61u ? 61u : id - 64u), values, rows == 2u ? 2u : 3u, 4u,
              0x40000u);
    } else if (id >= 30u) {
        write(kTextures, id, values, rows == 2u ? 2u : 3u, 4u, 0x30000u);
    } else {
        write(kPositions, id, values, rows == 2u ? 2u : 3u, 4u, 0x30000u);
    }
}

void DrawMatrices::write_light(unsigned n) {
    std::uint32_t words[draw_rows::kLightWords];
    light_words(lights_[n], words);
    ++writes_;
    bool changed = false;
    for (unsigned r = 0; r < 4u; ++r) {
        Row next{words[4u * r], words[4u * r + 1u], words[4u * r + 2u], words[4u * r + 3u]};
        Row& row = rows_[draw_rows::kLight + 4u * n + r];
        if (row != next) {
            row = next;
            changed = true;
        }
    }
    if (changed) {
        changed_ |= kLights;
        any_changed_ = true;
    }
}

void DrawMatrices::set_light(unsigned n, const NativeLight& light) {
    lights_[n & 7u] = light;
    write_light(n & 7u);
}

void DrawMatrices::set_light_attenuation(unsigned n, const std::uint8_t (&color)[4], const float (&angle)[3],
                                         const float (&distance)[3]) {
    NativeLight& light = lights_[n & 7u];
    for (unsigned i = 0; i < 3u; ++i) {
        light.angle[i] = angle[i];
        light.distance[i] = distance[i];
    }
    for (unsigned i = 0; i < 4u; ++i) {
        light.color[i] = color[i];
    }
    write_light(n & 7u);
}

void DrawMatrices::set_light_position(unsigned n, const float (&position)[3], const float (&direction)[3]) {
    NativeLight& light = lights_[n & 7u];
    for (unsigned i = 0; i < 3u; ++i) {
        light.position[i] = position[i];
        light.direction[i] = direction[i];
    }
    write_light(n & 7u);
}

const DrawMatrices::Row& DrawMatrices::row(std::uint32_t r) const noexcept {
    return rows_[r < draw_rows::kRows ? r : 0u];
}

float DrawMatrices::matrix_value(std::uint32_t r, std::uint32_t column) const noexcept {
    return std::bit_cast<float>(rows_[r & 63u][column & 3u]);
}

const DrawMatrices::Row* DrawMatrices::rows(Kind kind) const noexcept {
    return rows_.data() + range_of(kind).first;
}

std::uint32_t DrawMatrices::row_count(Kind kind) noexcept {
    return range_of(kind).count;
}

}  // namespace galaxy::render
