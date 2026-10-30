// The matrices and lights the game loads (matrix_memory.h).

#include "galaxy/render/matrix_memory.h"

#include <bit>
#include <cstddef>

namespace galaxy::render {

namespace {

template <std::size_t N>
void float_words(const float (&values)[N], std::uint32_t (&out)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        out[i] = std::bit_cast<std::uint32_t>(values[i]);
    }
}

std::uint16_t light_word(unsigned n, std::uint32_t word) noexcept {
    return static_cast<std::uint16_t>(MatrixMemory::kLightBase + MatrixMemory::kLightWords * (n & 7u) + word);
}

}  // namespace

void MatrixMemory::load_light(unsigned n, const NativeLight& light) {
    lights_[n & 7u] = light;
    std::uint32_t position[3], terms[6], direction[3];
    float_words(light.position, position);
    float_words(light.direction, direction);
    for (unsigned i = 0; i < 3u; ++i) {
        terms[i] = std::bit_cast<std::uint32_t>(light.angle[i]);
        terms[3u + i] = std::bit_cast<std::uint32_t>(light.distance[i]);
    }
    const std::uint32_t color = light_color_word(light);
    load(light_word(n, kLightPositionWord), position, 3u);
    load(light_word(n, kLightAngleWord), terms, 6u);
    load(light_word(n, kLightColorWord), &color, 1u);
    load(light_word(n, kLightDirectionWord), direction, 3u);
}

void MatrixMemory::load_light_attenuation(unsigned n, const std::uint8_t (&color)[4], const float (&angle)[3],
                                          const float (&distance)[3]) {
    NativeLight& light = lights_[n & 7u];
    std::uint32_t terms[6];
    for (unsigned i = 0; i < 3u; ++i) {
        light.angle[i] = angle[i];
        light.distance[i] = distance[i];
        terms[i] = std::bit_cast<std::uint32_t>(angle[i]);
        terms[3u + i] = std::bit_cast<std::uint32_t>(distance[i]);
    }
    for (unsigned i = 0; i < 4u; ++i) {
        light.color[i] = color[i];
    }
    const std::uint32_t head[4] = {0u, 0u, 0u, light_color_word(light)};
    load(light_word(n, 0u), head, 4u);
    load(light_word(n, kLightAngleWord), terms, 6u);
}

void MatrixMemory::load_light_position(unsigned n, const float (&position)[3], const float (&direction)[3]) {
    NativeLight& light = lights_[n & 7u];
    std::uint32_t words[6];
    for (unsigned i = 0; i < 3u; ++i) {
        light.position[i] = position[i];
        light.direction[i] = direction[i];
        words[i] = std::bit_cast<std::uint32_t>(position[i]);
        words[3u + i] = std::bit_cast<std::uint32_t>(direction[i]);
    }
    load(light_word(n, kLightPositionWord), words, 6u);
}

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
    const std::uint32_t last = (static_cast<std::uint32_t>(base) + count - 1u) / 4u;
    for (std::uint32_t row = base / 4u; row <= last && row < kRows; ++row) {
        dirty_rows_[row / 64u] |= std::uint64_t{1} << (row % 64u);
    }
}

}  // namespace galaxy::render
