#include "galaxy/ui/settings_system_page.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared System page must not include Windows.h
#endif

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
int recorded = 0;
void record() { ++recorded; }
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
galaxy::ui::SettingsRow& row(std::vector<galaxy::ui::SettingsRow>& rows, const char* label) {
    const auto found = std::find_if(rows.begin(), rows.end(), [label](const auto& r) { return !r.heading && r.label == label; });
    if (found == rows.end()) throw std::runtime_error(label);
    return *found;
}
}

int main() {
    try {
        galaxy::render::RenderFeatureCaps caps;
        std::memcpy(caps.adapter_description.data(), "TEST GPU  ", 11);
        caps.dxr_raytracing_tier = 11;
        caps.dxr.settings_exposed = true;
        auto rows = galaxy::ui::system_page(caps, "POST: SMAA", record);
        check(row(rows, "GRAPHICS CARD").value == "TEST GPU", "adapter description trimmed");
        check(row(rows, "RAY TRACING HARDWARE").value == "TIER 1.1 READY", "feature tier and visibility");
        check(row(rows, "NVIDIA STREAMLINE").value == "HIDDEN", "unexposed feature");
        check(row(rows, "POST-PROCESS").value == "SMAA", "post-process label");
        galaxy::ui::RenderConfig config;
        row(rows, "DIAGNOSTICS").press(config, 0);
        check(recorded == 1, "recording action");
        galaxy::ui::GpuTimingView off;
        off.toggle = [] {};
        auto untimed = galaxy::ui::system_page(caps, "POST: SMAA", record, &off);
        check(row(untimed, "GPU TIMING").value == "OFF", "timing is off by default");
        galaxy::ui::GpuTimingView on;
        on.on = true;
        on.toggle = [] {};
        on.passes = {{"scene", 3.0f}, {"present", 0.5f}, {"scene ao", 0.25f}, {"ao denoise", 0.25f}};
        auto timed = galaxy::ui::system_page(caps, "POST: SMAA", record, &on);
        check(row(timed, "GPU TIMING").value == "ON" && row(timed, "SCENE").value == "3.00 MS" &&
                  row(timed, "SCREEN OUTPUT").value == "0.50 MS" && row(timed, "AMBIENT OCCLUSION").value == "0.50 MS" &&
                  row(timed, "ALL PASSES").value == "4.00 MS",
              "passes and their sum are shown while on");
        check(std::none_of(rows.begin(), rows.end(), [](const auto& r) { return r.label == "GPU TIMING"; }), "no timing rows without a view");
        check(!row(rows, "GRAPHICS CARD").change && (row(rows, "GRAPHICS CARD").names & 2u) != 0u,
              "one GPU: the row names it and changes nothing");
        std::string chosen = "unset";
        galaxy::ui::GpuPickerView picker;
        picker.choices = {{"10de:2b85:89ef1043", "NVIDIA GeForce RTX 5090"}, {"8086:7d55:00000000", "Intel(R) Arc(TM) Graphics"}};
        picker.choose = [&chosen](const std::string& identity) { chosen = identity; };
        auto automatic = galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, &picker);
        check(row(automatic, "GRAPHICS CARD").value == "AUTO" && (row(automatic, "GRAPHICS CARD").names & 2u) == 0u,
              "the automatic choice shows AUTO, translated");
        check(row(automatic, "IN USE").value == "TEST GPU", "the GPU in use has its own row");
        row(automatic, "GRAPHICS CARD").change(config, -1);
        check(chosen == "unset", "left from AUTO stays at AUTO");
        row(automatic, "GRAPHICS CARD").change(config, 1);
        check(chosen == "10de:2b85:89ef1043", "right from AUTO takes the first GPU");
        picker.next = "8086:7d55:00000000";
        auto second = galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, &picker);
        check(row(second, "GRAPHICS CARD").value == "Intel(R) Arc(TM) Graphics" && (row(second, "GRAPHICS CARD").names & 2u) != 0u,
              "a chosen GPU shows its name as is");
        row(second, "GRAPHICS CARD").change(config, -1);
        check(chosen == "10de:2b85:89ef1043", "left from the second GPU takes the first");
        picker.next = "10de:2b85:89ef1043";
        auto first = galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, &picker);
        row(first, "GRAPHICS CARD").change(config, -1);
        check(chosen.empty(), "left from the first GPU goes back to AUTO");
        check(std::none_of(first.begin(), first.end(), [](const auto& r) { return r.label == "CHECK AT START"; }),
              "no UPDATES rows where the platform has no updates");
        int toggled = 0, checked = 0, installed = 0, restarted = 0;
        galaxy::ui::UpdatesView updates;
        updates.toggle = [&toggled] { ++toggled; };
        updates.check = [&checked] { ++checked; };
        updates.install = [&installed] { ++installed; };
        updates.restart = [&restarted] { ++restarted; };
        const auto page = [&] { return galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, nullptr, &updates); };
        auto idle = page();
        check(row(idle, "CHECK AT START").value == "ON", "the start's check is on");
        row(idle, "CHECK AT START").change(config, 1);
        check(toggled == 1, "the switch toggles");
        row(idle, "CHECK FOR UPDATES").press(config, 0);
        check(checked == 1, "CHECK checks");
        updates.step = galaxy::ui::UpdateStep::Checking;
        auto checking = page();
        check(row(checking, "CHECK FOR UPDATES").value == "CHECKING" && row(checking, "CHECK FOR UPDATES").buttons.empty(),
              "a running check has no button");
        updates.step = galaxy::ui::UpdateStep::UpToDate;
        auto current = page();
        check(row(current, "CHECK FOR UPDATES").help == "NEBULA IS UP TO DATE", "up to date");
        updates.step = galaxy::ui::UpdateStep::Available;
        updates.version = "2.1.0";
        auto available = page();
        check(row(available, "NEW VERSION").value == "2.1.0" && (row(available, "NEW VERSION").names & 2u) != 0u,
              "the new version is shown as is");
        row(available, "UPDATE NEBULA").press(config, 0);
        check(installed == 1, "UPDATE installs");
        updates.can_install = false;
        auto system_installs = page();
        check(std::none_of(system_installs.begin(), system_installs.end(), [](const auto& r) { return r.label == "UPDATE NEBULA"; }),
              "no UPDATE where the system installs Nebula");
        updates.can_install = true;
        updates.step = galaxy::ui::UpdateStep::Downloading;
        updates.percent = 140u;
        auto downloading = page();
        check(row(downloading, "UPDATE NEBULA").value == "100%", "the download's share, at most 100%");
        updates.step = galaxy::ui::UpdateStep::Restart;
        auto ready = page();
        row(ready, "UPDATE NEBULA").press(config, 0);
        check(restarted == 1, "RESTART restarts");
        updates.step = galaxy::ui::UpdateStep::InstallFailed;
        auto failed = page();
        row(failed, "UPDATE NEBULA").press(config, 0);
        check(installed == 2 && row(failed, "UPDATE NEBULA").buttons.front() == "TRY AGAIN", "a failed install tries again");

        // BENCHMARK: nothing where the game cannot start again and there is no result.
        galaxy::ui::BenchmarkView bench;
        const auto bench_page = [&] { return galaxy::ui::system_page(caps, "POST: SMAA", record, nullptr, nullptr, nullptr, &bench); };
        auto none = bench_page();
        check(std::none_of(none.begin(), none.end(), [](const auto& r) { return r.label == "BENCHMARK"; }), "no benchmark section");
        int runs = 0, views = 0, recommends = 0, sends = 0;
        bench.can_run = true;
        bench.run = [&runs] { ++runs; };
        bench.view = [&views] { ++views; };
        bench.recommend = [&recommends] { ++recommends; };
        bench.send_report = [&sends] { ++sends; };
        auto before = bench_page();
        check(std::any_of(before.begin(), before.end(), [](const auto& r) { return r.heading && r.label == "BENCHMARK"; }),
              "the section's heading");
        row(before, "RUN BENCHMARK").press(config, 0);
        check(runs == 1 && row(before, "LAST RESULT").value == "NONE" && !row(before, "LAST RESULT").press,
              "before a run: RUN, and no result to view");
        check(std::none_of(before.begin(), before.end(), [](const auto& r) { return r.label == "APPLY RECOMMENDED"; }),
              "nothing to apply before a run");
        bench.has_result = true;
        bench.last_when = "2026-11-15 01:43";
        bench.last_outcome = "SMOOTH AT 60 FPS";
        bench.apply = galaxy::ui::BenchmarkApply::Offer;
        bench.send = galaxy::ui::BenchmarkSend::Offer;
        auto offered = bench_page();
        check(row(offered, "LAST RESULT").help == "2026-11-15 01:43: SMOOTH AT 60 FPS", "the last result's time and outcome");
        row(offered, "LAST RESULT").press(config, 0);
        row(offered, "APPLY RECOMMENDED").press(config, 0);
        row(offered, "SEND TO DEVELOPER").press(config, 0);
        check(views == 1 && recommends == 1 && sends == 1, "VIEW, APPLY and SEND act");
        bench.apply = galaxy::ui::BenchmarkApply::InUse;
        bench.send = galaxy::ui::BenchmarkSend::Sent;
        auto in_use = bench_page();
        row(in_use, "APPLY RECOMMENDED").press(config, 0);
        check(recommends == 1 && row(in_use, "APPLY RECOMMENDED").buttons.front() == "-APPLY",
              "the recommended settings in use: APPLY unavailable and inert");
        check(row(in_use, "SEND TO DEVELOPER").value == "SENT" && !row(in_use, "SEND TO DEVELOPER").press, "sent: nothing to press");
        bench.send = galaxy::ui::BenchmarkSend::Failed;
        auto send_failed = bench_page();
        row(send_failed, "SEND TO DEVELOPER").press(config, 0);
        check(sends == 2 && row(send_failed, "SEND TO DEVELOPER").buttons.front() == "TRY AGAIN", "a failed send tries again");
        bench.can_run = false;
        auto no_run = bench_page();
        check(std::none_of(no_run.begin(), no_run.end(), [](const auto& r) { return r.label == "RUN BENCHMARK"; }) &&
                  row(no_run, "LAST RESULT").value == "VIEW",
              "where the game cannot start again: the kept result only");
        std::cout << "System page: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
