#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/GameFrame.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace {
    // The frame cleared to black and the far depth, with the view and pixel state its first draws start from.
    void clearFrame() {
        const f32 frameWidth = MR::getScreenWidth();
        const f32 frameHeight = NebulaFrameClock::cScreenLines;
        Mtx44 proj;
        NebulaMtxOrtho(proj, 0.0f, frameHeight, 0.0f, frameWidth, 0.0f, 1.0f);
        Frame::setOrthographic(proj);
        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
        Frame::clearFrame(0,
                                    NEBULA_CLEAR_COLOR | NEBULA_CLEAR_DEPTH);
    }
}  // namespace

namespace GameFrame {
    void begin() {
        Frame::begin();
        clearFrame();
    }

    void end() {
        Frame::show();
        Frame::end();
    }

}  // namespace GameFrame
