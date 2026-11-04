#include "Game/System/GameFrame.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include "Game/System/NebulaFrameClock.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

namespace {
    bool sKeepAlpha = true;

    // The frame cleared to black and the far depth, with the view and pixel state its first draws start from.
    void clearFrame() {
        const u16 frameWidth = NebulaFrameClock::cScreenUnitsAcross;
        const u16 frameHeight = NebulaFrameClock::cScreenUnitsDown;
        Mtx44 proj;
        NebulaMtxOrtho(proj, 0.0f, frameHeight, 0.0f, frameWidth, 0.0f, 1.0f);
        J3DNativeView::setOrthographic(proj);
        J3DNativeView::setViewport(0.0f, 0.0f, frameWidth, frameHeight, 0.0f, 1.0f);
        J3DNativeView::setScissor(0, 0, frameWidth, frameHeight);
        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DPass::positionMatrix(0);
        J3DPass::noAlphaTest();
        J3DPass::noBlend();
        if (sKeepAlpha) {
            J3DPass::writeAlpha(true);
            J3DPass::constantAlpha(false, 0);
        }
        J3DPass::depth(true, NEBULA_COMPARE_ALWAYS, true);
        J3DPass::cull(NEBULA_CULL_BACK);
        J3DNativeTarget::clearFrame(0, 0, frameWidth, frameHeight, 0,
                                    NEBULA_CLEAR_COLOR | (sKeepAlpha ? NEBULA_CLEAR_ALPHA : 0u) | NEBULA_CLEAR_DEPTH);
    }
}  // namespace

namespace GameFrame {
    void begin() {
        clearFrame();
        J3DNativeState::setFrameAlpha(sKeepAlpha);
    }

    void end() {
        J3DNativeState::showFrame();
        J3DNativeState::endFrame();
    }

    void setKeepAlpha(bool keep) {
        sKeepAlpha = keep;
    }
}  // namespace GameFrame
