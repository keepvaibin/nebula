#include "Game/Scene/LogoScene.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/LogoFader.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Screen/NebulaRtPrepare.hpp"

#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaHost.hpp"


namespace {
    NEW_NERVE(LogoSceneMountGameData, LogoScene, MountGameData);
    NEW_NERVE(LogoSceneWaitRayTracing, LogoScene, WaitRayTracing);
    NEW_NERVE(LogoSceneWaitReadDoneSystemArchive, LogoScene, WaitReadDoneSystemArchive);
    NEW_NERVE(LogoSceneDeactive, LogoScene, Deactive);
};  // namespace

LogoScene::LogoScene()
    : Scene("LogoScene"),
      mLogoFader() {
    MainLoopFramework::getManager()->mUseAlpha = false;
}

LogoScene::~LogoScene() {
    MainLoopFramework::getManager()->mUseAlpha = true;
}

void LogoScene::init() {
        // The system archive's load runs behind black (the fader starts blank) and the title follows as soon as it and the save data are ready.
        initNerve(GET_NERVE_ANON(LogoSceneWaitReadDoneSystemArchive));

    SceneFunction::createHioBasicNode(this);
    SceneFunction::initForNameObj();
    MR::createSceneObj(SceneObj_CameraContext);
    MR::createSceneObj(SceneObj_NameObjGroup);
    initLayout();
}

void LogoScene::update() {
    updateNerve();
    SceneFunction::executeMovementList();
}

void LogoScene::calcAnim() {
    SceneFunction::executeCalcAnimList();
    SceneFunction::executeCalcViewAndEntryList2D();
}

void LogoScene::draw() const {
    MR::drawInit();

    NebulaColor8 color;
    color.r = 0;
    color.g = 0;
    color.b = 0;
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

bool LogoScene::isBeforeTitle() const {
    return isNerve(GET_NERVE_ANON(LogoSceneDeactive)) || isNerve(GET_NERVE_ANON(LogoSceneWaitReadDoneSystemArchive));
}


void LogoScene::exeMountGameData() {
    if (MR::isFirstStep(this)) {
        GameSequenceFunction::startPreLoadSaveDataSequence();
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    setNerve(GET_NERVE_ANON(LogoSceneWaitRayTracing));
}

// Ray tracing's shaders compile before the title (NebulaRtPrepare): the title waits for them and their screen.
void LogoScene::exeWaitRayTracing() {
    if (!NebulaRtPrepare::isHolding()) {
        setNerve(GET_NERVE_ANON(LogoSceneDeactive));
    }
}

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
    mLogoFader = new LogoFader("\203\215\203\123\203\164\203\106\201\133\203\137" /* ロゴフェーダ */);
    mLogoFader->initWithoutIter();
    mLogoFader->setBlank();
    mLogoFader->appear();
    MR::connectToSceneLayout(mLogoFader);

}

