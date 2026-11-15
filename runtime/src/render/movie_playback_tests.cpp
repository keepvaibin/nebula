// SPDX-License-Identifier: GPL-3.0-only
// HD movie replacements: the planes' layout, the JPEG YCbCr of known colours and the file lookup.

#include "galaxy/render/movie_playback.h"
#include "galaxy/test_temp.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#endif

using namespace galaxy::render;

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

std::uint32_t texel(const std::vector<std::uint8_t>& out, std::uint64_t offset, std::uint32_t pitch, std::uint32_t x,
                    std::uint32_t y) {
    return out[offset + static_cast<std::uint64_t>(y) * pitch + x];
}

void test_colours() {
    // 4x2 BGRA: white, red, green, blue on the top row, the same below.
    const std::uint8_t colours[4][4] = {{255, 255, 255, 255}, {0, 0, 255, 255}, {0, 255, 0, 255}, {255, 0, 0, 255}};
    std::vector<std::uint8_t> bgra(4u * 2u * 4u);
    for (unsigned y = 0; y < 2u; ++y) {
        for (unsigned x = 0; x < 4u; ++x) std::memcpy(bgra.data() + (y * 4u + x) * 4u, colours[x], 4u);
    }
    std::array<std::uint64_t, 3> offsets{};
    std::array<std::uint32_t, 3> pitches{};
    const std::uint64_t size = movie_plane_layout(4u, 2u, offsets, pitches);
    expect(offsets[0] == 0u && pitches[0] == TextureBackend::kPitchAlignment &&
               offsets[1] % TextureBackend::kPlacementAlignment == 0u && offsets[2] > offsets[1] &&
               size == offsets[2] + pitches[2],
           "plane layout: aligned offsets and pitches, U and V half each way");
    std::vector<std::uint8_t> out(size, 0u);
    convert_bgra_to_movie_planes(bgra.data(), 4u, 2u, 16, out.data(), offsets, pitches);
    expect(texel(out, offsets[0], pitches[0], 0, 0) == 255u, "white: Y 255");
    expect(texel(out, offsets[0], pitches[0], 1, 1) == 76u, "red: Y 76");
    expect(texel(out, offsets[0], pitches[0], 2, 0) == 150u, "green: Y 150");
    expect(texel(out, offsets[0], pitches[0], 3, 0) == 29u, "blue: Y 29");
    // Chroma of white + red (2x2 block 0): mean R 255, G 127.5, B 127.5.
    // Cb = 128 - .168736*255 - .331264*127.5 + .5*127.5 = 106.8; Cr = 128 + .5*255 - .418688*127.5 - .081312*127.5 = 191.75
    const std::uint32_t cb0 = texel(out, offsets[1], pitches[1], 0, 0);
    const std::uint32_t cr0 = texel(out, offsets[2], pitches[2], 0, 0);
    expect(cb0 >= 106u && cb0 <= 107u && cr0 >= 191u && cr0 <= 192u, "white and red: Cb 107, Cr 192");
    // Green + blue (block 1): mean R 0, G 127.5, B 127.5: Cb = 128 - 42.2 + 63.75 = 149.5; Cr = 128 - 53.4 - 10.4 = 64.2
    const std::uint32_t cb1 = texel(out, offsets[1], pitches[1], 1, 0);
    const std::uint32_t cr1 = texel(out, offsets[2], pitches[2], 1, 0);
    expect(cb1 >= 149u && cb1 <= 150u && cr1 >= 64u && cr1 <= 65u, "green and blue: Cb 150, Cr 64");
    // A bottom-up picture (negative stride) gives the same planes.
    std::vector<std::uint8_t> flipped(out.size(), 0u);
    std::vector<std::uint8_t> bottom_up(bgra.size());
    std::memcpy(bottom_up.data(), bgra.data() + 16, 16);
    std::memcpy(bottom_up.data() + 16, bgra.data(), 16);
    convert_bgra_to_movie_planes(bottom_up.data() + 16, 4u, 2u, -16, flipped.data(), offsets, pitches);
    expect(flipped == out, "a negative stride reads rows upward");
}

