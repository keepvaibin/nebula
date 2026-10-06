#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

// Owned by one immutable decoded payload, never shared between cache entries.
// Replacing that payload must also replace this token. Buffer ownership and
// immediately consecutive segment generations establish byte validity; guest
// dependency validation and the renderer's GPU fence remain mandatory.
struct ImmutableUploadToken {
    struct Slot {
        std::uint64_t resource_identity = 0;
        std::uint64_t generation = 0;
        std::size_t offset = 0;
        std::size_t size = 0;
    };
    std::vector<Slot> slots;
};

}  // namespace galaxy::gx
