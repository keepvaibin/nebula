// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The game's movies, decoded off the game thread. The game names the frame it shows (its sound keeps the time) and
// draws its movie quad with the reserved texture ids NEBULA_TEXTURE_MOVIE_Y, _U and _V; the texture store shows that
// frame's planes there. The planes are JPEG's full-range BT.601 YCbCr, as the movie material converts them; each is a
// one-byte texture whose view repeats its value in every channel.
//
// The frame comes from one of two decoders, each on its own thread, ahead of the game, into upload buffers (the
// render thread only records copies):
// - the cooked movie (NMOV in the asset store, galaxy/movie_video.h's JPEG frames): every frame exactly the one the
//   game names, the render thread waiting for it when the decoder is behind;
// - an HD replacement video (MP4 and what else the platform decodes): <folder>/<name>.<ext> for the disc's
//   /MovieData/<name>.thp, from the folders configure() gives (a later folder wins), the frame of the same time at the
//   replacement's resolution, in place of the cooked one once its first frame is decoded.

#include "galaxy/render/gpu_texture_backend.h"

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

namespace galaxy::render {

// The movie frame a submission draws (the game's movie draw), carried with it to the render thread.
struct MovieFrameMark {
    static constexpr std::int32_t kNone = -1;     // no movie in this submission
    std::array<char, 64> name{};  // the disc path, e.g. /MovieData/PrologueA.thp
    std::int32_t frame = kNone;   // below 0: the movie is opening
    float frame_rate = 0.0f;
    const std::uint8_t* data = nullptr;  // the cooked movie, mapped for the session; null before the game opens it
    std::uint64_t size = 0;
    std::uint32_t width = 0;  // the Y plane; U and V are half each way
    std::uint32_t height = 0;
};

// A decoded frame: Y (width x height) and U, V (half each way), a byte per texel, in one upload buffer.
struct MovieFrame {
    std::int32_t frame = -1;
    std::int64_t time = 0;  // a replacement's presentation time, 100 ns units
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
// I420 (`y` rows; `u` and `v` rows at half size, `uv_pitch` apart) into the planes as convert_bgra_to_movie_planes
// writes them.
void convert_i420_to_movie_planes(const std::uint8_t* y, std::int32_t y_pitch, const std::uint8_t* u,
                                  const std::uint8_t* v, std::int32_t uv_pitch, std::uint32_t width,
                                  std::uint32_t height, const MovieYuvTransform& transform, std::uint8_t* out,
                                  const std::array<std::uint64_t, 3>& offsets,
                                  const std::array<std::uint32_t, 3>& pitches) noexcept;
// NV12 (`y` rows, then interleaved Cb Cr rows at half size) into the planes as convert_bgra_to_movie_planes writes them.
void convert_nv12_to_movie_planes(const std::uint8_t* y, std::int32_t y_pitch, const std::uint8_t* uv,
                                  std::int32_t uv_pitch, std::uint32_t width, std::uint32_t height,
                                  const MovieYuvTransform& transform, std::uint8_t* out,
                                  const std::array<std::uint64_t, 3>& offsets,
                                  const std::array<std::uint32_t, 3>& pitches) noexcept;
// The upload layout of the three planes; returns the total size.
std::uint64_t movie_plane_layout(std::uint32_t width, std::uint32_t height, std::array<std::uint64_t, 3>& offsets,
                                 std::array<std::uint32_t, 3>& pitches) noexcept;

// One cooked movie's frame table: each frame's video in the NMOV (crates/nebula-import/src/cook/thp.rs). Empty when the
// bytes are not a whole NMOV, or hold video this build cannot decode.
// The header's word at 0x24 is the codec: 0 the game's JPEG frames, 1 AV1 (a mod's movie; each frame one
// temporal unit of OBUs, frame 0 a key frame, 4:2:0 8-bit; only where the build has dav1d, NEBULA_AV1). The word at
// 0x28 is an AV1 movie's colour: 0 BT.601 full range (JPEG's), 1 BT.709 limited range.
enum class CookedMovieCodec : std::uint32_t { Jpeg = 0, Av1 = 1 };
struct CookedMovieIndex {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    CookedMovieCodec codec = CookedMovieCodec::Jpeg;
    bool bt709_limited = false;
    std::vector<std::uint64_t> video_at;   // per frame: its video's offset in the file
    std::vector<std::uint32_t> video_size;
};
// Whether this build decodes AV1 movies (built with dav1d).
[[nodiscard]] bool movie_av1_supported() noexcept;
[[nodiscard]] CookedMovieIndex index_cooked_movie(const std::uint8_t* data, std::uint64_t size);

class MoviePlayback {
public:
    MoviePlayback();
    MoviePlayback(const MoviePlayback&) = delete;
    MoviePlayback& operator=(const MoviePlayback&) = delete;
    ~MoviePlayback();

    // The folders searched for replacements (a later folder wins) and the upload buffers' maker (any thread).
    void configure(std::vector<std::filesystem::path> folders,
                   std::function<TextureBackend::UploadChunk(std::uint64_t)> stager);
    void shutdown() noexcept;

    // Render thread, each submission: the frame the mark names, or null (no movie, or no frame). Valid until the next
    // call.
    [[nodiscard]] const MovieFrame* frame(const MovieFrameMark& mark);

    // The replacement file for a disc movie path (none: empty).
    [[nodiscard]] std::filesystem::path find(const char* disc_path) const;

    struct Stats {
        std::uint64_t opened = 0;  // replacements
        std::uint64_t decoded = 0;
        std::uint64_t shown = 0;
        std::uint64_t late = 0;  // submissions that showed an older replacement frame as its decoder caught up
        std::uint64_t seeks = 0;
        double convert_ms = 0.0;
        std::uint64_t cooked_decoded = 0;  // the cooked movie's frames
        std::uint64_t cooked_shown = 0;
        std::uint64_t cooked_waits = 0;  // submissions whose frame the render thread waited for
        std::uint64_t cooked_seeks = 0;
        std::uint64_t cooked_failed = 0;  // frames whose data did not decode
        double cooked_decode_ms = 0.0;
        double cooked_wait_ms = 0.0;
    };
    [[nodiscard]] Stats stats() const;

private:
    class Decoder;
    class CookedDecoder;
    void close() noexcept;

    std::vector<std::filesystem::path> folders_;
    std::function<TextureBackend::UploadChunk(std::uint64_t)> stager_;
    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<CookedDecoder> cooked_;
    const std::uint8_t* cooked_data_ = nullptr;
    std::string current_name_;
    bool current_missing_ = false;  // no file for current_name_ (looked up once per opening)
    MovieFrame shown_;
    MovieFrame cooked_shown_;
    std::uint64_t shown_count_ = 0;
    std::uint64_t late_ = 0;
    std::uint64_t opened_ = 0;
    Stats retired_;  // the closed decoders' counts
};

}  // namespace galaxy::render