void test_nv12() {
    const MovieYuvTransform same = movie_yuv_transform(false, true);
    expect(same.ky == 65536 && std::abs(same.kyb) <= 1 && std::abs(same.kyr) <= 1 && std::abs(same.kbb - 65536) <= 1 &&
               std::abs(same.kbr) <= 1 && std::abs(same.krb) <= 1 && std::abs(same.krr - 65536) <= 1,
           "a BT.601 full-range source passes through");
    // BT.709 limited: white (235, 128, 128) -> Y 255; black (16) -> 0; pure red (63, 102, 240) -> RGB (255, 0, 0)
    // whose JPEG YCbCr is (76, 85, 255).
    const MovieYuvTransform hd = movie_yuv_transform(true, false);
    const std::uint8_t luma[4] = {235, 16, 63, 63};
    const std::uint8_t chroma[4] = {128, 128, 102, 240};
    std::array<std::uint64_t, 3> offsets{};
    std::array<std::uint32_t, 3> pitches{};
    std::vector<std::uint8_t> out(movie_plane_layout(4u, 1u, offsets, pitches), 0u);
    convert_nv12_to_movie_planes(luma, 4, chroma, 4, 4u, 1u, hd, out.data(), offsets, pitches);
    const auto at = [&](unsigned plane, unsigned x) { return out[offsets[plane] + x]; };
    expect(at(0, 0) == 255u && at(0, 1) == 0u, "BT.709 limited white and black: Y 255 and 0");
    expect(std::abs(at(0, 2) - 76) <= 2 && std::abs(at(1, 1) - 85) <= 2 && at(2, 1) >= 253u,
           "BT.709 limited red: JPEG Y 76, Cb 85, Cr 255");

    // A picture tall enough to be split across threads: a full-range BT.601 source's planes are its own bytes.
    const std::uint32_t w = 302u;
    const std::uint32_t h = 401u;
    std::vector<std::uint8_t> y(static_cast<std::size_t>(w) * h);
    std::vector<std::uint8_t> uv(static_cast<std::size_t>(w + 1u) / 2u * 2u * ((h + 1u) / 2u));
    for (std::size_t i = 0; i < y.size(); ++i) y[i] = static_cast<std::uint8_t>(i * 7u + i / w);
    for (std::size_t i = 0; i < uv.size(); ++i) uv[i] = static_cast<std::uint8_t>(i * 13u + 5u);
    std::vector<std::uint8_t> planes(movie_plane_layout(w, h, offsets, pitches), 0u);
    const std::int32_t uv_pitch = static_cast<std::int32_t>((w + 1u) / 2u * 2u);
    convert_nv12_to_movie_planes(y.data(), static_cast<std::int32_t>(w), uv.data(), uv_pitch, w, h, same,
                                 planes.data(), offsets, pitches);
    bool equal = true;
    for (std::uint32_t row = 0; row < h; ++row) {
        for (std::uint32_t x = 0; x < w; ++x) {
            equal &= texel(planes, offsets[0], pitches[0], x, row) == y[static_cast<std::size_t>(row) * w + x];
        }
    }
    for (std::uint32_t row = 0; row < (h + 1u) / 2u; ++row) {
        for (std::uint32_t x = 0; x < (w + 1u) / 2u; ++x) {
            const std::size_t at_uv = static_cast<std::size_t>(row) * uv_pitch + x * 2u;
            equal &= texel(planes, offsets[1], pitches[1], x, row) == uv[at_uv] &&
                     texel(planes, offsets[2], pitches[2], x, row) == uv[at_uv + 1u];
        }
    }
    expect(equal, "a full-range BT.601 source split across threads: the planes are its bytes");
}

