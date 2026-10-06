#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace galaxy::diagnostics {

enum class NativeThpBoundaryReason : std::uint8_t {
    ModeOff, MissingState, CompareLimit, WrapperMapping, Metadata,
    FrameRecord, VideoSize, PlaneMapping, SourceMapping, DecoderStatus,
    MissingOriginal, AdmittedCompare, AdmittedNative, Count
};
inline constexpr std::size_t kNativeThpBoundaryReasonCount =
    static_cast<std::size_t>(NativeThpBoundaryReason::Count);
inline constexpr std::array<const char*, kNativeThpBoundaryReasonCount>
    kNativeThpBoundaryReasonNames{
        "mode-off", "missing-state", "compare-limit", "wrapper-mapping",
        "metadata", "frame-record", "video-size", "plane-mapping",
        "source-mapping", "decoder-status", "missing-original",
        "admitted-compare", "admitted-native"};

// Values are snapshots of reads the existing native eligibility path already
// performed. A zero value without its checked-fields bit means not observed.
struct NativeThpBoundarySnapshot {
    NativeThpBoundaryReason reason{};
    std::uint64_t mode{};
    std::uint64_t vi{};
    std::uint32_t checked_fields{};
    std::uint32_t wrapper{}, payload{};
    std::uint32_t width{}, height{}, index{}, components{}, work{};
    std::uint32_t record{}, frame{}, valid{}, size{};
    std::array<std::uint32_t, 3> planes{};
    std::uint32_t plane_mapped_mask{};
    bool frame_mapped{}, source_mapped{};
    std::int32_t decoder_status{};
    bool original_present{};
};

// Owned only by the translated guest-execution thread. Shutdown reads it after
// that execution has unwound; no worker/callback writes, clocks or guest reads.
struct NativeThpBoundaryProbe {
    std::uint64_t attempts{};
    std::array<std::uint64_t, kNativeThpBoundaryReasonCount> reasons{};
    bool has_first{};
    NativeThpBoundarySnapshot first{};

    NativeThpBoundarySnapshot* begin(bool enabled, std::uint64_t mode,
        std::uint64_t vi, std::uint32_t wrapper, std::uint32_t payload) noexcept {
        if (!enabled) return nullptr;
        ++attempts;
        if (has_first) return nullptr;
        first.mode = mode;
        first.vi = vi;
        first.wrapper = wrapper;
        first.payload = payload;
        first.checked_fields = 1u;
        return &first;
    }
    void complete(bool enabled, NativeThpBoundaryReason reason) noexcept {
        if (!enabled) return;
        ++reasons[static_cast<std::size_t>(reason)];
        if (!has_first) {
            first.reason = reason;
            has_first = true;
        }
    }
};

} // namespace galaxy::diagnostics
