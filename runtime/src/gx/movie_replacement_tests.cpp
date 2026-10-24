// SPDX-License-Identifier: GPL-3.0-only
// HD movie replacements: the planes' layout, the JPEG YCbCr of known colours and the file lookup.

#include "galaxy/gx/movie_replacement.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace galaxy::gx;

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
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("nebula-movie-test-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "a");
    std::filesystem::create_directories(root / "b");
    std::ofstream(root / "a" / "PrologueA.mp4") << "x";
    std::ofstream(root / "b" / "prologuea.MKV") << "x";
    std::ofstream(root / "b" / "EndingA.txt") << "x";
    MovieReplacements movies;
    movies.configure({root / "a", root / "b"}, nullptr);
    const std::filesystem::path found = movies.find("/MovieData/PrologueA.thp");
    expect(found.parent_path() == root / "b", "a later folder wins, names and extensions in any case");
    expect(movies.find("/MovieData/EndingA.thp").empty(), "other extensions are not videos");
    expect(movies.find("/MovieData/PrologueB.thp").empty(), "no file: none");
    // Without a stager no decoder starts: the THP shows.
    MovieFrameMark mark{};
    const char name[] = "/MovieData/PrologueA.thp";
    std::memcpy(mark.name.data(), name, sizeof(name));
    mark.frame = 0;
    mark.frame_rate = 30.0f;
    expect(movies.frame(mark) == nullptr, "no upload buffers: no replacement");
    movies.shutdown();
    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    test_colours();
    test_nv12();
#if defined(_WIN32)
    test_find();
#endif
    if (failures == 0) std::cout << "movie replacement tests passed\n";
    return failures == 0 ? 0 : 1;
}
