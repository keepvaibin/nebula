// SPDX-License-Identifier: GPL-3.0-only
// benchmark_results.h: the results folder, a result read back, and the frame-time graph.
#include "galaxy/benchmark_results.h"

#include "galaxy/platform_paths.h"
#include "galaxy/strict_json.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <system_error>

namespace galaxy::bench {

// ---- the folder ------------------------------------------------------------------------------------------------------

std::filesystem::path results_folder() {
    const std::filesystem::path diagnostics = nebula_diagnostics_directory();
    return diagnostics.empty() ? std::filesystem::path() : diagnostics / "benchmarks";
}

std::filesystem::path new_result_file(const std::filesystem::path& folder) {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char name[48];
    std::strftime(name, sizeof(name), "benchmark-%Y%m%dT%H%M%SZ.json", &utc);
    return folder / name;
}

std::filesystem::path frames_file(const std::filesystem::path& result) {
    std::filesystem::path frames = result;
    frames.replace_extension(".frames");
    return frames;
}

namespace {

bool is_result_name(const std::filesystem::path& file) {
    const std::string name = file.filename().string();
    return name.size() == std::string("benchmark-20261115T070000Z.json").size() && name.rfind("benchmark-", 0) == 0 &&
           file.extension() == ".json";
}

}  // namespace

std::vector<std::filesystem::path> list_results(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    if (folder.empty() || !std::filesystem::is_directory(folder, error)) return files;
    for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (it->is_regular_file(error) && is_result_name(it->path())) files.push_back(it->path());
    }
    // The names hold the UTC time with fixed widths: newest first by name.
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.filename() > b.filename(); });
    return files;
}

void keep_newest(const std::filesystem::path& folder, std::size_t count) {
    const std::vector<std::filesystem::path> files = list_results(folder);
    for (std::size_t i = count; i < files.size(); ++i) {
        std::error_code error;
        std::filesystem::remove(files[i], error);
        std::filesystem::remove(frames_file(files[i]), error);
        std::filesystem::remove(std::filesystem::path(files[i]).concat(".sent"), error);
    }
}

// ---- a result read back --------------------------------------------------------------------------------------------

