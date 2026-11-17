// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/benchmark_stats.h"
#include "galaxy/strict_json.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}

bool near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance;
}
}  // namespace

// The headline judges the game's own steps as well as the pictures (P-089): with interpolation the pictures keep
// coming at a high rate while the game's steps fall behind, and the pictures alone said "fits".
void verdict_tests() {
    namespace bench = galaxy::bench;
    bench::FrameStats pictures;
    pictures.frames = 1000u;
    pictures.low_1_fps = 140.0;
    bench::FrameStats steps;
    steps.frames = 600u;
    steps.low_1_fps = 59.5;
    check(std::string(bench::verdict_of(pictures, steps, false)) == "fits", "pictures and steps at 60: fits");
    steps.low_1_fps = 45.0;
    check(std::string(bench::verdict_of(pictures, steps, true)) == "cpu_bound",
          "interpolated pictures at 140 while the steps fall to 45: not fits, the processor's");
    check(std::string(bench::verdict_of(pictures, steps, false)) == "below_target", "not fits, not a thread's: below");
    steps.low_1_fps = 59.5;
    pictures.low_1_fps = 50.0;
    check(std::string(bench::verdict_of(pictures, steps, false)) == "below_target", "steps at 60, pictures below: below");
    steps.frames = 0u;
    check(std::string(bench::verdict_of(pictures, steps, false)) == "none", "no steps counted: none");
}

