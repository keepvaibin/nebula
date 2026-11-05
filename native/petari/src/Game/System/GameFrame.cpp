#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/GameFrame.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
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
        J3DNativeView::setOrthographic(proj);
        J3DNativeView::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        J3DNativeView::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DPass::positionMatrix(0);
        J3DPass::noBlend();
        J3DPass::depth(true, NEBULA_COMPARE_ALWAYS, true);
        J3DPass::cull(NEBULA_CULL_BACK);
        J3DNativeTarget::clearFrame(0,
                                    NEBULA_CLEAR_COLOR | NEBULA_CLEAR_DEPTH);
    }
}  // namespace

namespace GameFrame {
    void begin() {
        clearFrame();
    }

    void end() {
        J3DNativeState::showFrame();
        J3DNativeState::endFrame();
    }

}  // namespace GameFrame