namespace {

using json::Value;

double number_at(const Value* v, std::string_view key, double otherwise = 0.0) {
    const Value* item = v != nullptr ? v->find(key) : nullptr;
    return item != nullptr ? item->number_or(otherwise) : otherwise;
}

unsigned whole_at(const Value* v, std::string_view key) {
    const double n = number_at(v, key, 0.0);
    return n >= 0.0 && n <= 4294967295.0 ? static_cast<unsigned>(n) : 0u;
}

std::string text_at(const Value* v, std::string_view key) {
    const Value* item = v != nullptr ? v->find(key) : nullptr;
    return item != nullptr && item->type == Value::String ? item->text : std::string();
}

bool bool_at(const Value* v, std::string_view key) {
    const Value* item = v != nullptr ? v->find(key) : nullptr;
    return item != nullptr && item->type == Value::Bool && item->boolean;
}

SettingsView settings_of(const Value* s) {
    SettingsView view;
    view.internal_height = whole_at(s, "internal_height");
    view.upscaler = whole_at(s, "upscaler");
    view.msaa_samples = std::max(1u, whole_at(s, "msaa_samples"));
    view.aa = whole_at(s, "aa");
    view.smaa_quality = whole_at(s, "smaa_quality");
    view.bloom = whole_at(s, "bloom");
    view.aspect_mode = whole_at(s, "aspect_mode");
    view.ambient_occlusion = bool_at(s, "ambient_occlusion");
    view.enhanced_lighting = bool_at(s, "enhanced_lighting");
    return view;
}

// A per-frame cost's average, or -1 for null.
double average_of(const Value* v) {
    return v != nullptr && v->type == Value::Object ? number_at(v, "average", -1.0) : -1.0;
}

PartView part_of(const Value& run) {
    PartView part;
    part.present = true;
    const Value* summary = run.find("summary");
    part.average_fps = number_at(summary, "average_fps");
    part.low_10_fps = number_at(summary, "low_10_fps");
    part.low_1_fps = number_at(summary, "low_1_fps");
    part.low_01_fps = number_at(summary, "low_01_fps");
    const Value* frame_ms = summary != nullptr ? summary->find("frame_ms") : nullptr;
    part.average_ms = number_at(frame_ms, "average");
    part.p99_ms = number_at(frame_ms, "p99");
    part.stutters = whole_at(summary, "stutters");
    const Value* steps = summary != nullptr ? summary->find("game_steps") : nullptr;
    part.steps_per_s = number_at(steps, "average_per_s");
    part.steps_low_1 = number_at(steps, "low_1_per_s");
    part.limiter = text_at(summary, "limiter");
    part.display_limited = bool_at(summary, "display_limited");
    part.settings = settings_of(run.find("measured_with"));
    const Value* scenes = run.find("scenes");
    double game = 0.0, render = 0.0, audio = 0.0;
    unsigned game_n = 0u, render_n = 0u, audio_n = 0u;
    if (scenes != nullptr && scenes->type == Value::Array) {
        for (const Value& scene : scenes->items) {
            StageView stage;
            stage.stage = text_at(&scene, "stage");
            stage.average_fps = number_at(&scene, "average_fps");
            stage.low_10_fps = number_at(&scene, "low_10_fps");
            stage.low_1_fps = number_at(&scene, "low_1_fps");
            stage.p99_ms = number_at(scene.find("frame_ms"), "p99");
            stage.stutters = whole_at(&scene, "stutters");
            stage.steps_per_s = number_at(scene.find("game_steps"), "average_per_s");
            stage.steps_low_1 = number_at(scene.find("game_steps"), "low_1_per_s");
            stage.settings_held = bool_at(&scene, "settings_held");
            stage.load_ms = number_at(scene.find("load"), "ms");
            stage.gpu_ms = average_of(scene.find("gpu_ms"));
            const Value* cpu = scene.find("cpu_ms");
            stage.game_ms = average_of(cpu != nullptr ? cpu->find("game") : nullptr);
            stage.render_ms = average_of(cpu != nullptr ? cpu->find("render") : nullptr);
            const double audio_ms = number_at(cpu, "audio", -1.0);
            const auto add = [](double ms, double& sum, unsigned& count) {
                if (ms >= 0.0) {
                    sum += ms;
                    ++count;
                }
            };
            add(stage.game_ms, game, game_n);
            add(stage.render_ms, render, render_n);
            add(audio_ms, audio, audio_n);
            if (const Value* passes = scene.find("gpu_passes"); passes != nullptr && passes->type == Value::Object) {
                for (const json::Member& pass : passes->fields) {
                    const double ms = number_at(&pass.second, "average");
                    auto at = std::find_if(part.gpu_passes.begin(), part.gpu_passes.end(),
                                           [&](const auto& p) { return p.first == pass.first; });
                    if (at == part.gpu_passes.end()) {
                        part.gpu_passes.emplace_back(pass.first, 0.0);
                        at = part.gpu_passes.end() - 1;
                    }
                    at->second += ms;
                }
            }
            if (const Value* output = scene.find("output"); output != nullptr && output->items.size() == 2u) {
                part.output_width = static_cast<unsigned>(std::clamp(output->items[0].number_or(0.0), 0.0, 65535.0));
                part.output_height = static_cast<unsigned>(std::clamp(output->items[1].number_or(0.0), 0.0, 65535.0));
            }
            part.stages.push_back(std::move(stage));
        }
    }
    if (!part.stages.empty()) {
        for (auto& pass : part.gpu_passes) pass.second /= static_cast<double>(part.stages.size());
        std::sort(part.gpu_passes.begin(), part.gpu_passes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    }
    part.game_ms = game_n != 0u ? game / static_cast<double>(game_n) : -1.0;
    part.render_ms = render_n != 0u ? render / static_cast<double>(render_n) : -1.0;
    part.audio_ms = audio_n != 0u ? audio / static_cast<double>(audio_n) : -1.0;
    return part;
}

}  // namespace

bool parse_result(std::string_view text, ResultView& out) {
    out = ResultView{};
    Value root;
    if (!json::parse(text, root) || root.type != Value::Object) return false;
    const Value* schema = root.find("schema");
    if (schema == nullptr || schema->number_or(0.0) != static_cast<double>(kResultSchema)) return false;
    out.created_utc = text_at(&root, "created_utc");
    out.status = text_at(&root, "status");
    out.failure = text_at(&root, "failure");
    out.backend = text_at(&root, "backend");
    const Value* device = root.find("device");
    out.gpu = text_at(device, "gpu");
    out.driver = text_at(device, "driver_version");
    out.cpu = text_at(device, "cpu");
    out.os = text_at(device, "os");
    out.ram_mb = whole_at(device, "ram_mb");
    out.logical_cores = whole_at(device, "logical_cores");
    out.refresh_hz = number_at(device, "refresh_hz");
    if (const Value* output = device != nullptr ? device->find("output") : nullptr; output != nullptr && output->items.size() == 2u) {
        out.output_width = static_cast<unsigned>(std::clamp(output->items[0].number_or(0.0), 0.0, 65535.0));
        out.output_height = static_cast<unsigned>(std::clamp(output->items[1].number_or(0.0), 0.0, 65535.0));
    }
    if (const Value* runs = root.find("runs"); runs != nullptr && runs->type == Value::Array) {
        for (const Value& run : runs->items) {
            const std::string part = text_at(&run, "part");
            if (part == "yours") out.yours = part_of(run);
            if (const Value* scenes = run.find("scenes"); scenes != nullptr) {
                for (const Value& scene : scenes->items) {
                    out.gpu_memory_peak_mb = std::max(out.gpu_memory_peak_mb, whole_at(&scene, "gpu_memory_peak_mb"));
                }
            }
        }
    }
    const Value* summary = root.find("summary");
    out.verdict = text_at(summary, "verdict");
    out.spare_percent = number_at(summary, "spare_percent");
    if (const Value* rec = root.find("recommendation"); rec != nullptr && rec->type == Value::Object) {
        if (const Value* rows = rec->find("settings"); rows != nullptr && rows->type == Value::Array) {
            for (const Value& row : rows->items) {
                out.recommendation.push_back({text_at(&row, "key"), whole_at(&row, "current"), whole_at(&row, "recommended")});
            }
        }
    }
    if (const Value* thermal = root.find("thermal"); thermal != nullptr && thermal->type == Value::Object) {
        out.thermal_judged = bool_at(thermal, "judged");
        out.thermal_slowdown = number_at(thermal, "slowdown", 1.0);
    }
    return out.status == "complete" || out.status == "aborted" || out.status == "failed";
}

bool read_result(const std::filesystem::path& file, ResultView& out) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::string text(1u << 20, '\0');
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (in.gcount() == static_cast<std::streamsize>(text.size())) return false;  // larger than any result
    text.resize(static_cast<std::size_t>(in.gcount()));
    return parse_result(text, out);
}

