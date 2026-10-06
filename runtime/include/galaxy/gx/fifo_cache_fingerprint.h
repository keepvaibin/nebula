#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace galaxy::gx {

// Process-local lookup filter only. A matching fingerprint MUST still pass
// the cache's size and exact FIFO byte comparison before a cache hit.
// Four independent word lanes avoid the byte-at-a-time serial multiply chain.
// memcpy permits unaligned input and every load stays inside the supplied span.
[[nodiscard]] inline std::uint64_t dependency_fifo_fingerprint(
    std::span<const std::byte> bytes) noexcept {
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t a = 1469598103934665603ull;
    std::uint64_t b = 0x9e3779b97f4a7c15ull;
    std::uint64_t c = 0xd6e8feb86659fd93ull;
    std::uint64_t d = 0xa0761d6478bd642full;
    std::size_t offset = 0u;
    while (bytes.size() - offset >= 32u) {
        std::uint64_t words[4];
        std::memcpy(words, bytes.data() + offset, sizeof(words));
        a = (a ^ words[0]) * prime;
        b = (b ^ words[1]) * prime;
        c = (c ^ words[2]) * prime;
        d = (d ^ words[3]) * prime;
        offset += 32u;
    }
    std::uint64_t hash = std::rotl(a, 1) ^ std::rotl(b, 17) ^
        std::rotl(c, 33) ^ std::rotl(d, 49);
    for (; offset < bytes.size(); ++offset) {
        hash = (hash ^ std::to_integer<std::uint8_t>(bytes[offset])) * prime;
    }
    hash = (hash ^ static_cast<std::uint64_t>(bytes.size())) * prime;
    hash ^= hash >> 32u;
    hash *= 0xd6e8feb86659fd93ull;
    return hash ^ (hash >> 32u);
}

} // namespace galaxy::gx
