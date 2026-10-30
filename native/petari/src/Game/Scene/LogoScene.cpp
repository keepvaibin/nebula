#include "Game/Scene/LogoScene.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/LogoFader.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#ifdef TARGET_PC
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaRtPrepare.hpp"
#include <cstring>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/pane.h>

namespace {
    // The strap screen's white backing (PicBG) as it shows, under the fader's black (LogoFader::draw blends its rate as
    // a byte): the grey of the borders around a fitted picture.
    f32 getStrapSurround(const SimpleLayout* pStrap, const LogoFader* pFader) {
        if (pStrap == nullptr || pFader == nullptr || pStrap->mFlag.mIsDead || pStrap->mFlag.mIsHidden) {
            return 0.0f;
        }
        const nw4r::lyt::Pane* pRoot = pStrap->mLayoutManager->mLayout->mpRootPane;
        if (pRoot == nullptr || strcmp(pRoot->mName, "RootPane") != 0) {
            return 0.0f;
        }
        bool identified = false;
        const nw4r::lyt::Pane* pBacking = nullptr;
        for (nw4r::lyt::PaneList::ConstIterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
            identified |= strcmp(it->mName, "WiiRemoteStrap") == 0;
            if (strcmp(it->mName, "PicBG") == 0) {
                pBacking = &*it;
            }
        }
        if (!identified || pBacking == nullptr) {
            return 0.0f;
        }
        f32 rate = !pFader->mFlag.mIsDead && !pFader->mFlag.mIsHidden ? pFader->mRate : 0.0f;
        rate = rate < 0.0f ? 0.0f : rate > 1.0f ? 1.0f : rate;
        const u32 fade = static_cast< u32 >(rate * 255.0f);
        return (pBacking->mGlbAlpha / 255.0f) * ((255 - fade) / 255.0f);
    }
};  // namespace
#endif
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"


namespace {
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    const s32 FADEINOUT_FRAME = 60;
#else
    const s32 FADEINOUT_FRAME = 30;
    const s32 CENSORSHIP_DISPLAY_FRAME = 180;
#endif
    const s32 STRAP_DISPLAY_MIN_FRAME = 450;
    const s32 STRAP_DISPLAY_MAX_FRAME = 1200;


    NEW_NERVE(LogoSceneStrapFadein, LogoScene, StrapFadein);
    NEW_NERVE(LogoSceneStrapDisplay, LogoScene, StrapDisplay);
    NEW_NERVE(LogoSceneStrapFadeout, LogoScene, StrapFadeout);
    NEW_NERVE(LogoSceneMountGameData, LogoScene, MountGameData);
#ifdef TARGET_PC
    NEW_NERVE(LogoSceneWaitRayTracing, LogoScene, WaitRayTracing);
#endif
    NEW_NERVE(LogoSceneWaitReadDoneSystemArchive, LogoScene, WaitReadDoneSystemArchive);
    NEW_NERVE(LogoSceneDeactive, LogoScene, Deactive);
};  // namespace

LogoScene::LogoScene()
    : Scene("LogoScene"),
      mStrapLayout(), mLogoFader() {
    MainLoopFramework::getManager()->mUseVFilter = false;
    MainLoopFramework::getManager()->mUseAlpha = false;
}

LogoScene::~LogoScene() {
    MainLoopFramework::getManager()->mUseVFilter = true;
    MainLoopFramework::getManager()->mUseAlpha = true;
}

void LogoScene::init() {
#ifdef TARGET_PC
        // The console's strap screen covers the system archive's load and asks for a button press. Natively the load
        // runs behind black (the fader starts blank) and the title follows as soon as it and the save data are ready.
        initNerve(GET_NERVE_ANON(LogoSceneWaitReadDoneSystemArchive));
#else
        initNerve(GET_NERVE_ANON(LogoSceneStrapFadein));
#endif

    SceneFunction::createHioBasicNode(this);
    SceneFunction::initForNameObj();
    MR::createSceneObj(SceneObj_CameraContext);
    MR::createSceneObj(SceneObj_NameObjGroup);
    initLayout();
}

void LogoScene::update() {
    GameSystemFunction::restartControllerLeaveWatcher();
    updateNerve();
    SceneFunction::executeMovementList();
}

void LogoScene::calcAnim() {
    SceneFunction::executeCalcAnimList();
    SceneFunction::executeCalcViewAndEntryList2D();
}

void LogoScene::draw() const {
#ifdef TARGET_PC
    __NebulaSceneSurround(getStrapSurround(mStrapLayout, mLogoFader));
#endif
    MR::drawInit();

    GXColor color;
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01)
    color.r = 0;
    color.g = 0;
    color.b = 0;
#elif (VERSION == RMGK01)
    color.r = 255;
    color.g = 255;
    color.b = 255;