// ---- the graph -----------------------------------------------------------------------------------------------------

namespace {

struct Color {
    std::uint8_t r, g, b, a;
};

// The settings menu's palette (NebulaSettingsMenu.cpp), for a picture that sits in its white window: a pale panel, its
// rules, the game's blues, a green and an amber for the 60 and 30 FPS lines, its warning red.
constexpr Color kCard{238, 243, 251, 255};
constexpr Color kGrid{180, 194, 222, 140};
constexpr Color kSeparator{180, 194, 222, 255};
constexpr Color kSpan{111, 166, 234, 80};
constexpr Color kLine{47, 111, 208, 255};
constexpr Color kSixty{46, 150, 90, 255};
constexpr Color kThirty{214, 140, 30, 255};
constexpr Color kStutter{200, 64, 46, 255};

class Canvas {
public:
    Canvas(unsigned width, unsigned height) : image_{width, height, std::vector<std::uint8_t>(std::size_t{width} * height * 4u, 0u)} {}

    void blend(int x, int y, Color c) {
        if (x < 0 || y < 0 || x >= static_cast<int>(image_.width) || y >= static_cast<int>(image_.height) || c.a == 0u) return;
        std::uint8_t* p = &image_.rgba[(static_cast<std::size_t>(y) * image_.width + static_cast<std::size_t>(x)) * 4u];
        const unsigned a = c.a, keep = 255u - a;
        const unsigned out_a = a + p[3] * keep / 255u;
        if (out_a == 0u) return;
        const auto mix = [&](std::uint8_t src, std::uint8_t dst) {
            return static_cast<std::uint8_t>((src * a + dst * p[3] * keep / 255u) / out_a);
        };
        p[0] = mix(c.r, p[0]);
        p[1] = mix(c.g, p[1]);
        p[2] = mix(c.b, p[2]);
        p[3] = static_cast<std::uint8_t>(out_a);
    }

