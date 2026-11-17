// SPDX-License-Identifier: GPL-3.0-only
// The benchmark's results pages (settings_benchmark_page.h) and their graph (benchmark_results.h): the table's marks
// as the game reads them, and the graph's scale and stage columns.
#include "galaxy/ui/settings_benchmark_page.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace bench = galaxy::bench;
namespace ui = galaxy::ui;

void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}

std::vector<std::string> split(const std::string& text, char at) {
    std::vector<std::string> out(1u);
    for (const char c : text) {
        if (c == at) {
            out.emplace_back();
        } else {
            out.back() += c;
        }
    }
    return out;
}

bench::PartView part(double fps, std::size_t stages) {
    bench::PartView p;
    p.present = true;
    p.average_fps = fps;
    p.low_10_fps = fps * 0.85;
    p.low_1_fps = fps * 0.7;
    p.p99_ms = 1000.0 / (fps * 0.6);
    p.stutters = 3u;
    p.steps_per_s = 59.96;
    p.steps_low_1 = 58.4;
    p.limiter = "gpu";
    p.gpu_passes = {{"opaque", 4.0}, {"bloom", 1.0}, {"scene ao", 0.5}, {"ao denoise", 0.25}, {"present", 0.001}};
    p.game_ms = 2.0;
    p.render_ms = 3.0;
    p.audio_ms = 0.5;
    for (std::size_t i = 0u; i < stages; ++i) {
        bench::StageView s;
        s.stage = i == 0u ? "CosmosGardenGalaxy" : "HeavenlyBeachGalaxy";
        s.average_fps = fps;
        s.low_10_fps = fps * 0.85;
        s.low_1_fps = fps * 0.7;
        s.p99_ms = 12.5;
        p.stages.push_back(s);
    }
    return p;
}

bench::ResultView result(const char* when, const char* backend) {
    bench::ResultView r;
    r.created_utc = when;
    r.status = "complete";
    r.backend = backend;
    r.gpu = "Example\tGPU\nName";  // the table's own separators in a name never make cells
    r.driver = "1.2.3";
    r.cpu = "Example CPU";
    r.os = "Example OS";
    r.ram_mb = 16384u;
    r.gpu_memory_peak_mb = 900u;
    r.logical_cores = 8u;
    r.output_width = 1920u;
    r.output_height = 1080u;
    r.refresh_hz = 60.0;
    r.verdict = "fits";
    r.yours = part(143.0, 2u);
    r.yours.stages[1].average_fps = 98.6;
    return r;
}

// Every cell of a table: its marks in order, then at most one of '@' and '"'.
void check_marks(const std::string& table, std::size_t columns, const char* label) {
    for (const std::string& line : split(table, '\n')) {
        const std::vector<std::string> cells = split(line, '\t');
        check(cells.size() <= columns, label);
        for (const std::string& cell : cells) {
            std::size_t at = 0u;
            for (const char mark : {'*', '#', '<'}) {
                if (at < cell.size() && cell[at] == mark) ++at;
            }
            check(cell.find_first_of("*#<", at) == std::string::npos || cell[at] == '"', label);
        }
    }
}
}  // namespace

