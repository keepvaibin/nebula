#pragma once

class LayoutManager;

// Layouts on a picture wider or narrower than 16:9 (MR::getDisplay): full-screen backings cover the wider canvas, HUD
// panes follow the canvas edges, and the cinema bars stay at the screen edges. The rules become the layout's anchors
// (Game/Ui/Anchor.hpp), which its matrix pass applies.
namespace LayoutAspect {
    // Before the layout's matrix pass: its anchors (resolved by name the first time) and this pass's canvas.
    void prepare(LayoutManager* pManager);
    // The height of the canvas the layouts are drawn on (456 at 4:3 and 16:9, taller on a taller picture): the
    // screen's height in layout units.
    f32 getCanvasHeight();
};  // namespace LayoutAspect
