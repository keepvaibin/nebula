// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The pacing recorder's present ids and display reports on a DXGI swap chain (Direct3D 12 and 11).

#include "galaxy/pacing_recorder.h"
#include "galaxy/windows_api.h"

#include <dxgi.h>

#include <cstdint>

namespace galaxy::render {

// After a Present: the present's id is the swap chain's present count, and the frame statistics give the latest
// present the display showed: its present count and the refresh it was first shown at (PresentRefreshCount), against
// the refresh counted at SyncQPCTime (SyncRefreshCount). Its moment is SyncQPCTime less the refreshes between, in
// QueryPerformanceCounter ticks (the runtime's clock). ours: a present of the game's own picture (not one frame
// generation's swap chain makes, which counts its own presents). period_qpc: the display's refresh period, 0 unknown.
inline void report_dxgi_present(IDXGISwapChain* chain, bool ours, std::int64_t period_qpc) noexcept {
    UINT count = 0u;
    if (chain == nullptr || !ours || FAILED(chain->GetLastPresentCount(&count))) {
        galaxy::pacing::presented(0u);
        return;
    }
    galaxy::pacing::presented(count);
    DXGI_FRAME_STATISTICS statistics{};
    if (period_qpc <= 0 || FAILED(chain->GetFrameStatistics(&statistics)) || statistics.SyncQPCTime.QuadPart == 0) {
        return;
    }
    const UINT since = statistics.SyncRefreshCount - statistics.PresentRefreshCount;
    if (since > 1000u) {
        return;  // the counts from different moments (a mode change): no moment to tell
    }
    galaxy::pacing::displayed(statistics.PresentCount,
                              statistics.SyncQPCTime.QuadPart - static_cast<std::int64_t>(since) * period_qpc, period_qpc);
}

}  // namespace galaxy::render
