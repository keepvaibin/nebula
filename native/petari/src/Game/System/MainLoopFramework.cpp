#include "Game/System/MainLoopFramework.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <JSystem/J2DGraph/J2DOrthoGraph.hpp>
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
    J3DNativeState::setZMode(GPU_TRUE, GPU_LEQUAL, GPU_TRUE);
    if (mUseAlpha) {
        J3DNativeState::setAlphaUpdate(GPU_TRUE);
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
        clearEfb(mClearColor);
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
        J3DNativeState::showFrame(NebulaFrameClock::cCanvasWidth, NebulaFrameClock::cCanvasHeight);
    }
}

void MainLoopFramework::endFrame() {
    J3DNativeState::endFrame();
}

// The frame cleared to `color` (its alpha when the frame keeps alpha) and the far depth, with the view, matrix and
// pixel state the frame's first draws start from.
void MainLoopFramework::clearEfb(NebulaColor8 color) {
    u16 fbWidth = NebulaFrameClock::cCanvasWidth;
    u16 efbHeight = NebulaFrameClock::cCanvasHeight;
    Mtx44 proj;
    NebulaMtxOrtho(proj, 0.0f, efbHeight, 0.0f, fbWidth, 0.0f, 1.0f);
    J3DNativeView::setProjection(proj, GPU_ORTHOGRAPHIC);
    J3DNativeView::setViewport(0.0f, 0.0f, fbWidth, efbHeight, 0.0f, 1.0f);
    J3DNativeView::setScissor(0, 0, fbWidth, efbHeight);
    Mtx identity;
    NebulaMtxIdentity(identity);
    J3DNativeShape::setPosition(0, identity);
    J3DNativeState::setCurrentMtx(GPU_PNMTX0);
    J3DNativeState::setAlphaCompare(GPU_ALWAYS, 0, GPU_AOP_OR, GPU_ALWAYS, 0);
    J3DNativeState::setBlendMode(GPU_BM_NONE, GPU_BL_ZERO, GPU_BL_ZERO, GPU_LO_NOOP);
    if (mUseAlpha) {
        J3DNativeState::setAlphaUpdate(GPU_TRUE);
        J3DNativeState::setDstAlpha(GPU_FALSE, color.a);
    }
    J3DNativeState::setZMode(GPU_TRUE, GPU_ALWAYS, GPU_TRUE);
    J3DNativeState::setCullMode(GPU_CULL_BACK);
    const u32 rgba = (static_cast< u32 >(color.r) << 24) | (static_cast< u32 >(color.g) << 16) |
                     (static_cast< u32 >(color.b) << 8) | color.a;
    J3DNativeTarget::clearFrame(0, 0, fbWidth, efbHeight, rgba, 0xFFFFFF,
                                NEBULA_CLEAR_COLOR | (mUseAlpha ? NEBULA_CLEAR_ALPHA : 0u) | NEBULA_CLEAR_DEPTH);
}

void MainLoopFramework::setForOSResetSystem() {
    NebulaFrameClock::show(false);
    NebulaFrameClock::waitFrame();
    if (sManager) {
        delete sManager;
        sManager = nullptr;
    }
}
