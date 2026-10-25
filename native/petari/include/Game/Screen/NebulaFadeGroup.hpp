#pragma once

#ifdef TARGET_PC
#include <revolution/types.h>

namespace nw4r {
    namespace lyt {
        class DrawInfo;
        class Pane;
    };  // namespace lyt
};  // namespace nw4r

// Nebula: a layout pane whose own alpha fades while its layers overlap fades as one picture. The game fades each pane
// on its own, so a half-faded window shows its own drop shadow and the screen dim through its panel and turns darker
// than both the screen before it and the finished window (SysInfoWindow's appear and end: 150 levels against 163 and
// 211). Here the screen behind the pane is copied, the pane and its children draw at full strength, and the copy goes
// back over them at the rest of the alpha: every pixel moves straight from the screen behind to the finished window.
namespace NebulaFadeGroup {
    // Pane::CalculateMtx, for a pane with influenced alpha below 255: whether it fades as one (above 0, two or more
    // layers that draw, and a copy free for it). True opens it until leave().
    bool enter(const nw4r::lyt::Pane* pPane);
    void leave();

    // Pane::Draw, for a pane enter() took: the copy of the screen behind it; then its drawing; then end() lays the copy
    // back at 255 less the pane's alpha.
    void begin(const nw4r::lyt::Pane* pPane, const nw4r::lyt::DrawInfo& rInfo);
    void end(const nw4r::lyt::Pane* pPane, u8 alpha);
};  // namespace NebulaFadeGroup
#endif
