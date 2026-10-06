#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

// Belongs to one immutable decoded payload and is never shared between cache
// entries; replacing the payload replaces the token. Buffer ownership and
// consecutive segment generations establish byte validity. Guest dependency
// validation and the renderer's GPU fence are still required.
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
