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
#include <JSystem/JUtility/JUTVideo.hpp>
#include "Game/Util/RenderSignals.hpp"
#include <runtime.h>
#include "Game/System/NebulaFrameClock.hpp"

MainLoopFramework* MainLoopFramework::sManager;


namespace {
    void waitForFrames(u16);

};  // namespace

void MainLoopFramework::ctor_subroutine(bool useAlpha) {
    mClearColor = (GXColor){0, 0, 0, 0};
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
    if (JUTVideo::getManager()) {
        waitBlanking(2);
    }
}

MainLoopFramework* MainLoopFramework::createManager(bool useAlpha) {
    if (!sManager) {
        sManager = new MainLoopFramework(useAlpha);
    }
    return sManager;
}

void MainLoopFramework::prepareCopyDisp() {
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    if (mUseAlpha) {
        J3DNativeState::setAlphaUpdate(GX_TRUE);
    }
}

void MainLoopFramework::preGX() {
    if (mUseAlpha) {
        J3DNativeState::setPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
        J3DNativeState::setDither(GX_TRUE);
    } else {
        J3DNativeState::setPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
        J3DNativeState::setDither(GX_FALSE);
    }
}

void MainLoopFramework::endGX() {
    J2DOrthoGraph ortho(0.0f, 0.0f, JUTVideo::getManager()->getFbWidth(), JUTVideo::getManager()->getEfbHeight(), -1.0f,
                        1.0f);
    if (JUTConsoleManager::sManager) {
        ortho.setPort();
        JUTConsoleManager::sManager->draw();
    }
    JUTAssertion::flushMessage_dbPrint();
}

void MainLoopFramework::waitForRetrace() {
    ::waitForFrames(mRetraceCount);
    JUTVideo::getManager()->waitRetraceIfNeed();
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
        J3DNativeState::showFrame(JUTVideo::getManager()->getFbWidth(),
                                  JUTVideo::getManager()->getEfbHeight(), mUseVFilter);
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
void MainLoopFramework::clearEfb(GXColor color) {
    u16 fbWidth = JUTVideo::getManager()->getFbWidth();
    u16 efbHeight = JUTVideo::getManager()->getEfbHeight();
    Mtx44 proj;
    NebulaMtxOrtho(proj, 0.0f, efbHeight, 0.0f, fbWidth, 0.0f, 1.0f);
    J3DNativeView::setProjection(proj, GX_ORTHOGRAPHIC);
    J3DNativeView::setViewport(0.0f, 0.0f, fbWidth, efbHeight, 0.0f, 1.0f);
    J3DNativeView::setScissor(0, 0, fbWidth, efbHeight);
    Mtx identity;
    NebulaMtxIdentity(identity);
    J3DNativeShape::setPosition(0, identity);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
    if (mUseAlpha) {
        J3DNativeState::setAlphaUpdate(GX_TRUE);
        J3DNativeState::setDstAlpha(GX_FALSE, color.a);
    }
    J3DNativeState::setZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    const u32 rgba = (static_cast< u32 >(color.r) << 24) | (static_cast< u32 >(color.g) << 16) |
                     (static_cast< u32 >(color.b) << 8) | color.a;
    J3DNativeTarget::clearFrame(0, 0, fbWidth, efbHeight, rgba, 0xFFFFFF,
                                NEBULA_CLEAR_COLOR | (mUseAlpha ? NEBULA_CLEAR_ALPHA : 0u) | NEBULA_CLEAR_DEPTH);
}

void MainLoopFramework::setForOSResetSystem() {
    JUTVideo::destroyManager();
    NebulaFrameClock::show(false);
    NebulaFrameClock::waitFrame();
    if (sManager) {
        delete sManager;
        sManager = nullptr;
    }
}

namespace {
    // Waits until `frames` host frames (at least one) have passed since the last wait ended: the frame counts arrive
    // through JUTVideo's queue, posted after each frame is shown.
    void waitForFrames(u16 frames) {
        static u32 nextCount;
        static s8 countInitialized;
        if (!countInitialized) {
            nextCount = NebulaFrameClock::count();
            countInitialized = 1;
        }

        u32 frame;
        do {
            // A message is pointer-sized natively: receive into one, keep the frame count it carries.
            void* message;
            frame = (JUTVideo::getManager()->getMessageQueue())->receive(&message, true)
                        ? (u32)(uintptr_t)message
                        : 0;
        } while ((s32)(frame - nextCount) < 0);
        nextCount = frame + (frames != 0 ? frames : 1);
    }

};  // namespace
