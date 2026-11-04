#pragma once

// Pipeline keys: a native material's program pair (its vertex and pixel shader hashes) with the draw's render state,
// and the hashing the shader caches persist.

#include "galaxy/render/render_state_key.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace galaxy::render {

// Every key type is padding-free (asserted), so comparing bytes is exact member-wise equality.
[[nodiscard]] inline bool key_bytes_equal(
    const void* left,
    const void* right,
    std::size_t size) noexcept {
    return std::memcmp(left, right, size) == 0;
}


// 64-bit FNV-1a. The shader key hashes and shader checksums in the on-disk shader and PSO
// caches use it, so changing it invalidates those caches.
[[nodiscard]] inline std::uint64_t fnv1a64(
    const void* data,
    std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

// SplitMix64 finalizer, for the in-memory PsoKey hash, which is never persisted. A PsoKey holds
// already-mixed hashes, so folding its words is cheaper than a byte-wise FNV walk.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

[[nodiscard]] constexpr std::uint64_t combine64(
    std::uint64_t hash,
    std::uint64_t value) noexcept {
    return mix64(hash ^ mix64(value + 0x9E3779B97F4A7C15ull));
}

// 64-bit hash of a key whose size is a multiple of eight bytes, a word per step. The shader caches persist it.
[[nodiscard]] inline std::uint64_t hash_key_words(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = mix64(size);
    for (std::size_t i = 0; i < size; i += 8u) {
        std::uint64_t word;
        std::memcpy(&word, bytes + i, sizeof(word));
        hash = mix64(hash ^ word);
    }
    return hash;
}

// RenderStateKey::blend_bits targets beyond the scene target. Motion vectors (temporal scenes): the motion-vector target as
// render target 1. Reactive mask (temporal scenes): a blended draw also writes its alpha (max-blended, capped at 0.9)
// to an R8 render target 1. The effects layer: a reactive draw also blends its colour into render target 2 and the
// scene target's destination factor into render target 3.
inline constexpr std::uint32_t kRenderStateMotionTarget = 1u << 29u;
inline constexpr std::uint32_t kRenderStateReactiveTarget = 1u << 30u;
inline constexpr std::uint32_t kRenderStateLayerTarget = 1u << 23u;
// A blend under constant destination alpha 0 drawn single-source: the alpha channel's own blend writes the 0 (factors
// zero), the colour blends at the shader's alpha.
inline constexpr std::uint32_t kRenderStateZeroAlpha = 1u << 24u;

struct PsoKey {
    std::uint64_t vs_hash;
    std::uint64_t ps_hash;
    RenderStateKey render_state;

    // Compares exactly the fields hash() mixes (see RenderStateKey::operator==).
    [[nodiscard]] bool operator==(const PsoKey& other) const noexcept {
        return vs_hash == other.vs_hash &&
            ps_hash == other.ps_hash &&
            render_state == other.render_state;
    }
    // Folds the fields with mix64, leaving out `reserved`.
    [[nodiscard]] std::uint64_t hash() const {
        std::uint64_t hash = mix64(vs_hash);
        hash = combine64(hash, ps_hash);
        hash = combine64(hash, render_state.blend_bits);
        hash = combine64(hash, render_state.zmode_bits);
        hash = combine64(
            hash,
            static_cast<std::uint64_t>(render_state.cull) |
                (static_cast<std::uint64_t>(render_state.pixfmt) << 8) |
                (static_cast<std::uint64_t>(render_state.primitive_topology)
                 << 16) |
                (static_cast<std::uint64_t>(render_state.samples_log2) << 24));
        return hash;
    }
};
static_assert(sizeof(PsoKey) == 32);
static_assert(std::has_unique_object_representations_v<PsoKey>);

struct PsoKeyHasher {
    [[nodiscard]] std::size_t operator()(const PsoKey& key) const {
        return static_cast<std::size_t>(key.hash());
    }
};

// A PsoKey with its hash computed once. `key` must not change afterwards; RenderCore replaces the
// whole object instead.
}  // namespace galaxy::render
