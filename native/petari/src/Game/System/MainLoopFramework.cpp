#include "Game/System/MainLoopFramework.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <revolution/os.h>
#include "Game/Util/SchedulerUtil.hpp"
#include <JSystem/J2DGraph/J2DOrthoGraph.hpp>
#include <JSystem/JUtility/JUTAssert.hpp>
#include <JSystem/JUtility/JUTConsole.hpp>
#include <JSystem/JUtility/JUTDirectPrint.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>
#include <JSystem/JUtility/JUTXfb.hpp>
#include <revolution/gx/GXRegs.h>
#include <runtime.h>

MainLoopFramework* MainLoopFramework::sManager;


GXTexObj clear_z_tobj;

Mtx e_mtx = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

#ifdef TARGET_PC
// A texture: the GPU reads its bytes in the console's order (A/R pairs, then G/B pairs, of a Z24X8 tile).
u8 clearZTexData[] ATTRIBUTE_ALIGN(32) = {0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                                          0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                          0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#else
u32 clearZTexData[] ATTRIBUTE_ALIGN(32) = {0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF, 0x00FF00FF,
                                           0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
#endif

namespace {
    s32 getDisplayingXfbIndex() NO_INLINE;

    void waitForTick(u32, u16);

};  // namespace

void MainLoopFramework::ctor_subroutine(bool useAlpha) {
    mClearColor = (GXColor){0, 0, 0, 0};
    mClearZ = 0xffffff;
    mDispCopyGamma = GX_GM_1_0;
    mCopyClamp = GX_CLAMP_TOP | GX_CLAMP_BOTTOM;
    _C = 1;
    mRetraceCount = 1;
    mTickDuration = 0;
    mUseAlpha = useAlpha;
    mUseVFilter = true;
    mCombinationRatio = 0.0f;
    mTick = OSGetTick();
    mLastFrameTime = 0;
    mLastVideoTickDelta = 0;
    mSingleBufferIndex = 0;
    _3E = 0;
    J3DNativeTexture::registerImage(clearZTexData, GX_TF_Z24X8, 4, 4, 1);
    J3DNativeTexture::init(&clear_z_tobj, clearZTexData, 4, 4, GX_TF_Z24X8, GX_REPEAT, GX_REPEAT, GX_FALSE);
    J3DNativeTexture::initLOD(&clear_z_tobj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    mRenderInterval = 1;
    mRenderCounter = 0;
    mDoRenderFrame = false;
    mPreRenderCallback = nullptr;
}

MainLoopFramework::~MainLoopFramework() {
    if (JUTVideo::getManager()) {
        waitBlanking(2);
    }
    JUTXfb::destroyManager();
}

MainLoopFramework* MainLoopFramework::createManager(const GXRenderModeObj* pRenderModeObj, void* xfb1, void* xfb2, void* xfb3, bool useAlpha) {
    if (pRenderModeObj) {
        JUTVideo::getManager()->setRenderMode(pRenderModeObj);
    }
    if (!sManager) {
        sManager = new MainLoopFramework(xfb1, xfb2, xfb3, useAlpha);
    }
    return sManager;
}

void callDirectDraw() {
    u16 efbHeight, fbWidth;
    fbWidth = JUTVideo::getManager()->getRenderMode()->fbWidth;
    efbHeight = JUTVideo::getManager()->getRenderMode()->efbHeight;
    void* pXfb = JUTXfb::sManager->getDrawingXfb();
    JUTDirectPrint::sDirectPrint->changeFrameBuffer(pXfb, fbWidth, efbHeight);
    JUTAssertion::flushMessage();
}

#pragma push
#pragma global_optimizer off
void MainLoopFramework::prepareCopyDisp() {
    u16 fbWidth = JUTVideo::getManager()->getRenderMode()->fbWidth;
    u16 efbHeight = JUTVideo::getManager()->getRenderMode()->efbHeight;
    u16 xfbHeight = JUTVideo::getManager()->getRenderMode()->xfbHeight;
    f32 yscale = J3DNativeState::getYScaleFactor(efbHeight, xfbHeight);
    u16 nlines = J3DNativeState::getNumXfbLines(efbHeight, yscale);
    J3DNativeState::setCopyClear(mClearColor, mClearZ);
    J3DNativeState::setDispCopySrc(0, 0, fbWidth, efbHeight);
    J3DNativeState::setDispCopyDst(fbWidth, nlines);
    J3DNativeState::setDispCopyYScale(yscale);
    VIFlush();
    GXRenderModeObj* pMode = JUTVideo::getManager()->getRenderMode();
    u8* pVFilter = pMode->vfilter;
    J3DNativeState::setCopyFilter(pMode->aa, pMode->sample_pattern, mUseVFilter, pVFilter);
    J3DNativeState::setCopyClamp((GXFBClamp)mCopyClamp);
    J3DNativeState::setDispCopyGamma((GXGamma)mDispCopyGamma);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    if (mUseAlpha) {
        J3DNativeState::setAlphaUpdate(GX_TRUE);
    }
}
#pragma pop

void MainLoopFramework::drawendXfb_single() {
    JUTXfb* pXfbMgr = JUTXfb::sManager;
    if (pXfbMgr->mDrawingXfbIndex >= 0) {
        prepareCopyDisp();
        GXDrawDone();
        GXFlush();
        pXfbMgr->mDrawnXfbIndex = pXfbMgr->mDrawingXfbIndex;
    }
}

void MainLoopFramework::exchangeXfb_double() {
    JUTXfb* pXfbMgr = JUTXfb::sManager;
    if (pXfbMgr->mDrawnXfbIndex == pXfbMgr->mDisplayingXfbIndex) {
        if (pXfbMgr->mDrawingXfbIndex >= 0) {
            if (mPreRenderCallback) {
                mPreRenderCallback();
            }
            prepareCopyDisp();
            J3DNativeState::copyDisp(pXfbMgr->getDrawingXfb(), GX_TRUE);
            if (!_C) {
                pXfbMgr->mDrawnXfbIndex = pXfbMgr->mDrawingXfbIndex;
                GXDrawDone();
                JUTVideo::dummyNoDrawWait();
            } else {
                JUTVideo::drawDoneStart();
            }
            if (!_C) {
                callDirectDraw();
            }
        }
        s16 lastIdx = pXfbMgr->mDrawingXfbIndex;
        pXfbMgr->mDrawnXfbIndex = lastIdx;
        pXfbMgr->mDrawingXfbIndex = lastIdx >= 0 ? lastIdx ^ 1 : 0;
    } else {
        clearEfb(mClearColor);
        if (pXfbMgr->mDrawingXfbIndex < 0) {
            pXfbMgr->mDrawingXfbIndex = 0;
        }
    }
}

namespace {
    s32 getDisplayingXfbIndex() {
        return JUTXfb::getManager()->getDisplayingXfbIndex();
    }
};  // namespace

void MainLoopFramework::exchangeXfb_triple() {
    JUTXfb* pXfbMgr = JUTXfb::sManager;
    if (pXfbMgr->mDrawingXfbIndex >= 0) {
        callDirectDraw();
    }
    pXfbMgr->mDrawnXfbIndex = pXfbMgr->mDrawingXfbIndex;
    s16 nextIdx = pXfbMgr->mDrawingXfbIndex + 1;
    do {
        if (nextIdx >= 3 || nextIdx < 0) {
            nextIdx = 0;
        }
#ifdef TARGET_PC
        if (nextIdx == ::getDisplayingXfbIndex()) {
            __NebulaPoll();
        }
#endif
    } while (nextIdx == ::getDisplayingXfbIndex());
    pXfbMgr->mDrawingXfbIndex = nextIdx;
}

void MainLoopFramework::copyXfb_triple() {
    JUTXfb* pXfbMgr = JUTXfb::sManager;
    if (pXfbMgr->mDrawingXfbIndex >= 0) {
        if (mPreRenderCallback) {
            mPreRenderCallback();
        }
        prepareCopyDisp();
        J3DNativeState::copyDisp(pXfbMgr->getDrawingXfb(), GX_FALSE);
    }
}

void MainLoopFramework::preGX() {
    J3DNativeState::invalidateTexAll();
    if (JUTVideo::getManager()->getRenderMode()->aa) {
        J3DNativeState::setPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
        J3DNativeState::setDither(GX_TRUE);
    } else if (mUseAlpha) {
        J3DNativeState::setPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
        J3DNativeState::setDither(GX_TRUE);
    } else {
        J3DNativeState::setPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
        J3DNativeState::setDither(GX_FALSE);
    }
}

void MainLoopFramework::endGX() {
    J2DOrthoGraph ortho(0.0f, 0.0f, JUTVideo::getManager()->getRenderMode()->fbWidth, JUTVideo::getManager()->getRenderMode()->efbHeight, -1.0f,
                        1.0f);
    if (JUTConsoleManager::sManager) {
        ortho.setPort();
        JUTConsoleManager::sManager->draw();
    }
    if (_C || JUTXfb::sManager->mBufferNum == 1) {
        JUTAssertion::flushMessage_dbPrint();
    }
    GXFlush();
}

void MainLoopFramework::waitForRetrace() {
    ::waitForTick(mTickDuration, mRetraceCount);
    JUTVideo::getManager()->waitRetraceIfNeed();
}

void MainLoopFramework::beginRender() {
    OSTick currentTick = OSGetTick();
    mLastFrameTime = currentTick - mTick;
    mTick = currentTick;

    mLastVideoTickDelta = currentTick - JUTVideo::getVideoLastTick();

    if (mDoRenderFrame) {
        JUTXfb* xfb = JUTXfb::sManager;
        switch (xfb->mBufferNum) {
        case 1:
            if (xfb->mSDrawingFlag != 2) {
                xfb->mSDrawingFlag = 1;
                clearEfb(mClearColor);
            } else {
                xfb->mSDrawingFlag = 1;
            }
            xfb->mDrawingXfbIndex = mSingleBufferIndex;
            break;
        case 2:
            exchangeXfb_double();
            break;
        case 3:
            exchangeXfb_triple();
            break;
        }
    }

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
        JUTXfb* xfb = JUTXfb::sManager;
        switch (xfb->mBufferNum) {
        case 1:
            drawendXfb_single();
            break;
        case 2:
            break;
        case 3:
            copyXfb_triple();
            break;
        }
    }
    calcCombinationRatio();
}

void MainLoopFramework::endFrame() {
    if (mDoRenderFrame) {
        JUTXfb* xfb = JUTXfb::sManager;
        switch (xfb->mBufferNum) {
        case 1:
            break;
        case 2:
            GXDrawDone();
            GXFlush();
            break;
        case 3:
            GXDrawDone();
            GXFlush();
            break;
        }
    }

    if (mDoRenderFrame) {
        static s8 lbl_806B6ED4;
        if (!lbl_806B6ED4) {
            VIGetRetraceCount();
            lbl_806B6ED4 = 1;
        }
        VIGetRetraceCount();
    }
}

void MainLoopFramework::waitBlanking(int tickCount) {
    while (tickCount-- > 0) {
        ::waitForTick(mTickDuration, mRetraceCount);
    }
}

void MainLoopFramework::setTickRateFromFrame(u16 frame) {
    mTickDuration = frameToTick(frame);
    mRetraceCount = 0;
}

void MainLoopFramework::clearEfb(GXColor color) {
    u16 fbWidth = JUTVideo::getManager()->getRenderMode()->fbWidth;
    u16 efbHeight = JUTVideo::getManager()->getRenderMode()->efbHeight;
    clearEfb(0, 0, fbWidth, efbHeight, color);
}

void MainLoopFramework::clearEfb(int param1, int param2, int param3, int param4, GXColor color) {
    u16 fbWidth = JUTVideo::getManager()->getRenderMode()->fbWidth;
    u16 efbHeight = JUTVideo::getManager()->getRenderMode()->efbHeight;
    Mtx44 proj;
    C_MTXOrtho(proj, 0.0f, efbHeight, 0.0f, fbWidth, 0.0f, 1.0f);
    J3DNativeView::setProjection(proj, GX_ORTHOGRAPHIC);
    J3DNativeView::setViewport(0.0f, 0.0f, fbWidth, efbHeight, 0.0f, 1.0f);
    J3DNativeView::setScissor(0, 0, fbWidth, efbHeight);
    J3DNativeShape::sendPosition(e_mtx, GX_PNMTX0);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeTexture::load(&clear_z_tobj, GX_TEXMAP0);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevColor(GX_TEVREG0, color);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
    J3DNativeState::setZCompLoc(GX_FALSE);
    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);

    if (mUseAlpha) {
        J3DNativeState::setAlphaUpdate(GX_TRUE);
        J3DNativeState::setDstAlpha(GX_TRUE, color.a);
    }

    J3DNativeState::setZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    J3DNativeState::setCullMode(GX_CULL_BACK);

#ifdef TARGET_PC
    __NebulaGXClearQuadColourMarker(0xFFFFFF, (static_cast< u32 >(color.r) << 24) | (static_cast< u32 >(color.g) << 16) |
                                                  (static_cast< u32 >(color.b) << 8) | color.a);
#endif
    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(param1)), static_cast< f32 >(static_cast< u16 >(param2)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(param1 + param3)), static_cast< f32 >(static_cast< u16 >(param2)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(param1 + param3)), static_cast< f32 >(static_cast< u16 >(param2 + param4)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);

        J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(param1)), static_cast< f32 >(static_cast< u16 >(param2 + param4)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);
    }

    J3DNativeState::setZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
    J3DNativeState::setZCompLoc(GX_TRUE);

    if (mUseAlpha) {
        J3DNativeState::setDstAlpha(GX_FALSE, color.a);
    }
}

