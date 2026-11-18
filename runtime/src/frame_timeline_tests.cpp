// SPDX-License-Identifier: GPL-3.0-only
// frame_timeline.h on every platform: nothing recorded without --diagnostics timeline; with it, events keep their
// order, values, clock (galaxy::qpc_now) and thread, and the exit's file is what tools/perf/timeline_digest.py reads
// (the NBTL0001 header: version, ticks a second, records written and kept, process). Before 11-18 the timeline's
// clock read 0 and it wrote no file outside Windows.
#include "galaxy/frame_timeline.h"
#include "galaxy/launch_args.h"
#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* text) {
    if (!value) throw std::runtime_error(text);
}
}  // namespace

int main() {
    namespace tl = galaxy::timeline;
    try {
        const std::filesystem::path file =
            std::filesystem::temp_directory_path() /
            ("frame_timeline_tests-" + std::to_string(galaxy::current_process_id()) + ".bin");

        tl::initialize();
        check(!tl::enabled(), "no timeline without --diagnostics timeline");
        tl::record(tl::Event::GameFrame, 1u);
        check(tl::g_state.next.load() == 0u, "a timeline that is off records nothing");

        galaxy::g_launch_args.diagnostics = galaxy::kDiagnoseTimeline;
        galaxy::g_launch_args.timeline = file;
        tl::initialize();
        check(tl::enabled(), "--diagnostics timeline turns it on");
        const std::uint64_t before = tl::now();
        tl::record(tl::Event::SlotWaitBegin, 7u);
        tl::record(tl::Event::SlotWaitEnd, 7u);
        tl::record(tl::Event::ContentTime, 2u, 1u);
        const std::uint64_t after = tl::now();
        check(before != 0u && after >= before, "the clock reads the platform's");
        tl::dump();
        check(!tl::enabled(), "the exit's dump ends recording");

        std::FILE* in = galaxy::open_path(file, "rb");
        check(in != nullptr, "the dump writes the file --timeline names");
        std::uint64_t header[8]{};
        std::vector<tl::Record> records(4u);
        const bool header_read = std::fread(header, sizeof(header), 1u, in) == 1u;
        const std::size_t read = std::fread(records.data(), sizeof(tl::Record), records.size(), in);
        std::fclose(in);
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
        check(header_read && read == 3u, "a header and the three records, nothing more");
        check(std::memcmp(&header[0], "NBTL0001", 8u) == 0 && header[1] == 1u, "format NBTL0001, version 1");
        check(header[2] == static_cast<std::uint64_t>(galaxy::qpc_frequency()) && header[2] != 0u,
              "the header gives the clock's ticks a second");
        check(header[3] == 3u && header[4] == 3u, "three records kept of three written");
        check(header[5] == galaxy::current_process_id(), "the header names the process");
        check(records[0].event == static_cast<std::uint16_t>(tl::Event::SlotWaitBegin) &&
                  records[1].event == static_cast<std::uint16_t>(tl::Event::SlotWaitEnd) &&
                  records[2].event == static_cast<std::uint16_t>(tl::Event::ContentTime),
              "the events in their order");
        check(records[0].a == 7u && records[1].a == 7u && records[2].a == 2u && records[2].b == 1u,
              "each event's values");
        for (std::size_t i = 0u; i < 3u; ++i) {
            check(records[i].valid == 1u, "every record is marked written");
            check(records[i].qpc >= before && records[i].qpc <= after, "every record's moment is on the clock");
            check(records[i].thread != 0u && records[i].thread == records[0].thread,
                  "every record names this thread");
        }
        check(records[0].qpc <= records[1].qpc && records[1].qpc <= records[2].qpc, "moments in order");
    } catch (const std::exception& error) {
        std::cerr << "frame_timeline_tests: FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "frame_timeline_tests: pass\n";
    return 0;
}