    void fill(int x0, int y0, int x1, int y1, Color c) {
        for (int y = std::max(0, y0); y < std::min<int>(y1, static_cast<int>(image_.height)); ++y) {
            for (int x = std::max(0, x0); x < std::min<int>(x1, static_cast<int>(image_.width)); ++x) blend(x, y, c);
        }
    }

    // A rounded rectangle (corners `radius`), each corner pixel by how much of it lies inside.
    void rounded(int x0, int y0, int x1, int y1, int radius, Color c) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const double dx = std::max({0.0, static_cast<double>(x0 + radius) - (x + 0.5), (x + 0.5) - (x1 - radius)});
                const double dy = std::max({0.0, static_cast<double>(y0 + radius) - (y + 0.5), (y + 0.5) - (y1 - radius)});
                const double cover = std::clamp(static_cast<double>(radius) + 0.5 - std::hypot(dx, dy), 0.0, 1.0);
                if (cover > 0.0) blend(x, y, {c.r, c.g, c.b, static_cast<std::uint8_t>(c.a * cover)});
            }
        }
    }

    // A horizontal line `thickness` pixels high centred on y; dashed with `dash` pixels on and off when not 0.
    void hline(int x0, int x1, int y, Color c, int thickness, int dash = 0) {
        for (int x = x0; x < x1; ++x) {
            if (dash == 0 || ((x - x0) / dash) % 2 == 0) fill(x, y - thickness / 2, x + 1, y - thickness / 2 + thickness, c);
        }
    }

    // A vertical line `thickness` pixels wide from y0 to y1 (either order) at column x.
    void vline(int x, int y0, int y1, Color c, int thickness) {
        fill(x - thickness / 2, std::min(y0, y1), x - thickness / 2 + thickness, std::max(y0, y1) + 1, c);
    }

    Image take() { return std::move(image_); }

private:
    Image image_;
};

}  // namespace

double graph_step(double top) noexcept {
    constexpr double kBases[] = {1.0, 2.0, 2.5, 5.0};
    for (double power = 1.0; power <= 1e7; power *= 10.0) {
        for (const double base : kBases) {
            if (top / (base * power) <= 6.0 + 1e-9) return base * power;
        }
    }
    return top / 6.0;
}

double graph_top(double fps) noexcept {
    const double wanted = std::isfinite(fps) ? std::max(fps, kGraphLeastTopFps) : kGraphLeastTopFps;
    const double step = graph_step(wanted);
    return std::ceil(wanted / step - 1e-9) * step;
}