void MainLoopFramework::calcCombinationRatio() {
    s32 videoInterval = JUTVideo::getVideoInterval();
    s32 var1 = videoInterval;
    while (var1 < 2 * mLastFrameTime) {
        var1 += videoInterval;
    }
    s32 var2 = var1 - 2 * mLastFrameTime - mLastVideoTickDelta;
    if (var2 < 0) {
        var2 += videoInterval;
    }

    mCombinationRatio = (f32)var2 / (u32)mLastFrameTime;
    if (mCombinationRatio > 1.0f) {
        mCombinationRatio = 1.0f;
    }
}

u32 MainLoopFramework::frameToTick(f32 frame) {
    s32 tick = (s32)((OS_BUS_CLOCK / 4) * frame / 59.94) - 100;
    return (tick < 0) ? 0 : tick;
}

void MainLoopFramework::setForOSResetSystem() {
    JUTVideo::destroyManager();
    VISetBlack(TRUE);
    VIFlush();
    VIWaitForRetrace();
    if (sManager) {
        delete sManager;
        sManager = nullptr;
    }
}

namespace {
    void waitForTick(u32 tickDuration, u16 retraceCount) {
        if (tickDuration) {
            static s64 nextTime;
            static s8 timeInitialized;
            if (!timeInitialized) {
                nextTime = OSGetTime();
                timeInitialized = 1;
            }
            s64 currentTime = OSGetTime();
            while (currentTime < nextTime) {
                OSSleepTicks(nextTime - currentTime);
                currentTime = OSGetTime();
            }
            nextTime = currentTime + tickDuration;
        } else {
            static u32 nextCount;
            static s8 countInitialized;
            if (!countInitialized) {
                nextCount = VIGetRetraceCount();
                countInitialized = 1;
            }

            u32 count = retraceCount ? retraceCount : 1;
            u32 dummy = 0;

            u32 msg;
            do {
#ifdef TARGET_PC
                // An OSMessage is pointer-sized natively: receive into one, keep the retrace count it carries.
                OSMessage message;
                if (!OSReceiveMessage(JUTVideo::getManager()->getMessageQueue(), &message, OS_MESSAGE_BLOCK)) {
                    msg = dummy;
                } else {
                    msg = (u32)(uintptr_t)message;
                }
#else
                if (!OSReceiveMessage(JUTVideo::getManager()->getMessageQueue(), (OSMessage*)&msg, OS_MESSAGE_BLOCK)) {
                    msg = dummy;
                }
#endif
            } while ((s32)(msg - nextCount) < 0);
            nextCount = msg + count;
        }
    }

};  // namespace
