// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "galaxy/ui/settings_model.h"

namespace galaxy::ui {
// The GPU timing rows: the switch (off at every start) and, while it is on, each pass's time.
struct GpuTimingView {
    bool on = false;
    void (*toggle)() = nullptr;
    std::vector<std::pair<std::string, float>> passes;  // name, milliseconds
};

// The GRAPHICS CARD choice: the GPUs the renderer found, the one the next start takes (empty: automatic) and what
// choosing one does. With fewer than two GPUs the row only names the one in use.
struct GpuPickerView {
    std::vector<render::GpuChoice> choices;
    std::string next;
    std::function<void(const std::string&)> choose;
};

// The UPDATES rows (desktop hosts; none where a store updates Nebula): the start's check switch, the check, and the
// install of a newer release.
enum class UpdateStep : unsigned {
    NotChecked,     // no answer this session or kept from an earlier one
    Checking,
    UpToDate,
    CheckFailed,
    Available,      // `version` is newer
    Downloading,    // `percent` of it
    Installing,     // checked, being put in place
    Restart,        // installed: the next start runs it
    InstallFailed,
};
struct UpdatesView {
    bool check_at_start = true;
    UpdateStep step = UpdateStep::NotChecked;
    std::string version;        // Available and after
    unsigned percent = 0u;      // Downloading
    bool can_install = true;    // false: the system installs Nebula's updates (a distribution's package, Flatpak)
    bool closes_game = false;   // Windows: Nebula Setup installs it once the game has closed
    std::function<void()> toggle, check, install, restart;
};

// The BENCHMARK rows: run it (where the game can start itself again), the last result, its recommended settings and
// sending it to Nebula's developer (where the build can send reports).
enum class BenchmarkSend : unsigned {
    Hidden,   // the build cannot send reports, or there is nothing to send (no result, or one closed early)
    Offer,
    Sending,
    Sent,
    Failed,
};
enum class BenchmarkApply : unsigned {
    NoResult,      // no finished run with recommended settings
    OtherGpu,      // the last result is from another graphics card
    InUse,         // the settings in use (with the changes waiting) are the recommended ones
    Offer,
};
struct BenchmarkView {
    bool can_run = false;          // this host runs it in the game (benchmark_menu::Runner)
    bool has_result = false;
    std::string last_when;         // the last result's time (benchmark_local_time)
    std::string last_outcome;      // what it came to (benchmark_outcome)
    BenchmarkApply apply = BenchmarkApply::NoResult;
    BenchmarkSend send = BenchmarkSend::Hidden;
    std::function<void()> run, view, recommend, send_report;
};

// The RENDERER row: the renderer this program runs; on Windows, the renderer the next start takes (config.ini
// renderer) and what each renderer needs of the card the next start renders on (render::renderers_for over the
// renderer check's cards), with what choosing one does. Without `choose` the row only names the renderer in use.
struct RendererPickerView {
    render::RendererKind running = render::RendererKind::kD3D12;
    render::RendererKind next = render::RendererKind::kAuto;
    bool checked = false;  // the renderer check ended; `card` holds its answers
    render::CardRenderers card;
    std::function<void(render::RendererKind)> choose;
};
// The renderer the row shows for the next start: the chosen one where the card runs it (or it is not known), else the
// first in render::kRendererPreference that is (the automatic choice); the running one when none is.
[[nodiscard]] render::RendererKind shown_renderer(const RendererPickerView& view);

std::vector<SettingsRow> system_page(const render::RenderFeatureCaps& caps, std::string post_process,
                                     void (*record_diagnostics)(), const GpuTimingView* timing = nullptr,
                                     const GpuPickerView* gpus = nullptr, const UpdatesView* updates = nullptr,
                                     const BenchmarkView* benchmark = nullptr, const RendererPickerView* renderers = nullptr);
}  // namespace galaxy::ui
