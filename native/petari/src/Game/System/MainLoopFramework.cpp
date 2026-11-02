#include "Game/System/MainLoopFramework.hpp"
#include "Game/System/NebulaKernel.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <revolution/os.h>
#include "Game/Util/SchedulerUtil.hpp"
#include <JSystem/J2DGraph/J2DOrthoGraph.hpp>
#include <JSystem/JUtility/JUTAssert.hpp>
#include <JSystem/JUtility/JUTConsole.hpp>
#include "Game/Util/RenderSignals.hpp"
#include <runtime.h>
#include "Game/System/NebulaFrameClock.hpp"

MainLoopFramework* MainLoopFramework::sManager;


namespace {
    void waitForFrames(u16);

};  // namespace

void MainLoopFramework::ctor_subroutine(bool useAlpha) {
    mClearColor = (NebulaColor8){0, 0, 0, 0};
    mRetraceCount = 1;
    mUseAlpha = useAlpha;
    mUseVFilter = true;
    mCombinationRatio = 0.0f;
    mRenderInterval = 1;
    mRenderCounter = 0;
    mDoRenderFrame = false;
    mPreRenderCallback = nullptr;
}

MainLoopFramework::~MainLoopFramework() {
    waitBlanking(2);
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
        J3DNativeState::setDither(GPU_TRUE);
    } else {
        J3DNativeState::setFrameAlpha(false);
        J3DNativeState::setDither(GPU_FALSE);
    }
}

void MainLoopFramework::endGX() {
    J2DOrthoGraph ortho(0.0f, 0.0f, NebulaFrameClock::cFrameWidth, NebulaFrameClock::cFrameHeight, -1.0f,
                        1.0f);
    if (JUTConsoleManager::sManager) {
        ortho.setPort();
        JUTConsoleManager::sManager->draw();
    }
    JUTAssertion::flushMessage_dbPrint();
}

void MainLoopFramework::waitForRetrace() {
    ::waitForFrames(mRetraceCount);
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
        J3DNativeState::showFrame(NebulaFrameClock::cFrameWidth,
                                  NebulaFrameClock::cFrameHeight, mUseVFilter);
    }
}

void MainLoopFramework::endFrame() {
    J3DNativeState::endFrame();
}

void MainLoopFramework::waitBlanking(int tickCount) {
    while (tickCount-- > 0) {
        ::waitForFrames(mRetraceCount);
    }
}


// The frame cleared to `color` (its alpha when the frame keeps alpha) and the far depth, with the view, matrix and
// pixel state the frame's first draws start from.
void MainLoopFramework::clearEfb(NebulaColor8 color) {
    u16 fbWidth = NebulaFrameClock::cFrameWidth;
    u16 efbHeight = NebulaFrameClock::cFrameHeight;
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

namespace {
    // Waits until `frames` frames (at least one) have passed since the last wait ended.
    void waitForFrames(u16 frames) {
        static u32 nextCount;
        static s8 countInitialized;
        if (!countInitialized) {
            nextCount = NebulaFrameClock::count();
            countInitialized = 1;
        }

        NebulaFrameClock::waitUntil(nextCount);
        nextCount = NebulaFrameClock::count() + (frames != 0 ? frames : 1);
    }

};  // namespace
