#pragma once

#ifdef TARGET_PC
#include <revolution/types.h>

class LayoutManager;

// Layouts on a picture wider or narrower than 16:9 (MR::getDisplay): full-screen backings cover the wider canvas, HUD
// panes follow the canvas edges, and the cinema bars stay at the screen edges. Run by
// LayoutManager::calcAnimWithoutLocationAdjust.
namespace LayoutAspect {
    // After the animation, before the matrices: backing sizes are absolute, so the animation keeps every other value.
    void extendBackings(LayoutManager* pManager);
    // After the matrices and their copies: moves and scales panes by the canvas, then copies the moved matrices again.
    void fit(LayoutManager* pManager);
};  // namespace LayoutAspect
#endif
