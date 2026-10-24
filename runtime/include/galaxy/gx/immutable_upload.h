#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace galaxy::gx {

// Where one immutable decoded payload was last uploaded, per upload-ring frame slot.
// UploadRing::upload_immutable skips the copy when the same ring places the payload at the
// same offset one segment generation later. One token per payload, never shared between cache
// entries; it does not replace guest dependency checks or the GPU fence.
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