void test_find() {
    const std::filesystem::path root = galaxy::test_temp_path("nebula-movie-test");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "a");
    std::filesystem::create_directories(root / "b");
    std::ofstream(root / "a" / "PrologueA.mp4") << "x";
    std::ofstream(root / "b" / "prologuea.MKV") << "x";
    std::ofstream(root / "b" / "EndingA.txt") << "x";
    MoviePlayback movies;
    movies.configure({root / "a", root / "b"}, nullptr);
    const std::filesystem::path found = movies.find("/MovieData/PrologueA.thp");
    expect(found.parent_path() == root / "b", "a later folder wins, names and extensions in any case");
    expect(movies.find("/MovieData/EndingA.thp").empty(), "other extensions are not videos");
    expect(movies.find("/MovieData/PrologueB.thp").empty(), "no file: none");
    // Without a stager no decoder starts.
    MovieFrameMark mark{};
    const char name[] = "/MovieData/PrologueA.thp";
    std::memcpy(mark.name.data(), name, sizeof(name));
    mark.frame = 0;
    mark.frame_rate = 30.0f;
    expect(movies.frame(mark) == nullptr, "no upload buffers: no frame");
    movies.shutdown();
    std::filesystem::remove_all(root);
}

// A cooked movie of `frames` 16x16 frames, each one baseline JPEG of a constant picture (six blocks with zero DC and
// end of block: every texel 128), as the importer lays them out.
std::vector<std::uint8_t> cooked_movie(std::uint32_t frames) {
    std::vector<std::uint8_t> jpeg{0xFF, 0xD8};
    const auto segment = [&jpeg](std::uint8_t marker, std::vector<std::uint8_t> bytes) {
        const auto length = bytes.size() + 2;
        jpeg.insert(jpeg.end(), {0xFF, marker, static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length)});
        jpeg.insert(jpeg.end(), bytes.begin(), bytes.end());
    };
    std::vector<std::uint8_t> quant(65, 1);
    quant[0] = 0;
    segment(0xDB, quant);
    segment(0xC0, {8, 0, 16, 0, 16, 3, 1, 0x22, 0, 2, 0x11, 0, 3, 0x11, 0});
    for (const auto descriptor : {0, 0x10}) {
        std::vector<std::uint8_t> huffman(18, 0);
        huffman[0] = static_cast<std::uint8_t>(descriptor);
        huffman[1] = 1;
        segment(0xC4, huffman);
    }
    segment(0xDA, {3, 1, 0, 2, 0, 3, 0, 0, 63, 0});
    jpeg.insert(jpeg.end(), {0, 0});
    const auto padded = static_cast<std::uint32_t>((16 + jpeg.size() + 31) / 32 * 32);
    std::vector<std::uint8_t> out(0x40, 0);
    const auto put = [&out](std::size_t at, std::uint32_t value) { std::memcpy(out.data() + at, &value, 4); };
    std::memcpy(out.data(), "NMOV", 4);
    put(4, frames);
    const float rate = 30.0f;
    std::memcpy(out.data() + 8, &rate, 4);
    put(12, padded);
    put(16, padded);
    put(20, 16);
    put(24, 16);
    for (std::uint32_t f = 0; f < frames; ++f) {
        const std::size_t at = out.size();
        out.resize(at + padded, 0);
        put(at, f + 1 < frames ? padded : 0u);
        put(at + 4, static_cast<std::uint32_t>(jpeg.size()));
        std::memcpy(out.data() + at + 16, jpeg.data(), jpeg.size());
    }
    return out;
}

