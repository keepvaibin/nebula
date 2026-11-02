// What the game asked for and the player did not get: a draw with no material or pipeline, a texture drawn blank, a sound read
// past its memory. Never silent: each kind is logged the first time, every count at exit, and tools/verify/losscheck.py fails a
// run where any is above zero.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace galaxy::loss {

enum class Kind : unsigned { DrawNoMaterial, DrawNoPipeline, BlankTexture, SoundRead, Count };

inline std::array<std::atomic<std::uint64_t>, static_cast<unsigned>(Kind::Count)> counters{};

[[nodiscard]] inline const char* name(Kind kind) noexcept {
    switch (kind) {
    case Kind::DrawNoMaterial: return "draws-no-material";
    case Kind::DrawNoPipeline: return "draws-no-pipeline";
    case Kind::BlankTexture: return "textures-blank";
    case Kind::SoundRead: return "sound-reads-missing";
    default: return "?";
    }
}

inline void note(Kind kind) noexcept {
    if (counters[static_cast<unsigned>(kind)].fetch_add(1u, std::memory_order_relaxed) == 0u) {
        std::fprintf(stderr, "[loss] first %s\n", name(kind));
    }
}

// A blank texture whose picture has come since (a capture read the frame before its first capture): not lost.
inline void filled(Kind kind) noexcept {
    counters[static_cast<unsigned>(kind)].fetch_sub(1u, std::memory_order_relaxed);
}

inline void report() noexcept {
    std::fprintf(stderr, "[loss]");
    for (unsigned i = 0; i < counters.size(); ++i) {
        std::fprintf(stderr, " %s=%llu", name(static_cast<Kind>(i)),
                     static_cast<unsigned long long>(counters[i].load(std::memory_order_relaxed)));
    }
    std::fprintf(stderr, "\n");
}

}  // namespace galaxy::loss
