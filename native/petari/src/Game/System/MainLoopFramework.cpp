#include "Game/System/MainLoopFramework.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "JSystem/JUtility/JUTNativeLog.hpp"
#include "Game/Util/RenderSignals.hpp"
#include "Game/System/NebulaFrameClock.hpp"

MainLoopFramework* MainLoopFramework::sManager;


void MainLoopFramework::ctor_subroutine(bool useAlpha) {
    mClearColor = (NebulaColor8){0, 0, 0, 0};
    mUseAlpha = useAlpha;
    mCombinationRatio = 0.0f;
    mRenderInterval = 1;
    mRenderCounter = 0;
    mDoRenderFrame = false;
    mPreRenderCallback = nullptr;
}

MainLoopFramework::~MainLoopFramework() {
    NebulaFrameClock::waitFrame();
    NebulaFrameClock::waitFrame();
}

MainLoopFramework* MainLoopFramework::createManager(bool useAlpha) {
    if (!sManager) {
        sManager = new MainLoopFramework(useAlpha);
    }
    return sManager;
}

void MainLoopFramework::prepareCopyDisp() {
    J3DPass::depth(true, NEBULA_COMPARE_LEQUAL, true);
    if (mUseAlpha) {
        J3DPass::writeAlpha(true);
    }
}

void MainLoopFramework::preGX() {
    if (mUseAlpha) {
        J3DNativeState::setFrameAlpha(true);
    } else {
        J3DNativeState::setFrameAlpha(false);
    }
}

void MainLoopFramework::endGX() {
}

void MainLoopFramework::beginRender() {
    if ((mDoRenderFrame = ++mRenderCounter >= mRenderInterval)) {
        mRenderCounter = 0;
    }
    if (mDoRenderFrame) {
        clearFrame(mClearColor);
        preGX();
    }
}

void MainLoopFramework::endRender() {
    endGX();
    if (mDoRenderFrame) {
        if (mPreRenderCallback) {
            mPreRenderCallback();
        }
        prepareCopyDisp();
        J3DNativeState::showFrame();
    }
}

void MainLoopFramework::endFrame() {
    J3DNativeState::endFrame();
}

// The frame cleared to `color` (its alpha when the frame keeps alpha) and the far depth, with the view, matrix and
// pixel state the frame's first draws start from.
void MainLoopFramework::clearFrame(NebulaColor8 color) {
    u16 frameWidth = NebulaFrameClock::cScreenUnitsAcross;
    u16 frameHeight = NebulaFrameClock::cScreenUnitsDown;
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
    if (mUseAlpha) {
        J3DPass::writeAlpha(true);
        J3DPass::constantAlpha(false, color.a);
    }
    J3DPass::depth(true, NEBULA_COMPARE_ALWAYS, true);
    J3DPass::cull(NEBULA_CULL_BACK);
    const u32 rgba = (static_cast< u32 >(color.r) << 24) | (static_cast< u32 >(color.g) << 16) |
                     (static_cast< u32 >(color.b) << 8) | color.a;
    J3DNativeTarget::clearFrame(0, 0, frameWidth, frameHeight, rgba,
                                NEBULA_CLEAR_COLOR | (mUseAlpha ? NEBULA_CLEAR_ALPHA : 0u) | NEBULA_CLEAR_DEPTH);
}

void MainLoopFramework::prepareForExit() {
    NebulaFrameClock::show(false);
    NebulaFrameClock::waitFrame();
    if (sManager) {
        delete sManager;
        sManager = nullptr;
    }
}