int main() {
    verdict_tests();
    namespace bench = galaxy::bench;
    try {
        {
            // 990 frames of 10 ms and 10 of 50 ms: 10.4 s in all.
            std::vector<double> frames(990u, 10.0);
            frames.insert(frames.begin() + 500, 10u, 50.0);
            const bench::FrameStats s = bench::frame_stats(frames);
            check(s.frames == 1000u && near(s.seconds, 10.4), "every frame counted");
            check(near(s.average_fps, 1000.0 / 10.4), "average FPS is frames over time, not the mean of per-frame FPS");
            check(near(s.average_ms, 10.4), "average frame time");
            check(near(s.p50_ms, 10.0) && near(s.p99_ms, 10.0) && near(s.p999_ms, 50.0) && near(s.worst_ms, 50.0),
                  "nearest-rank percentiles");
            check(near(s.low_10_fps, 100000.0 / 1400.0), "the 10% low: 10 frames of 50 ms and 90 of 10 ms");
            check(near(s.low_1_fps, 20.0), "the 1% low averages the slowest 10 frames (50 ms each)");
            check(near(s.low_01_fps, 20.0), "the 0.1% low takes the slowest frame");
            check(s.stutters == 10u, "frames over 25 ms are stutters");
        }
        {
            // The 1% low is the average time of the slowest 1%, not its slowest frame.
            std::vector<double> frames(198u, 5.0);
            frames.push_back(10.0);
            frames.push_back(30.0);
            const bench::FrameStats s = bench::frame_stats(frames);
            check(near(s.low_10_fps, 20000.0 / 130.0), "the slowest 20 of 200 frames: 30, 10 and 18 of 5 ms");
            check(near(s.low_1_fps, 2000.0 / 40.0), "the slowest two of 200 frames: 20 ms on average, 50 FPS");
            check(near(s.low_01_fps, 1000.0 / 30.0), "fewer than 1000 frames: the 0.1% low is the slowest frame");
            check(s.stutters == 1u && near(s.worst_ms, 30.0), "one stutter");
        }
        {
            const bench::FrameStats none = bench::frame_stats({});
            check(none.frames == 0u && none.average_fps == 0.0 && none.low_10_fps == 0.0 && none.low_1_fps == 0.0,
                  "no frames: all zero");
            const bench::FrameStats bad = bench::frame_stats({0.0, -3.0, NAN, INFINITY, 16.0});
            check(bad.frames == 1u && near(bad.average_fps, 62.5), "zero, negative and non-finite times are left out");
            check(bench::frame_stats({25.0}).stutters == 0u && bench::frame_stats({25.001}).stutters == 1u,
                  "a stutter is longer than 25 ms");
        }
        {
            check(bench::percentile({}, 0.5) == 0.0, "an empty list's percentile");
            const std::vector<double> sorted{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
            check(bench::percentile(sorted, 0.0) == 1.0 && bench::percentile(sorted, 0.5) == 5.0 &&
                      bench::percentile(sorted, 0.95) == 10.0 && bench::percentile(sorted, 1.0) == 10.0 &&
                      bench::percentile(sorted, 2.0) == 10.0,
                  "percentile ranks");
            const bench::Spread s = bench::spread({3.0, 1.0, 2.0, -1.0, NAN});
            check(s.count == 3u && near(s.average, 2.0) && near(s.p99, 3.0) && near(s.worst, 3.0),
                  "a spread leaves out unknown values");
        }
        {
            bench::FrameFacts f;
            f.frame_ms = 40.0;
            f.gpu_ms = 35.0;
            f.pipelines = 1u;
            f.read_bytes = 4096u;
            check(bench::cause_of(f) == bench::Cause::Pipeline, "a pipeline built names the frame first");
            f.pipelines = 0u;
            check(bench::cause_of(f) == bench::Cause::DiscRead, "then a disc read");
            f.read_bytes = 0u;
            f.textures = 2u;
            check(bench::cause_of(f) == bench::Cause::Texture, "then a texture made");
            f.textures = 0u;
            check(bench::cause_of(f) == bench::Cause::Gpu, "then the busiest part");
            f.render_ms = 38.0;
            check(bench::cause_of(f) == bench::Cause::RenderThread, "the busiest of the three");
            f.game_ms = 39.0;
            check(bench::cause_of(f) == bench::Cause::GameThread, "the game thread");
            f.gpu_ms = f.render_ms = f.game_ms = 5.0;
            check(bench::cause_of(f) == bench::Cause::Waiting, "nothing took half the frame: waiting");
            f.gpu_ms = f.render_ms = f.game_ms = -1.0;
            check(bench::cause_of(f) == bench::Cause::Waiting, "nothing known: waiting");
            check(std::string(bench::cause_name(bench::Cause::DiscRead)) == "disc read", "cause names");
        }
        {
            std::vector<bench::FrameList> lists(3u);
            lists[0] = {0u, "CosmosGardenGalaxy", {16.67, 4.21, 0.0, 700.0, 0.004}};
            lists[1] = {1u, "AstroGalaxy", {}};
            lists[2] = {1u, "HeavenlyBeachGalaxy", std::vector<double>(5000u, 2.5)};
            const std::string bytes = bench::encode_frame_lists(lists);
            std::vector<bench::FrameList> back;
            check(bench::decode_frame_lists(bytes, back) && back.size() == 3u, "the frame lists read back");
            check(back[0].part == 0u && back[0].stage == "CosmosGardenGalaxy" && back[0].frame_ms.size() == 5u &&
                      near(back[0].frame_ms[0], 16.67, 1e-6) && near(back[0].frame_ms[1], 4.21, 1e-6) &&
                      back[0].frame_ms[2] == 0.0 && near(back[0].frame_ms[3], 655.35, 1e-6) && back[0].frame_ms[4] == 0.0,
                  "10 microsecond units, longer frames stored as 655.35 ms");
            check(back[1].frame_ms.empty() && back[2].frame_ms.size() == 5000u && back[2].part == 1u, "empty and long lists");
            check(bytes.size() == 12u + (2u + 18u + 4u + 10u) + (2u + 11u + 4u) + (2u + 19u + 4u + 10000u), "compact");
            for (std::size_t cut = 0; cut < bytes.size(); cut += 97u) {
                check(!bench::decode_frame_lists(std::string_view(bytes).substr(0, cut), back) && back.empty(),
                      "a cut file is refused whole");
            }
            check(!bench::decode_frame_lists(bytes + "x", back), "trailing bytes are refused");
            std::string other = bytes;
            other[0] = 'X';
            check(!bench::decode_frame_lists(other, back), "another file is refused");
            other = bytes;
            other[4] = 2;
            check(!bench::decode_frame_lists(other, back), "another version is refused");
            const std::vector<bench::FrameList> odd{{2u, "A", {1.0}}, {0u, std::string(300u, 'a'), {1.0}}, {0u, "a\nb", {1.0}}};
            check(bench::decode_frame_lists(bench::encode_frame_lists(odd), back) && back.empty(),
                  "lists that cannot be stored are left out");
        }
        {
            galaxy::json::Value v;
            check(galaxy::json::parse("{\"a\":[1,2.5,\"x\",null,true],\"b\":{\"c\":-3}}", v), "strict JSON reads");
            check(v.find("a") != nullptr && v.find("a")->items.size() == 5u && v.find("b")->find("c")->number == -3.0 &&
                      v.find("z") == nullptr && v.find("a")->find("x") == nullptr,
                  "members by name");
            check(!galaxy::json::parse("{\"a\":1,\"a\":2}", v) && !galaxy::json::parse("[1,]", v) &&
                      !galaxy::json::parse("\"\\n\"", v) && !galaxy::json::parse("1 2", v) &&
                      !galaxy::json::parse(std::string(20u, '[') + std::string(20u, ']'), v),
                  "duplicate keys, trailing commas, control characters, two values and deep nesting are refused");
        }
    } catch (const std::exception& failure) {
        std::cerr << "benchmark_stats: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "benchmark_stats: all checks passed\n";
    return 0;
}
