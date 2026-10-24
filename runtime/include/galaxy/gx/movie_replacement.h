// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// HD movie replacements: a video file (MP4 and what else the platform decodes) in place of one of the game's THP
// movies. The game still plays its THP (its timing, sound, subtitles and rumble follow it); each frame it draws,
// the texture cache shows the replacement's frame of the same time in place of the THP's Y, U and V planes, at the
// replacement's resolution. The planes are JPEG's full-range BT.601 YCbCr, as the game's TEV converts them; each is a
// one-byte texture whose view repeats its value in every channel, as GX expands an I8 texture.
//
// Files: <folder>/<name>.<ext> for the disc's /MovieData/<name>.thp, from the folders configure() gives (a later folder
// wins). Decoding runs on its own thread a few frames ahead into upload buffers; the render thread only records copies.

#include "galaxy/gx/texture_backend.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace galaxy::gx {

// The movie frame a submission draws (the game's THPGXYuv2RgbDraw), carried with it to the render thread.
struct MovieFrameMark {
    static constexpr std::int32_t kNone = -1;     // no movie in this submission
    std::array<char, 64> name{};  // the disc path, e.g. /MovieData/PrologueA.thp
    std::int32_t frame = kNone;
    float frame_rate = 0.0f;
    std::uint32_t y = 0;  // guest physical addresses of the I8 planes
    std::uint32_t u = 0;
    std::uint32_t v = 0;
    std::uint16_t width = 0;  // the Y plane; U and V are half each way
    std::uint16_t height = 0;
};

// A decoded frame: Y (width x height) and U, V (half each way), a byte per texel, in one upload buffer.
struct MovieReplacementFrame {
    std::int32_t frame = -1;
    std::int64_t time = 0;  // the file's presentation time, 100 ns units
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::shared_ptr<TextureBackend::UploadChunk> staging;
    std::array<std::uint64_t, 3> offsets{};
    std::array<std::uint32_t, 3> pitches{};
};

// Full-range BT.601 planes of a BGRA picture (tightly packed rows of `stride` bytes), a byte per texel at
// `offsets`/`pitches` (Y full size, U and V at half each way, rounding up).
void convert_bgra_to_movie_planes(const std::uint8_t* bgra, std::uint32_t width, std::uint32_t height,
                                  std::int32_t stride, std::uint8_t* out, const std::array<std::uint64_t, 3>& offsets,
                                  const std::array<std::uint32_t, 3>& pitches) noexcept;
// A source's YCbCr (its matrix, BT.601 or BT.709, and its range) re-encoded as JPEG's: Y' = ky(Y - y0) + kyb(Cb - 128)
// + kyr(Cr - 128), Cb' = 128 + kbb(Cb - 128) + kbr(Cr - 128), Cr' = 128 + krb(Cb - 128) + krr(Cr - 128), in 16.16 fixed
// point. Luma's chroma terms vanish for a BT.601 source; the chroma never takes luma (the RGB weights sum to one).
struct MovieYuvTransform {
    std::int32_t y0 = 0;
    std::int32_t ky = 65536;
    std::int32_t kyb = 0, kyr = 0;
    std::int32_t kbb = 65536, kbr = 0;
    std::int32_t krb = 0, krr = 65536;
};
[[nodiscard]] MovieYuvTransform movie_yuv_transform(bool bt709, bool full_range) noexcept;
// NV12 (`y` rows, then interleaved Cb Cr rows at half size) into the planes as convert_bgra_to_movie_planes writes them.
void convert_nv12_to_movie_planes(const std::uint8_t* y, std::int32_t y_pitch, const std::uint8_t* uv,
                                  std::int32_t uv_pitch, std::uint32_t width, std::uint32_t height,
                                  const MovieYuvTransform& transform, std::uint8_t* out,
                                  const std::array<std::uint64_t, 3>& offsets,
                                  const std::array<std::uint32_t, 3>& pitches) noexcept;
// The upload layout of the three planes; returns the total size.
std::uint64_t movie_plane_layout(std::uint32_t width, std::uint32_t height, std::array<std::uint64_t, 3>& offsets,
                                 std::array<std::uint32_t, 3>& pitches) noexcept;

class MovieReplacements {
public:
    MovieReplacements();
    MovieReplacements(const MovieReplacements&) = delete;
    MovieReplacements& operator=(const MovieReplacements&) = delete;
    ~MovieReplacements();

    // The folders searched for replacements (a later folder wins) and the upload buffers' maker (any thread).
    void configure(std::vector<std::filesystem::path> folders,
                   std::function<TextureBackend::UploadChunk(std::uint64_t)> stager);
    void shutdown() noexcept;

    // Render thread, each submission: the replacement frame for the game's movie frame, or null (the THP shows).
    // The frame stays valid until the next call.
    [[nodiscard]] const MovieReplacementFrame* frame(const MovieFrameMark& mark);

    // The replacement file for a disc movie path (none: empty).
    [[nodiscard]] std::filesystem::path find(const char* disc_path) const;

    struct Stats {
        std::uint64_t opened = 0;
        std::uint64_t decoded = 0;
        std::uint64_t shown = 0;
        std::uint64_t late = 0;  // submissions that showed an older frame (or the THP) as the decoder caught up
        std::uint64_t seeks = 0;
        double convert_ms = 0.0;
    };
    [[nodiscard]] Stats stats() const;
    // Any thread: a replacement's frames are showing for the open movie (the THP's own decode is then unseen).
    [[nodiscard]] static bool showing() noexcept;

private:
    class Decoder;
    void close() noexcept;

    std::vector<std::filesystem::path> folders_;
    std::function<TextureBackend::UploadChunk(std::uint64_t)> stager_;
    std::unique_ptr<Decoder> decoder_;
    std::string current_name_;
    bool current_missing_ = false;  // no file for current_name_ (looked up once per opening)
    MovieReplacementFrame shown_;
    std::uint64_t shown_count_ = 0;
    std::uint64_t late_ = 0;
    std::uint64_t opened_ = 0;
};

}  // namespace galaxy::gx