int main() {
    try {
        const ui::BenchmarkClock clock = [](const std::string&) { return std::string("2026-11-15 01:43"); };
        const std::vector<bench::ResultView> runs = {result("2026-11-15T09:43:00Z", "d3d12"),
                                                    result("2026-11-14T09:43:00Z", "vulkan")};
        const std::vector<ui::BenchmarkPage> pages = ui::benchmark_pages(runs, 0u, clock);
        check(pages.size() == 4u, "four pages");
        check(pages[0].title == "SUMMARY" && pages[0].graph, "the summary has the graph");
        check(pages[1].title == "STAGES" && !pages[1].graph, "the stages page has none");
        check(pages[2].title == "WHERE THE TIME GOES" && pages[3].title == "EARLIER RUNS", "the other pages");

        // Summary: two columns, the outcome and the numbers.
        check_marks(pages[0].table, 2u, "summary marks");
        check(pages[0].table.find("RESULT\tSMOOTH AT 60 FPS") != std::string::npos, "the outcome");
        check(pages[0].table.find("AVERAGE\t143 FPS") != std::string::npos, "whole above 100 FPS");
        check(pages[0].table.find("GAME STEPS A SECOND\t60.0") != std::string::npos &&
                  pages[0].table.find("GAME STEPS, 1% LOW\t58.4") != std::string::npos,
              "the game's own steps beside the pictures");

        // Stages: the stage column left aligned, a stage named by its id, each part's heading spanning.
        check_marks(pages[1].table, 6u, "stage marks");
        check(pages[1].table.rfind("\t#<STAGE\t#AVERAGE", 0) == 0, "the stage heading is left aligned");
        check(pages[1].table.find("1\t<@CosmosGardenGalaxy\t143 FPS") != std::string::npos, "a stage line");
        check(pages[1].table.find("\n#YOUR SETTINGS\n") != std::string::npos, "the part heading is a line alone");
        check(pages[1].table.find("2\t<@HeavenlyBeachGalaxy\t98.6 FPS") != std::string::npos, "one place under 100 FPS");

        // Where the time goes: four cells a line, the hardware group's names left aligned.
        check_marks(pages[2].table, 4u, "time marks");
        for (const std::string& line : split(pages[2].table, '\n')) {
            const std::vector<std::string> cells = split(line, '\t');
            check(cells.size() == 4u, "two groups of two");
            check(cells[2].empty() || cells[2][0] == '<' || cells[2].rfind("#<", 0) == 0, "the second group is left aligned");
        }
        check(pages[2].table.find("\"Example GPU Name") != std::string::npos, "a name's tab and break become spaces");
        {
            // A device's whole name (an emulator's runs long); a longer one cut, its end marked.
            std::vector<bench::ResultView> named = runs;
            named[0].gpu = "Goldfish GFXStream (NVIDIA GeForce RTX 5090 Laptop GPU)";
            named[0].driver = std::string(80u, 'x');
            const std::string table = ui::benchmark_pages(named, 0u, clock)[2].table;
            check(table.find("\"Goldfish GFXStream (NVIDIA GeForce RTX 5090 Laptop GPU)\n") != std::string::npos,
                  "a long name whole");
            check(table.find("\"" + std::string(37u, 'x') + "...\n") != std::string::npos, "a cut name ends in ...");
        }
        check(pages[2].table.find("<MEMORY\t16 GB") != std::string::npos, "memory in GB");
        check(pages[2].table.find("\"opaque\t4.00 MS (70%)") != std::string::npos, "a pass no renderer names, as it is");
        check(pages[2].table.find("\nBLOOM\t1.00 MS (17%)") != std::string::npos, "a pass by its feature's name");
        check(pages[2].table.find("\nAMBIENT OCCLUSION\t0.75 MS (13%)") != std::string::npos, "one feature's passes together");
        check(pages[2].table.find("SCREEN OUTPUT") == std::string::npos, "a feature under 0.005 ms left out");

        // Earlier runs: the renderer left aligned, the shown run marked on every cell.
        check_marks(pages[3].table, 5u, "runs marks");
        check(split(pages[3].table, '\n').size() == 3u, "a heading and a line each");
        const std::vector<std::string> lines = split(ui::benchmark_pages(runs, 1u, clock)[3].table, '\n');
        check(lines[0] == "#DATE\t#<RENDERER\t#AVERAGE\t#10% LOW\t#1% LOW", "the runs heading");
        check(lines[1].rfind("\"2026-11-15 01:43\t<\"Direct3D 12\t", 0) == 0, "a run not shown: no mark");
        check(lines[2].rfind("*\"2026-11-15 01:43\t*<\"Vulkan\t*", 0) == 0, "the shown run: marked, renderer left aligned");

        // Outcomes.
        bench::ResultView r = runs[0];
        r.status = "aborted";
        r.failure = "stopped";
        check(ui::benchmark_outcome(r) == "STOPPED BEFORE THE END", "stopped");
        r.failure = "closed";
        check(ui::benchmark_outcome(r) == "CLOSED BEFORE THE END", "closed");
        r.status = "failed";
        check(ui::benchmark_outcome(r) == "DID NOT FINISH", "failed");
        check(ui::benchmark_local_time("2026-11-15 09:43").empty() && ui::benchmark_local_time("2026-13-15T09:43:00Z").empty(),
              "a time not written as benchmark.cpp writes it is none");
        const std::string local = ui::benchmark_local_time("2026-11-15T09:43:00Z");
        check(local.size() == 16u && local[4] == '-' && local[7] == '-' && local[10] == ' ' && local[13] == ':',
              "a local time");

        // The graph's scale: round steps, at most six of them to the top, the top a whole number of them.
        const double fastest[] = {10.0, 75.0, 130.0, 650.0, 3200.0, 12000.0};
        const double tops[] = {80.0, 80.0, 150.0, 800.0, 4000.0, 12000.0};
        const double steps[] = {20.0, 20.0, 25.0, 200.0, 1000.0, 2000.0};
        for (std::size_t i = 0u; i < 6u; ++i) {
            check(bench::graph_top(fastest[i]) == tops[i] && bench::graph_step(tops[i]) == steps[i], "a top and its step");
        }

        // The graph: two stages of part 0 side by side, the other part's frames left out.
        std::vector<bench::FrameList> lists(3u);
        lists[0].part = 0u;
        lists[0].frame_ms.assign(600u, 8.0);
        lists[1].part = 0u;
        lists[1].frame_ms.assign(300u, 14.0);
        lists[2].part = 1u;
        lists[2].frame_ms.assign(100u, 90.0);
        const bench::FrameGraph graph = bench::draw_frame_graph(lists, 0u, ui::kBenchmarkGraphWidth, ui::kBenchmarkGraphHeight);
        check(graph.image.width == 1024u && graph.image.height == 300u && graph.image.rgba.size() == 1024u * 300u * 4u,
              "the picture's size");
        check(graph.left < graph.right && graph.right <= 1024u && graph.top < graph.bottom && graph.bottom <= 300u,
              "the plot inside the picture");
        check(graph.top_fps == 150.0 && graph.step_fps == 25.0, "the top over the fastest column (125 FPS)");
        check(graph.stage_starts.size() == 2u && graph.stage_starts[0] == graph.left &&
                  graph.stage_starts[1] > graph.stage_starts[0] && graph.stage_starts[1] < graph.right,
              "each stage's first column");
        check(bench::draw_frame_graph(lists, 2u, 1024u, 300u).image.rgba.empty(), "no frames: no picture");
        check(bench::draw_frame_graph(lists, 0u, 100u, 300u).image.rgba.empty(), "too narrow: no picture");
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
    std::cout << "settings_benchmark_page tests passed\n";
    return 0;
}