void test_index() {
    const std::vector<std::uint8_t> movie = cooked_movie(3);
    const CookedMovieIndex index = index_cooked_movie(movie.data(), movie.size());
    expect(index.width == 16u && index.height == 16u && index.video_at.size() == 3u, "a cooked movie's frame table");
    expect(index.video_at.size() == 3u && index.video_at[0] == 0x50u &&
               index.video_at[2] - index.video_at[1] == index.video_at[1] - index.video_at[0] &&
               index.video_size[0] == index.video_size[2],
           "frames follow the next-size chain");
    // Frames are padded to 32 bytes: a cut into the padding leaves every picture whole; a cut into the last one doesn't.
    expect(index_cooked_movie(movie.data(), movie.size() - 1).video_at.size() == 3u, "a movie cut in its padding keeps its table");
    expect(index_cooked_movie(movie.data(), index.video_at[2] + index.video_size[2] - 1u).video_at.empty(), "a cut-off movie has no table");
    std::vector<std::uint8_t> bad = movie;
    bad[0] = 'X';
    expect(index_cooked_movie(bad.data(), bad.size()).video_at.empty(), "not NMOV: no table");
    // The codec word (0x24) and an AV1 movie's colour word (0x28).
    expect(index.codec == CookedMovieCodec::Jpeg && !index.bt709_limited, "a JPEG movie's codec word is 0");
    std::vector<std::uint8_t> other = movie;
    other[0x24] = 2;
    expect(index_cooked_movie(other.data(), other.size()).video_at.empty(), "codec 2: no table");
    std::vector<std::uint8_t> av1 = movie;
    av1[0x24] = 1;
    av1[0x28] = 1;
    const CookedMovieIndex av1_index = index_cooked_movie(av1.data(), av1.size());
    expect(movie_av1_supported() ? av1_index.codec == CookedMovieCodec::Av1 && av1_index.bt709_limited &&
                                       av1_index.video_at.size() == 3u
                                 : av1_index.video_at.empty(),
           "an AV1 movie: a table only where the build decodes AV1");
    av1[0x28] = 2;
    expect(index_cooked_movie(av1.data(), av1.size()).video_at.empty(), "an AV1 movie's colour word past 1: no table");
    // I420 in JPEG's own YCbCr: the planes are its bytes.
    {
        std::uint8_t y[4 * 2] = {10, 20, 30, 40, 50, 60, 70, 80};
        const std::uint8_t u[2] = {100, 110};
        const std::uint8_t v[2] = {150, 160};
        std::array<std::uint64_t, 3> offsets{};
        std::array<std::uint32_t, 3> pitches{};
        std::vector<std::uint8_t> planes(movie_plane_layout(4, 2, offsets, pitches));
        convert_i420_to_movie_planes(y, 4, u, v, 2, 4, 2, movie_yuv_transform(false, true), planes.data(), offsets, pitches);
        expect(planes[offsets[0] + pitches[0] + 3] == 80 && planes[offsets[1] + 1] == 110 && planes[offsets[2]] == 150,
               "I420 in JPEG's YCbCr copies through");
    }

#if defined(_WIN32)
    // The cooked decoder through the playback: frames in turn, a repeat, a loop, a wrap past the end, a jump back.
    std::vector<std::unique_ptr<std::vector<std::uint8_t>>> buffers;
    std::mutex buffers_mutex;
    MoviePlayback playback;
    playback.configure({}, [&](std::uint64_t size) {
        std::lock_guard<std::mutex> lock(buffers_mutex);
        buffers.push_back(std::make_unique<std::vector<std::uint8_t>>(size));
        TextureBackend::UploadChunk chunk{};
        chunk.cpu_base = buffers.back()->data();
        chunk.size = size;
        return chunk;
    });
    MovieFrameMark mark{};
    const char name[] = "/MovieData/Test.thp";
    std::memcpy(mark.name.data(), name, sizeof(name));
    mark.frame_rate = 30.0f;
    mark.data = movie.data();
    mark.size = movie.size();
    mark.width = 16;
    mark.height = 16;
    mark.frame = -2;
    expect(playback.frame(mark) == nullptr, "opening: no frame yet");
    bool right = true;
    for (const std::int32_t wanted : {0, 1, 1, 2, 3, 5, 0, 2, 1}) {
        mark.frame = wanted;
        const MovieFrame* shown = playback.frame(mark);
        right &= shown != nullptr && shown->frame == wanted % 3 && shown->staging &&
                 shown->staging->cpu_base[shown->offsets[0]] == 128 && shown->staging->cpu_base[shown->offsets[1]] == 128 &&
                 shown->staging->cpu_base[shown->offsets[2] + shown->pitches[2] * 7 + 7] == 128;
    }
    expect(right, "every frame the mark names, decoded");
    const MoviePlayback::Stats stats = playback.stats();
    expect(stats.cooked_failed == 0u && stats.cooked_shown >= 8u, "no frame failed");
    mark.frame = MovieFrameMark::kNone;
    expect(playback.frame(mark) == nullptr, "closed: no frame");
    playback.shutdown();
#endif
}

}  // namespace

int main() {
    test_colours();
    test_nv12();
#if defined(_WIN32)
    test_find();
#endif
    test_index();
    if (failures == 0) std::cout << "movie playback tests passed\n";
    return failures == 0 ? 0 : 1;
}
