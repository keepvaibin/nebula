// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A replacement movie's pictures as the system decodes them (movie_playback.h's HD replacement): one source for each
// platform behind one interface, so the decoder thread (movie_playback.cpp) is the same everywhere:
// - Windows: Media Foundation's source reader, on the GPU's video decoder where it has one for the format
//   (movie_source_win.cpp);
// - macOS and iOS: AVFoundation's asset reader, VideoToolbox (movie_source_apple.mm);
// - Android: the NDK's media extractor and MediaCodec into an image reader (movie_source_android.cpp);
// - Linux and other systems: GStreamer 1.x, opened at run time; without it the movie is named in the log and the
//   disc's plays (movie_source_gstreamer.cpp).
// Video only: the game's own sound keeps the movie's time. A source is used on one thread, the one that opened it.

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace galaxy::render {

// How a source's pictures are laid out.
enum class MoviePixels : std::uint8_t {
    Yuv420,  // Y; Cb and Cr at half size each way, chroma_step bytes between a row's samples (1 planar, 2 interleaved)
    Bgra,    // 4 bytes a pixel
};

struct MovieSourceFormat {
    std::uint32_t width = 0;  // the picture shown (its display area); 0 until the first picture where not known before
    std::uint32_t height = 0;
    MoviePixels pixels = MoviePixels::Yuv420;
    bool bt709 = false;       // the YUV matrix: BT.709, else BT.601
    bool full_range = false;  // full-range YUV, else limited (16-235)
    double rate = 0.0;        // the file's own frame rate; 0 when it gives none (yet)
};

// One decoded picture: its presentation time and its planes at the shown area, valid until the source's next read or
// seek.
struct MovieSourcePicture {
    std::int64_t time = 0;                        // 100 ns units
    std::array<const std::uint8_t*, 3> planes{};  // Y, Cb, Cr; BGRA in planes[0]
    std::array<std::int32_t, 3> pitches{};        // bytes between rows; Cb's and Cr's are the same
    std::uint32_t chroma_step = 1;
};

class MovieSource {
public:
    enum class Read : std::uint8_t { Picture, End, Failed };

    MovieSource() = default;
    MovieSource(const MovieSource&) = delete;
    MovieSource& operator=(const MovieSource&) = delete;
    virtual ~MovieSource() = default;
    // The format of the last picture read (its rate and, where the container gives them, its size before the first).
    [[nodiscard]] virtual const MovieSourceFormat& format() const = 0;
    // The next picture in presentation order. Failed: why is in error().
    [[nodiscard]] virtual Read read(MovieSourcePicture& out) = 0;
    // To `time` (100 ns units, the pictures' own times): reads go on from the key frame at or before it, or from the
    // picture at it where the system decodes up to it (AVFoundation).
    virtual bool seek(std::int64_t time) = 0;
    [[nodiscard]] const std::string& error() const { return error_; }

protected:
    std::string error_;
};

// The system's decoder for `file`; null with `error` set when it cannot be opened or nothing decodes its video.
[[nodiscard]] std::unique_ptr<MovieSource> open_movie_source(const std::filesystem::path& file, std::string& error);

// What the first open would otherwise wait for, done ahead (any thread, once a session): the GPU video device on
// Windows, GStreamer's libraries and plugin registry on Linux; nothing elsewhere.
void prepare_movie_sources();

}  // namespace galaxy::render