#endif
    color.a = 255;

    MR::fillScreen(color);
    MR::clearZBuffer();
    MR::drawInitFor2DModel();


    CategoryList::drawOpa(MR::DrawBufferType_Model3DFor2D);
    CategoryList::drawXlu(MR::DrawBufferType_Model3DFor2D);
    CategoryList::execute(MR::DrawType_Layout);
    CategoryList::execute(MR::DrawType_EffectDraw2D);
    CategoryList::execute(MR::DrawType_EffectDrawFor2DModel);
    CategoryList::execute(MR::DrawType_CometScreenFilter);
    CategoryList::execute(MR::DrawType_WipeLayout);
}

bool LogoScene::isDisplayStrapRemineder() const {
    return isNerve(GET_NERVE_ANON(LogoSceneDeactive)) ||
           isNerve(GET_NERVE_ANON(LogoSceneStrapFadein)) || isNerve(GET_NERVE_ANON(LogoSceneStrapDisplay)) ||
           isNerve(GET_NERVE_ANON(LogoSceneStrapFadeout)) || isNerve(GET_NERVE_ANON(LogoSceneWaitReadDoneSystemArchive));
}


void LogoScene::exeStrapFadein() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(mStrapLayout, "Strap", 0);
        MR::setAnimFrameAndStop(mStrapLayout, 0.0f, 0);

        mLogoFader->mMaxStep = FADEINOUT_FRAME;
    }

    if (tryFadeinLayout(mStrapLayout)) {
        setNerve(GET_NERVE_ANON(LogoSceneStrapDisplay));
    }
}

void LogoScene::exeStrapDisplay() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(mStrapLayout, "Strap", 0);
    }

    if (MR::isStep(this, STRAP_DISPLAY_MAX_FRAME - 1)) {
        MR::setAnimRate(mStrapLayout, 0.0f, 0);
    }

    if (MR::isGreaterStep(this, STRAP_DISPLAY_MIN_FRAME) &&
        (MR::testCorePadTriggerAnyWithoutHome(WPAD_CHAN0) || MR::isGreaterEqualStep(this, STRAP_DISPLAY_MAX_FRAME))) {
        setNerve(GET_NERVE_ANON(LogoSceneStrapFadeout));
    }
}

void LogoScene::exeStrapFadeout() {
    if (tryFadeoutLayout(mStrapLayout)) {
        setNerve(GET_NERVE_ANON(LogoSceneWaitReadDoneSystemArchive));
    }
}

void LogoScene::exeMountGameData() {
    if (MR::isFirstStep(this)) {
        GameSequenceFunction::startPreLoadSaveDataSequence();
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

#ifdef TARGET_PC
    setNerve(GET_NERVE_ANON(LogoSceneWaitRayTracing));
#else
    setNerve(GET_NERVE_ANON(LogoSceneDeactive));
#endif
}

#ifdef TARGET_PC
// Ray tracing's shaders compile before the title (NebulaRtPrepare): the title waits for them and their screen.
void LogoScene::exeWaitRayTracing() {
    if (!NebulaRtPrepare::isHolding()) {
        setNerve(GET_NERVE_ANON(LogoSceneDeactive));
    }
}
#endif

void LogoScene::exeWaitReadDoneSystemArchive() {
    if (GameSystemFunction::isDoneLoadSystemArchive()) {
        setNerve(GET_NERVE_ANON(LogoSceneMountGameData));
    }
}

void LogoScene::exeDeactive() {
    if (MR::isFirstStep(this)) {
        GameSequenceFunction::notifyToGameSequenceProgressToEndScene();
    }
}

void LogoScene::initLayout() {
    mStrapLayout = MR::createSimpleLayout("\203\130\203\147\203\211\203\142\203\166\222\205\227\160\211\346\226\312" /* ストラップ着用画面 */, "WiiRemoteStrap", 1);
    mStrapLayout->kill();

    mLogoFader = new LogoFader("\203\215\203\123\203\164\203\106\201\133\203\137" /* ロゴフェーダ */);
    mLogoFader->initWithoutIter();
    mLogoFader->setBlank();
    mLogoFader->appear();
    MR::connectToSceneLayout(mLogoFader);

}

bool LogoScene::tryFadeinLayout(LayoutActor* pActor) {
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01 || VERSION == RMGP01)
    if (MR::isFirstStep(this)) {
        mLogoFader->startFadeIn();
        pActor->appear();
    }

    if (mLogoFader->isFadeEnd()) {
        return true;
    }

    return false;
#else
    if (MR::isFirstStep(this)) {
        pActor->appear();
    }

    if (tryFadeinLayout()) {
        return true;
    }

    return false;
#endif
}

bool LogoScene::tryFadeoutLayout(LayoutActor* pActor) {
#if (VERSION == RMGJ01 || VERSION == RMGE01 || VERSION == RMGP01 || VERSION == RMGP01)
    if (MR::isFirstStep(this)) {
        mLogoFader->startFadeOut();
    }

    if (mLogoFader->isFadeEnd()) {
        pActor->kill();
        return true;
    }

    return false;
#else
    if (tryFadeoutLayout()) {
        pActor->kill();
        return true;
    }

    return false;
#endif
}

