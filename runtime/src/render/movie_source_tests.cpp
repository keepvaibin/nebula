// SPDX-License-Identifier: GPL-3.0-only
// A replacement movie through the system's decoder (movie_source.h): tests/data/movie/h264_64x64.mp4 (64x64 H.264 in
// MP4, 30 frames at 30 fps, 0-14 red and 15-29 blue, a key frame every 15; tools/dev/make_movie_fixture.cpp) opens,
// gives every picture in order with its time and its colours, and seeks to its second key frame. Usage:
// galaxy_movie_source_tests <the fixture> [WIDTHxHEIGHT]: the size, 64x64 unless given (the same movie at another size
// from tools/dev/make_movie_fixture, for a device whose hardware decoder takes no 64x64 picture).
#include "movie_source.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace galaxy::render;

namespace {

int g_failures = 0;

void expect(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what.c_str());
        ++g_failures;
    }
}

// The picture's centre as the source gives it: Y Cb Cr, or B G R.
struct Sample {
    int a = 0, b = 0, c = 0;
};

Sample centre(const MovieSourceFormat& format, const MovieSourcePicture& picture) {
    const std::uint32_t x = format.width / 2u, y = format.height / 2u;
    if (format.pixels == MoviePixels::Bgra) {
        const std::uint8_t* const pixel = picture.planes[0] + static_cast<std::ptrdiff_t>(y) * picture.pitches[0] + x * 4u;
        return {pixel[0], pixel[1], pixel[2]};
    }
    const std::ptrdiff_t chroma = static_cast<std::ptrdiff_t>(y / 2u) * picture.pitches[1] +
                                  static_cast<std::ptrdiff_t>(x / 2u) * picture.chroma_step;
    return {picture.planes[0][static_cast<std::ptrdiff_t>(y) * picture.pitches[0] + x], picture.planes[1][chroma],
            picture.planes[2][chroma]};
}

// The fixture's colours as written (BT.601, limited range): red Y 81 Cb 90 Cr 240, blue Y 41 Cb 240 Cr 110; as BGRA,
// red 0 0 255 and blue 255 0 0. Within the encoder's loss.
bool within(const Sample& s, int a, int b, int c) {
    return std::abs(s.a - a) <= 16 && std::abs(s.b - b) <= 16 && std::abs(s.c - c) <= 16;
}
bool red(const MovieSourceFormat& format, const Sample& s) {
    return format.pixels == MoviePixels::Bgra ? within(s, 0, 0, 255) : within(s, 81, 90, 240);
}
bool blue(const MovieSourceFormat& format, const Sample& s) {
    return format.pixels == MoviePixels::Bgra ? within(s, 255, 0, 0) : within(s, 41, 240, 110);
}

std::string text(const Sample& s) {
    return std::to_string(s.a) + " " + std::to_string(s.b) + " " + std::to_string(s.c);
}

}  // namespace

int main(int argc, char** argv) {
    unsigned long wanted_width = 64u, wanted_height = 64u;
    if (argc == 3) {
        char* end = nullptr;
        wanted_width = std::strtoul(argv[2], &end, 10);
        wanted_height = end != nullptr && *end == 'x' ? std::strtoul(end + 1, &end, 10) : 0u;
    }
    if ((argc != 2 && argc != 3) || wanted_width < 2u || wanted_height < 2u) {
        std::fprintf(stderr, "usage: galaxy_movie_source_tests <h264_64x64.mp4> [WIDTHxHEIGHT]\n");
        return 2;
    }
    prepare_movie_sources();
    std::string error;
    const std::unique_ptr<MovieSource> source = open_movie_source(argv[1], error);
    if (!source) {
        std::fprintf(stderr, "FAILED: the fixture does not open: %s\n", error.c_str());
        return 1;
    }
    expect(open_movie_source("no such movie.mp4", error) == nullptr && !error.empty(),
           "a missing file does not open, and says why");
    std::vector<std::int64_t> times;
    std::vector<Sample> samples;
    MovieSourcePicture picture;
    MovieSource::Read read = MovieSource::Read::Picture;
    while ((read = source->read(picture)) == MovieSource::Read::Picture && times.size() < 100u) {
        const MovieSourceFormat& format = source->format();
        expect(format.width == wanted_width && format.height == wanted_height,
               std::to_string(wanted_width) + "x" + std::to_string(wanted_height) + ", not " +
                   std::to_string(format.width) + "x" + std::to_string(format.height));
        expect(format.pixels == MoviePixels::Bgra || picture.chroma_step == 1u || picture.chroma_step == 2u,
               "a chroma step of 1 or 2");
        times.push_back(picture.time);
        samples.push_back(centre(format, picture));
    }
    expect(read == MovieSource::Read::End, "the movie ends: " + source->error());
    const MovieSourceFormat& format = source->format();
    expect(format.bt709 == (wanted_height >= 720u), "an SD movie with no colour tags is BT.601, an HD one BT.709");
    expect(format.rate == 0.0 || (format.rate > 29.99 && format.rate < 30.01),
           "30 frames a second, or none given: " + std::to_string(format.rate));
    expect(times.size() == 30u, "30 pictures, not " + std::to_string(times.size()));
    for (std::size_t f = 0; f < times.size(); ++f) {
        const std::int64_t wanted = static_cast<std::int64_t>(f) * 10'000'000 / 30;
        expect(std::llabs(times[f] - times[0] - wanted) <= 10'000,
               "picture " + std::to_string(f) + " at its time, not " + std::to_string(times[f] - times[0]));
        expect(f < 15u ? red(format, samples[f]) : blue(format, samples[f]),
               "picture " + std::to_string(f) + (f < 15u ? " red: " : " blue: ") + text(samples[f]));
    }
    // To the second key frame (frame 15, 0.5 s): the pictures go on from there, blue.
    if (!times.empty()) {
        expect(source->seek(times[0] + 5'000'000), "a seek to 0.5 s: " + source->error());
        read = source->read(picture);
        expect(read == MovieSource::Read::Picture, "a picture after the seek: " + source->error());
        if (read == MovieSource::Read::Picture) {
            expect(std::llabs(picture.time - times[0] - 5'000'000) <= 10'000,
                   "the seek lands on frame 15, not at " + std::to_string(picture.time - times[0]));
            const Sample after = centre(source->format(), picture);
            expect(blue(source->format(), after), "frame 15 is blue: " + text(after));
        }
        // And back to the start (a looping movie, a restart): red again.
        expect(source->seek(times[0]), "a seek back to the start: " + source->error());
        read = source->read(picture);
        expect(read == MovieSource::Read::Picture && std::llabs(picture.time - times[0]) <= 10'000 &&
                   red(source->format(), centre(source->format(), picture)),
               "the seek back lands on frame 0, red");
    }
    if (g_failures != 0) {
        std::fprintf(stderr, "movie source: %d failed\n", g_failures);
        return 1;
    }
    std::printf("movie source: %zu pictures, %ux%u %s, every case holds\n", times.size(), format.width, format.height,
                format.pixels == MoviePixels::Yuv420 ? (picture.chroma_step == 2u ? "NV12" : "I420") : "BGRA");
    return 0;
}
