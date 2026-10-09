#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace galaxy::thp {
// Owned by the simulation thread. A lease detaches scratch from the pool, so a
// nested translated fallback/compare cannot overwrite an outer decode's planes.
// Padding is reset just as it was with newly allocated zero-initialized vectors.
class VideoScratch final {
public:
    static constexpr std::size_t kMaxRetainedPlaneBytes = 1024u * 1024u;
    class Lease final {
    public:
        explicit Lease(VideoScratch& owner) noexcept : owner_(owner) {
            planes.swap(owner_.planes_);
        }
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        ~Lease() noexcept {
            for (std::size_t i = 0; i < planes.size(); ++i) {
                if (planes[i].capacity() <= kMaxRetainedPlaneBytes &&
                    planes[i].capacity() > owner_.planes_[i].capacity()) {
                    planes[i].swap(owner_.planes_[i]);
                }
            }
        }
        void resize_zeroed(std::size_t index, std::size_t size) {
            auto& plane = planes[index];
            const auto retained_size = std::min(plane.size(), size);
            plane.resize(size);
            // resize already value-initializes a newly grown suffix.
            std::fill_n(plane.begin(), retained_size, std::uint8_t{});
        }
        std::array<std::vector<std::uint8_t>, 3> planes;
    private:
        VideoScratch& owner_;
    };
private:
    std::array<std::vector<std::uint8_t>, 3> planes_;
};

// Bounded native THP subsystem boundary. Outputs use GX I8 8x4 tiling.
// Adapted from Aurora MIT THPDec.cpp; see implementation for provenance.
std::int32_t decode_video(std::span<const std::uint8_t> input,
    std::uint16_t width, std::uint16_t height,
    std::span<std::uint8_t> y, std::span<std::uint8_t> u,
    std::span<std::uint8_t> v);
}
