// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The pacing recorder's present ids and display reports, and the display model's, on a DXGI swap chain (Direct3D 12
// and 11).

#include "galaxy/frame_timeline.h"
#include "galaxy/pacing_recorder.h"
#include "galaxy/render/frame_schedule.h"
#include "galaxy/render/renderer_common.h"
#include "galaxy/windows_api.h"

#include <dxgi.h>

#include <cstdint>

namespace galaxy::render {

// What a present was drawn for and when it could go: `slot`, the refresh the schedule drew it for (0: none); `ready`,
// when it was ready to present, before the wait on the frame latency object; `call`, when the Present call was made.
struct DxgiPresent {
    std::int64_t slot = 0;
    std::int64_t ready = 0;
    std::int64_t call = 0;
};

// After a Present: the present's id is the swap chain's present count, and the frame statistics give the latest
// present the display showed: its present count and the refresh it was first shown at (PresentRefreshCount), against
// the refresh counted at SyncQPCTime (SyncRefreshCount). Its moment is SyncQPCTime less the refreshes between, in
// QueryPerformanceCounter ticks (the runtime's clock). ours: a present of the game's own picture (not one frame
// generation's swap chain makes, which counts its own presents). period_qpc: the display's refresh period, 0 unknown.
// Both go to the pacing recorder and to the display model (G2.3.1), whose answers stop when they stop (a present not
// ours starts it again). --diagnostics timeline records the refresh each present was drawn for (DisplayModel events),
// beside the statistics' DisplayStats. The variable-refresh proof (G2.3.5 point 5) hears of each present, with the period
// the pacer held it to (`held`, 0: none), and of each moment shown: a sample taken at the refresh that showed it
// (no refresh between) needs no period, so a display whose refreshes follow the presents is read without one.
inline void report_dxgi_present(IDXGISwapChain* chain, bool ours, std::int64_t period_qpc, DisplayModel& model,
                                 const DxgiPresent& present, RefreshAnswer& refresh, std::int64_t held) noexcept {
    UINT count = 0u;
    if (chain == nullptr || !ours || FAILED(chain->GetLastPresentCount(&count))) {
        galaxy::pacing::presented(0u);
        model.reset();
        return;
    }
    galaxy::pacing::presented(count);
    model.presented(count, present.slot, present.ready, present.call);
    refresh.presented(count, present.call, held);
    if (present.slot != 0 && galaxy::timeline::enabled()) {
        galaxy::timeline::record_at(galaxy::timeline::Event::DisplayModel, static_cast<std::uint64_t>(present.slot),
                                    count, static_cast<std::uint64_t>(present.ready));
    }
    DXGI_FRAME_STATISTICS statistics{};
    if (FAILED(chain->GetFrameStatistics(&statistics)) || statistics.SyncQPCTime.QuadPart == 0) {
        return;
    }
    const UINT since = statistics.SyncRefreshCount - statistics.PresentRefreshCount;
    if (since > 1000u || (since != 0u && period_qpc <= 0)) {
        return;  // the counts from different moments (a mode change), or refreshes between with no period: no moment
    }
    const std::int64_t shown = statistics.SyncQPCTime.QuadPart - static_cast<std::int64_t>(since) * period_qpc;
    refresh.shown(statistics.PresentCount, shown);
    if (period_qpc <= 0) {
        return;
    }
    galaxy::pacing::displayed(statistics.PresentCount, shown, period_qpc);
    model.displayed(statistics.PresentCount, shown, period_qpc);
}

}  // namespace galaxy::render