FrameGraph draw_frame_graph(const std::vector<FrameList>& lists, unsigned part, unsigned width, unsigned height) {
    std::vector<const FrameList*> shown;
    std::size_t frames = 0u;
    for (const FrameList& list : lists) {
        if (list.part == part && !list.frame_ms.empty()) {
            shown.push_back(&list);
            frames += list.frame_ms.size();
        }
    }
    FrameGraph graph;
    if (shown.empty() || width < kGraphMinWidth || height < kGraphMinHeight || width > kGraphMaxSide || height > kGraphMaxSide) {
        return graph;
    }

    // Lines and the panel's corners grow with the picture: about a pixel for every 340 of its width.
    const int unit = std::max(1, static_cast<int>(std::lround(static_cast<double>(width) / 340.0)));
    Canvas canvas(width, height);
    const int w = static_cast<int>(width), h = static_cast<int>(height);
    canvas.rounded(0, 0, w, h, 5 * unit, kCard);
    const int pad = 4 * unit;
    graph.left = static_cast<unsigned>(pad);
    graph.top = static_cast<unsigned>(pad);
    graph.right = static_cast<unsigned>(w - pad);
    graph.bottom = static_cast<unsigned>(h - pad);
    const int left = pad, right = w - pad, above = pad, below = h - pad;
    const int plot_w = right - left, plot_h = below - above;

    // Each column's frames: their rate (frames over their time), the rate of the slowest, and whether it stuttered.
    struct Column {
        std::size_t count = 0u;
        double fps = 0.0, slowest_fps = 0.0;
        bool stutter = false;
    };
    std::vector<Column> columns(static_cast<std::size_t>(plot_w));
    const double per_column = static_cast<double>(frames) / static_cast<double>(plot_w);
    std::size_t index = 0u;
    std::size_t next_start = 0u;
    std::size_t list = 0u, in_list = 0u;
    double fastest = 0.0;
    for (int column = 0; column < plot_w; ++column) {
        const auto end = std::min(frames, static_cast<std::size_t>(std::llround((column + 1) * per_column)));
        Column& c = columns[static_cast<std::size_t>(column)];
        double high = 0.0, sum = 0.0;
        for (; index < end; ++index) {
            if (index == next_start) {
                graph.stage_starts.push_back(static_cast<unsigned>(left + column));
                next_start += shown[list]->frame_ms.size();
            }
            const double ms = shown[list]->frame_ms[in_list];
            if (++in_list == shown[list]->frame_ms.size()) {
                in_list = 0u;
                ++list;
            }
            high = std::max(high, ms);
            sum += ms;
            ++c.count;
            c.stutter = c.stutter || ms > kStutterMs;
        }
        if (c.count == 0u || sum <= 0.0) {
            c.count = 0u;
            continue;
        }
        c.fps = 1000.0 * static_cast<double>(c.count) / sum;
        c.slowest_fps = high > 0.0 ? 1000.0 / high : c.fps;
        fastest = std::max(fastest, c.fps);
    }
    graph.top_fps = graph_top(fastest * 1.05);
    graph.step_fps = graph_step(graph.top_fps);
    const auto y_of = [&](double fps) {
        return below - static_cast<int>(std::lround(std::clamp(fps / graph.top_fps, 0.0, 1.0) * plot_h));
    };

    // The scale's lines (the game writes their values beside the picture).
    for (double fps = graph.step_fps; fps < graph.top_fps - 1e-6; fps += graph.step_fps) {
        canvas.hline(left, right, y_of(fps), kGrid, unit);
    }

    // The frames: each column's band from its rate down to its slowest frame's (red where that frame stuttered), and the
    // line through the rates. A column with no frame (fewer frames than columns) repeats the one before it.
    std::vector<int> averages(static_cast<std::size_t>(plot_w), -1);
    int previous_y = -1;
    for (int column = 0; column < plot_w; ++column) {
        const Column& c = columns[static_cast<std::size_t>(column)];
        if (c.count == 0u) {
            averages[static_cast<std::size_t>(column)] = previous_y;
            continue;
        }
        const int x = left + column;
        canvas.fill(x, y_of(c.fps), x + 1, y_of(c.slowest_fps) + 1, c.stutter ? kStutter : kSpan);
        previous_y = y_of(c.fps);
        averages[static_cast<std::size_t>(column)] = previous_y;
    }
    // The average over the bands: each column joined to the one before it, so steep steps stay one line.
    for (int column = 0; column < plot_w; ++column) {
        const int y = averages[static_cast<std::size_t>(column)];
        if (y < 0) continue;
        const int before = column > 0 ? averages[static_cast<std::size_t>(column - 1)] : -1;
        canvas.vline(left + column, before >= 0 ? before : y, y, kLine, unit + 1);
    }

    // The 60 and 30 FPS lines over the frames, dashed (the game names them).
    canvas.hline(left, right, y_of(60.0), kSixty, unit + 1, 6 * unit);
    canvas.hline(left, right, y_of(30.0), kThirty, unit + 1, 6 * unit);

    // Each stage after the first: a line at its start (the game numbers the stages over the picture).
    for (std::size_t s = 1; s < graph.stage_starts.size(); ++s) {
        canvas.vline(static_cast<int>(graph.stage_starts[s]), above, below, kSeparator, unit);
    }
    graph.image = canvas.take();
    return graph;
}

}  // namespace galaxy::bench
