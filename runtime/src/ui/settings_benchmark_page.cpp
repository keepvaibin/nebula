// SPDX-License-Identifier: GPL-3.0-only
// settings_benchmark_page.h: the benchmark's results window, page by page.
#include "galaxy/ui/settings_benchmark_page.h"

#include "galaxy/ui/gpu_pass_label.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <string_view>

namespace galaxy::ui {
namespace {

// `value` with `decimals` places and a decimal point in every locale (the game's text keeps it: "S.#_MS").
std::string fixed(double value, unsigned decimals) {
    if (!std::isfinite(value)) return "0";
    unsigned scale = 1u;
    for (unsigned i = 0u; i < decimals; ++i) scale *= 10u;
    const bool negative = value < 0.0;
    const long long units = std::llround(std::fabs(value) * scale);
    std::string text = std::to_string(units / scale);
    if (decimals != 0u) {
        std::string part = std::to_string(units % scale);
        text += '.' + std::string(decimals - part.size(), '0') + part;
    }
    return negative && units != 0 ? '-' + text : text;
}

// A frame rate: whole above 100, one place below (57.6 FPS reads against 60).
std::string fps(double value) {
    return fixed(value, value < 100.0 ? 1u : 0u) + " FPS";
}

// A low's frame rate; "-" for none (a result written before it was measured).
std::string low_fps(double value) {
    return value > 0.0 ? fps(value) : std::string("\"-");
}

// A time in milliseconds: two places under 10, one under 100, whole above.
std::string ms(double value) {
    return fixed(value, value < 10.0 ? 2u : value < 100.0 ? 1u : 0u) + " MS";
}

// A name shown as it is in a cell: no tab, line break or leading mark of the table's own. Longer than `most` bytes, it
// is cut at a character's start and ends in "...".
std::string name_cell(std::string_view text, std::size_t most = 64u) {
    std::string out = "\"";
    for (const char c : text) {
        out += c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
    }
    if (out.size() > most + 1u) {
        std::size_t cut = most + 1u - 3u;
        while (cut > 1u && (static_cast<unsigned char>(out[cut]) & 0xC0u) == 0x80u) --cut;
        out.resize(cut);
        while (out.size() > 1u && (out.back() == ' ' || out.back() == '(' || out.back() == '-' || out.back() == ',')) {
            out.pop_back();
        }
        out += "...";
    }
    return out;
}

// A stage id the game names, left aligned (printable ASCII only: the result file's own check).
std::string stage_cell(const std::string& id) {
    std::string out = "<@";
    for (const char c : id) {
        if (c > ' ' && c < 0x7F) out += c;
    }
    return out;
}

void line(std::string& table, std::initializer_list<std::string_view> cells) {
    if (!table.empty()) table += '\n';
    bool first = true;
    for (const std::string_view cell : cells) {
        if (!first) table += '\t';
        table += cell;
        first = false;
    }
}

// A heading cell: the text, marked (the table's marks are added here, so the text tool sees each text as it is shown).
std::string heading(std::string_view text) {
    return "#" + std::string(text);
}

// A heading cell, left aligned.
std::string left_heading(std::string_view text) {
    return "#<" + std::string(text);
}

const char* limiter_text(const std::string& limiter) {
    if (limiter == "gpu") return "THE GRAPHICS CARD";
    if (limiter == "render thread") return "THE RENDER THREAD";
    if (limiter == "game thread") return "THE GAME THREAD";
    if (limiter == "display") return "THE DISPLAY'S REFRESH RATE";
    if (limiter == "game frames") return "THE GAME'S OWN 60 FRAMES A SECOND";
    return "WAITING ON THE SYSTEM";
}

const char* renderer_name(const std::string& backend) {
    if (backend == "d3d12") return "Direct3D 12";
    if (backend == "d3d11") return "Direct3D 11";
    if (backend == "vulkan") return "Vulkan";
    if (backend == "metal") return "Metal";
    return "-";
}

// The part the summary and the time page read: the player's settings.
const bench::PartView& main_part(const bench::ResultView& r) {
    return r.yours;
}

BenchmarkPage summary_page(const bench::ResultView& r, const BenchmarkClock& clock) {
    BenchmarkPage page{"SUMMARY", {}, true};
    line(page.table, {"RESULT", benchmark_outcome(r)});
    if (r.status == "failed" && r.failure.rfind("the game did not enter", 0) == 0) {
        line(page.table, {"REASON", "A STAGE DID NOT LOAD"});
    }
    const bench::PartView& part = main_part(r);
    if (part.present && !part.stages.empty()) {
        line(page.table, {"AVERAGE", fps(part.average_fps)});
        if (part.low_10_fps > 0.0) line(page.table, {"10% LOW", fps(part.low_10_fps)});
        line(page.table, {"1% LOW", fps(part.low_1_fps)});
        // The game's own steps: interpolated pictures can hold the frame rate while the game falls behind.
        if (part.steps_per_s > 0.0) {
            line(page.table, {"GAME STEPS A SECOND", fixed(part.steps_per_s, 1u)});
            line(page.table, {"GAME STEPS, 1% LOW", fixed(part.steps_low_1, 1u)});
        }
        line(page.table, {"99TH PERCENTILE FRAME TIME", ms(part.p99_ms)});
        line(page.table, {"STUTTERS (OVER 25 MS)", std::to_string(part.stutters)});
        line(page.table, {"LIMITED BY", limiter_text(part.limiter)});
    }
    if (const std::string when = clock ? clock(r.created_utc) : std::string(); !when.empty()) {
        line(page.table, {"RUN ON", name_cell(when)});
    }
    return page;
}

void stage_lines(std::string& table, const bench::PartView& part, std::string_view title) {
    line(table, {heading(title)});
    unsigned number = 1u;
    for (const bench::StageView& stage : part.stages) {
        line(table, {std::to_string(number++), stage_cell(stage.stage), fps(stage.average_fps), low_fps(stage.low_10_fps),
                     fps(stage.low_1_fps), ms(stage.p99_ms)});
    }
    line(table, {"", left_heading("ALL STAGES"), heading(fps(part.average_fps)), heading(low_fps(part.low_10_fps)),
                 heading(fps(part.low_1_fps)), heading(ms(part.p99_ms))});
}

BenchmarkPage stages_page(const bench::ResultView& r) {
    BenchmarkPage page{"STAGES", {}, false};
    line(page.table, {"", left_heading("STAGE"), heading("AVERAGE"), heading("10% LOW"), heading("1% LOW"),
                      heading("99TH PERCENTILE")});
    if (r.yours.present && !r.yours.stages.empty()) {
        stage_lines(page.table, r.yours, "YOUR SETTINGS");
    } else {
        line(page.table, {"", "NO STAGE WAS MEASURED"});
    }
    return page;
}

BenchmarkPage time_page(const bench::ResultView& r) {
    BenchmarkPage page{"WHERE THE TIME GOES", {}, false};
    const bench::PartView& part = main_part(r);
    std::vector<std::pair<std::string, std::string>> time;
    if (part.present && !part.stages.empty()) {
        // Each feature's passes under its one name, slowest first; a feature under 0.005 ms a frame is left out.
        std::vector<std::pair<std::string, double>> features;
        double total = 0.0;
        for (const auto& [pass, pass_ms] : part.gpu_passes) {
            if (!(pass_ms > 0.0)) continue;
            const char* const label = gpu_pass_label(pass);
            const std::string cell = label != nullptr ? std::string(label) : name_cell(pass, 24u);
            const auto at = std::find_if(features.begin(), features.end(), [&](const auto& f) { return f.first == cell; });
            if (at == features.end()) {
                features.emplace_back(cell, pass_ms);
            } else {
                at->second += pass_ms;
            }
            total += pass_ms;
        }
        std::stable_sort(features.begin(), features.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        features.erase(std::find_if(features.begin(), features.end(), [](const auto& f) { return f.second < 0.005; }),
                       features.end());
        if (!features.empty()) {
            time.emplace_back(heading("GRAPHICS CARD, EACH FRAME"), "");
            constexpr std::size_t kFeaturesShown = 6u;
            for (std::size_t i = 0u; i < features.size() && i < kFeaturesShown; ++i) {
                time.emplace_back(features[i].first,
                                  ms(features[i].second) + " (" + fixed(features[i].second * 100.0 / total, 0u) + "%)");
            }
        }
        const std::string processor = heading("PROCESSOR, EACH FRAME");
        time.emplace_back(processor, "");
        if (part.game_ms >= 0.0) time.emplace_back("GAME THREAD", ms(part.game_ms));
        if (part.render_ms >= 0.0) time.emplace_back("RENDER THREAD", ms(part.render_ms));
        if (part.audio_ms >= 0.0) time.emplace_back("AUDIO", ms(part.audio_ms));
        if (time.back().first == processor) time.pop_back();
    }
    std::vector<std::pair<std::string, std::string>> device;
    device.emplace_back(heading("HARDWARE"), "");
    if (!r.gpu.empty()) device.emplace_back("GRAPHICS CARD", name_cell(r.gpu));
    if (!r.driver.empty()) device.emplace_back("DRIVER", name_cell(r.driver, 40u));
    if (r.gpu_memory_peak_mb != 0u) device.emplace_back("VIDEO MEMORY USED", std::to_string(r.gpu_memory_peak_mb) + " MB");
    if (!r.cpu.empty()) device.emplace_back("PROCESSOR", name_cell(r.cpu));
    if (r.logical_cores != 0u) device.emplace_back("LOGICAL PROCESSORS", std::to_string(r.logical_cores));
    if (r.ram_mb != 0u) device.emplace_back("MEMORY", fixed(r.ram_mb / 1024.0, 0u) + " GB");
    if (!r.os.empty()) device.emplace_back("SYSTEM", name_cell(r.os, 48u));
    device.emplace_back("RENDERER", name_cell(renderer_name(r.backend)));
    if (r.output_width != 0u && r.output_height != 0u) {
        std::string display = std::to_string(r.output_width) + "X" + std::to_string(r.output_height);
        if (r.refresh_hz > 0.0) display += ", " + fixed(r.refresh_hz, 0u) + " HZ";
        device.emplace_back("DISPLAY", display);
    }
    // Side by side: the time on the left, the device on the right (a narrow window stacks them).
    const std::size_t lines = std::max(time.size(), device.size());
    for (std::size_t i = 0u; i < lines; ++i) {
        const auto left = i < time.size() ? time[i] : std::pair<std::string, std::string>{};
        const auto right = i < device.size() ? device[i] : std::pair<std::string, std::string>{};
        std::string third = right.first.empty() ? std::string() : "<" + right.first;
        if (!right.first.empty() && right.first[0] == '#') third = "#<" + right.first.substr(1);
        line(page.table, {left.first, left.second, third, right.second});
    }
    return page;
}

BenchmarkPage runs_page(const std::vector<bench::ResultView>& runs, std::size_t shown, const BenchmarkClock& clock) {
    BenchmarkPage page{"EARLIER RUNS", {}, false};
    line(page.table,
         {heading("DATE"), left_heading("RENDERER"), heading("AVERAGE"), heading("10% LOW"), heading("1% LOW")});
    for (std::size_t i = 0u; i < runs.size() && i < bench::kKeptResults; ++i) {
        const bench::ResultView& r = runs[i];
        const bench::PartView& part = main_part(r);
        const std::string mark = i == shown ? "*" : "";
        std::string when = clock ? clock(r.created_utc) : std::string();
        if (when.empty()) when = "-";
        const bool measured = r.status == "complete" && part.present && !part.stages.empty();
        line(page.table, {mark + name_cell(when), mark + "<" + name_cell(renderer_name(r.backend)),
                          mark + (measured ? fps(part.average_fps) : benchmark_outcome(r)),
                          mark + (measured ? low_fps(part.low_10_fps) : std::string("\"-")),
                          mark + (measured ? fps(part.low_1_fps) : std::string("\"-"))});
    }
    return page;
}

}  // namespace

std::string benchmark_local_time(const std::string& created_utc) {
    // benchmark.cpp writes %Y-%m-%dT%H:%M:%SZ.
    if (created_utc.size() != 20u || created_utc[4] != '-' || created_utc[7] != '-' || created_utc[10] != 'T' ||
        created_utc[13] != ':' || created_utc[16] != ':' || created_utc[19] != 'Z') {
        return {};
    }
    const auto field = [&](std::size_t at, std::size_t count, int& out) {
        out = 0;
        for (std::size_t i = at; i < at + count; ++i) {
            if (created_utc[i] < '0' || created_utc[i] > '9') return false;
            out = out * 10 + (created_utc[i] - '0');
        }
        return true;
    };
    std::tm utc{};
    int year = 0, month = 0;
    if (!field(0, 4, year) || !field(5, 2, month) || !field(8, 2, utc.tm_mday) || !field(11, 2, utc.tm_hour) ||
        !field(14, 2, utc.tm_min) || !field(17, 2, utc.tm_sec) || month < 1 || month > 12 || utc.tm_mday < 1 ||
        utc.tm_mday > 31 || utc.tm_hour > 23 || utc.tm_min > 59 || utc.tm_sec > 60) {
        return {};
    }
    utc.tm_year = year - 1900;
    utc.tm_mon = month - 1;
#if defined(_WIN32)
    const std::time_t time = _mkgmtime(&utc);
#else
    const std::time_t time = timegm(&utc);
#endif
    std::tm local{};
#if defined(_WIN32)
    if (time == static_cast<std::time_t>(-1) || localtime_s(&local, &time) != 0) return {};
#else
    if (time == static_cast<std::time_t>(-1) || localtime_r(&time, &local) == nullptr) return {};
#endif
    char text[32]{};
    if (std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M", &local) == 0u) return {};
    return text;
}

std::string benchmark_outcome(const bench::ResultView& r) {
    if (r.status == "aborted") return r.failure == "stopped" ? "STOPPED BEFORE THE END" : "CLOSED BEFORE THE END";
    if (r.status != "complete") return "DID NOT FINISH";
    if (r.verdict == "fits") return "SMOOTH AT 60 FPS";
    if (r.verdict == "cpu_bound") return "LIMITED BY THE PROCESSOR";
    if (r.verdict == "below_target") return "DROPS BELOW 60 FPS";
    return "NOTHING WAS MEASURED";
}

std::vector<BenchmarkPage> benchmark_pages(const std::vector<bench::ResultView>& runs, std::size_t shown,
                                           const BenchmarkClock& clock) {
    std::vector<BenchmarkPage> pages;
    if (shown >= runs.size()) return pages;
    const bench::ResultView& r = runs[shown];
    pages.push_back(summary_page(r, clock));
    pages.push_back(stages_page(r));
    pages.push_back(time_page(r));
    pages.push_back(runs_page(runs, shown, clock));
    return pages;
}

}  // namespace galaxy::ui
