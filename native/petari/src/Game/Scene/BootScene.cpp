#include "Game/Scene/BootScene.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/GameFrame.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Screen/NebulaRtPrepare.hpp"

#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/NebulaHost.hpp"


namespace {
    NEW_NERVE(BootSceneMountGameData, BootScene, MountGameData);
    NEW_NERVE(BootSceneWaitRayTracing, BootScene, WaitRayTracing);
    NEW_NERVE(BootSceneWaitReadDoneSystemArchive, BootScene, WaitReadDoneSystemArchive);
    NEW_NERVE(BootSceneDeactive, BootScene, Deactive);
};  // namespace

BootScene::BootScene()
    : Scene("BootScene") {
}

BootScene::~BootScene() {
}

void BootScene::init() {
        // The system archive's load runs behind black (the fader starts blank) and the title follows as soon as it and the save data are ready.
        initNerve(GET_NERVE_ANON(BootSceneWaitReadDoneSystemArchive));

    SceneFunction::initForNameObj();
    MR::createSceneObj(SceneObj_CameraContext);
    MR::createSceneObj(SceneObj_NameObjGroup);
}

void BootScene::update() {
    updateNerve();
    SceneFunction::executeMovementList();
}

void BootScene::calcAnim() {
    SceneFunction::executeCalcAnimList();
    SceneFunction::executeCalcViewAndEntryList2D();
}

void BootScene::draw() const {
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

bool BootScene::isBeforeTitle() const {
    return isNerve(GET_NERVE_ANON(BootSceneDeactive)) || isNerve(GET_NERVE_ANON(BootSceneWaitReadDoneSystemArchive));
}


void BootScene::exeMountGameData() {
    if (MR::isFirstStep(this)) {
        GameSequenceFunction::startPreLoadSaveDataSequence();
    }

    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    setNerve(GET_NERVE_ANON(BootSceneWaitRayTracing));
}

// Ray tracing's shaders compile before the title (NebulaRtPrepare): the title waits for them and their screen.
void BootScene::exeWaitRayTracing() {
    if (!NebulaRtPrepare::isHolding()) {
        setNerve(GET_NERVE_ANON(BootSceneDeactive));
    }
}

void BootScene::exeWaitReadDoneSystemArchive() {
    if (GameSystemFunction::isDoneLoadSystemArchive()) {
        setNerve(GET_NERVE_ANON(BootSceneMountGameData));
    }
}

void BootScene::exeDeactive() {
    if (MR::isFirstStep(this)) {
        GameSequenceFunction::notifyToGameSequenceProgressToEndScene();
    }
}
