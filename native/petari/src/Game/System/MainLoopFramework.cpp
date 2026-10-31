#include "Game/System/MainLoopFramework.hpp"
#include "Game/System/NebulaKernel.hpp"
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
#include <JSystem/JUtility/JUTVideo.hpp>
#include "Game/Util/RenderSignals.hpp"
#include <runtime.h>
#include "Game/System/NebulaFrameClock.hpp"

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
    void waitForFrames(u16);

};  // namespace

void MainLoopFramework::ctor_subroutine(bool useAlpha) {
    mClearColor = (GXColor){0, 0, 0, 0};
    mClearZ = 0xffffff;
    mDispCopyGamma = GX_GM_1_0;
    mCopyClamp = GX_CLAMP_TOP | GX_CLAMP_BOTTOM;
    mRetraceCount = 1;
    mUseAlpha = useAlpha;
    mUseVFilter = true;
    mCombinationRatio = 0.0f;
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
}

MainLoopFramework* MainLoopFramework::createManager(const GXRenderModeObj* pRenderModeObj, bool useAlpha) {
    if (pRenderModeObj) {
        JUTVideo::getManager()->setRenderMode(pRenderModeObj);
    }
    if (!sManager) {
        sManager = new MainLoopFramework(useAlpha);
    }
    return sManager;
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
        J3DNativeState::showFrame();
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


void MainLoopFramework::clearEfb(GXColor color) {
    u16 fbWidth = JUTVideo::getManager()->getRenderMode()->fbWidth;
    u16 efbHeight = JUTVideo::getManager()->getRenderMode()->efbHeight;
    clearEfb(0, 0, fbWidth, efbHeight, color);
}

void MainLoopFramework::clearEfb(int param1, int param2, int param3, int param4, GXColor color) {
    u16 fbWidth = JUTVideo::getManager()->getRenderMode()->fbWidth;
    u16 efbHeight = JUTVideo::getManager()->getRenderMode()->efbHeight;
    Mtx44 proj;
    NebulaMtxOrtho(proj, 0.0f, efbHeight, 0.0f, fbWidth, 0.0f, 1.0f);
    J3DNativeView::setProjection(proj, GX_ORTHOGRAPHIC);
    J3DNativeView::setViewport(0.0f, 0.0f, fbWidth, efbHeight, 0.0f, 1.0f);
    J3DNativeView::setScissor(0, 0, fbWidth, efbHeight);
    J3DNativeShape::setPosition(0, e_mtx);
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
    NebulaRender::clearQuadColour(0xFFFFFF, (static_cast< u32 >(color.r) << 24) | (static_cast< u32 >(color.g) << 16) |
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
