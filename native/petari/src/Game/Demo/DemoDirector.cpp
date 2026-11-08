#include "Game/Demo/DemoDirector.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Demo/DemoCastGroupHolder.hpp"
#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Demo/DemoSimpleCastHolder.hpp"
#include "Game/Demo/DemoStartRequestHolder.hpp"
#include "Game/Demo/DemoStartRequestUtil.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

DemoDirector::DemoDirector(const char* pName)
    : NameObj(pName), mIsActive(), mExecutor(), mIsWaitingCamera(), mCastGroupHolder(), mSimpleCastHolder(new DemoSimpleCastHolder(512, 64, 128)), mResourceHolder(),
      mStartRequestHolder(new DemoStartRequestHolder()), mDemoStarter(), mDemoName(), mMovementType(-1), mUseCinemaFrame(true) {
    MR::connectToScene(this, MR::MovementType_DemoDirector, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);

    mCastGroupHolder = new DemoCastGroupHolder();
    mCastGroupHolder->initWithoutIter();

    mCastSubGroupHolder = new DemoCastGroupHolder();
    mCastSubGroupHolder->initWithoutIter();

    mResourceHolder = DemoFunction::loadDemoArchive();
}

void DemoDirector::movement() {
    if (mExecutor != nullptr && !mIsWaitingCamera) {
        mExecutor->movement();
    }

    if (tryStartDemoRequested()) {
        return;
    }

    if (!mIsWaitingCamera) {
        return;
    }

    if (!MR::isCameraInterpolatingNearlyEnd()) {
        return;
    }

    mIsWaitingCamera = false;

    doDemoEndRequest();
}

void DemoDirector::startDemoProgrammable(NameObj* pParam1, const char* pParam2, bool param3, s32 param4) {
    NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_START, 0, nullptr, pParam2);
    startDemo(pParam1, pParam2, param3, param4);
}

void DemoDirector::startDemoTimeKeep(NameObj* pParam1, const char* pParam2, s32 param3, bool param4, const char* pParam5) {
    NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_START, 0, nullptr, pParam2);
    startDemo(pParam1, pParam2, param4, param3);
    startDemoExecutor(pParam1, pParam2, param3, pParam5);
}

void DemoDirector::startDemoExecutor(NameObj* pParam1, const char* pParam2, s32 param3, const char* pParam4) {
    mExecutor = DemoFunction::findDemoExecutor(pParam2);

    if (pParam4 != nullptr) {
        mExecutor->startPart(pParam1, pParam2, pParam4, param3);
    } else {
        mExecutor->start(pParam1, pParam2, param3);
    }
}

const char* DemoDirector::getCurrentDemoName() const {
    if (mIsActive) {
        return mDemoName;
    }

    return nullptr;
}

void DemoDirector::endDemo(NameObj* pParam1, const char* pParam2, bool param3) {
    NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_END, 0, nullptr, pParam2);
    if (DemoStartRequestUtil::isExistStartDemoRequest(mStartRequestHolder)) {
        MR::sendMsgToAllLiveActor(ACTMES_END_DEMO, nullptr);

        mExecutor = nullptr;

        doDemoEndRequest();
        startDemoRequested();
    } else if (param3 && !MR::isCameraInterpolatingNearlyEnd() && mMovementType != 3) {
        mIsWaitingCamera = true;

        MR::sendMsgToAllLiveActor(ACTMES_END_DEMO, nullptr);

        mExecutor = nullptr;

        MR::getSceneObj< SceneNameObjMovementController >(SceneObj_SceneNameObjMovementController)->requestStopSceneOverwrite(pParam1);
    } else {
        MR::sendMsgToAllLiveActor(ACTMES_END_DEMO, nullptr);

        mExecutor = nullptr;

        doDemoEndRequest();
    }
}

bool DemoDirector::isExistTimeKeepDemo(const char* pParam1) const {
    for (int i = 0; i < mCastGroupHolder->getObjNum(); i++) {
        if (MR::isName(mCastGroupHolder->getCastGroup(i), pParam1)) {
            return true;
        }
    }

    return false;
}

bool DemoDirector::registerDemoCast(LiveActor* pActor, const TableIter& rIter) {
    if (!MR::isValidInfo(rIter)) {
        return false;
    }

    s32 demoGroupId = -1;

    if (!MR::getPlacementDemoGroupID(rIter, &demoGroupId)) {
        return false;
    }

    PlacementId idInfo = PlacementId(MR::getDemoGroupID(rIter), rIter);

    if (mCastGroupHolder->tryRegisterDemoActor(pActor, rIter, idInfo)) {
        return true;
    }

    return mCastSubGroupHolder->tryRegisterDemoActor(pActor, rIter, idInfo);
}

bool DemoDirector::registerDemoCast(LiveActor* pActor, const char* pParam2, const TableIter& rIter) {
    if (mCastGroupHolder->tryRegisterDemoActor(pActor, pParam2, rIter)) {
        return true;
    }

    return mCastSubGroupHolder->tryRegisterDemoActor(pActor, pParam2, rIter);
}

void DemoDirector::registerDemoSimpleCast(LiveActor* pActor) {
    mSimpleCastHolder->registerActor(pActor);
}

void DemoDirector::registerDemoSimpleCast(LayoutActor* pActor) {
    mSimpleCastHolder->registerActor(pActor);
}

void DemoDirector::registerDemoSimpleCast(NameObj* pObj) {
    mSimpleCastHolder->registerNameObj(pObj);
}

bool DemoDirector::tryStartDemoRequested() {
    if (!mStartRequestHolder->isExistRequest()) {
        return false;
    }

    if (!MR::canStartDemo()) {
        return false;
    }

    startDemoRequested();

    return true;
}

void DemoDirector::startDemo(NameObj* pObj, const char* pParam2, bool param3, s32 param4) {
    mDemoStarter = pObj;
    mIsActive = true;
    mDemoName = pParam2;
    mMovementType = param4;
    mUseCinemaFrame = param3;

    mSimpleCastHolder->movementOnAllCasts();
    MR::initStarPieceGetCSSound();
}

void DemoDirector::startDemoRequested() {
    DemoStartRequestUtil::startDemo(mStartRequestHolder);

    const DemoStartInfo& info = *mStartRequestHolder->getCurrentInfo();

    startDemo(DemoStartRequestUtil::getDemoStarter(info), info.mDemoName, info.mFrameType == 0, info.mMovementType);
    DemoStartRequestUtil::popStartDemoRequest(mStartRequestHolder);
}

void DemoDirector::doDemoEndRequest() {
    MR::getSceneNameObjMovementController()->requestPlaySceneFor(MR::MovementControlType(mMovementType), mDemoStarter);
    MR::activateDefaultGameLayout();
    MR::endStarPointerMode(mDemoStarter);

    if (mUseCinemaFrame) {
        MR::tryFrameToScreenCinemaFrame();
    }

    if (mMovementType == MR::MovementControlType_2 || mMovementType == MR::MovementControlType_3) {
        MarioAccess::endRemoteDemo(nullptr);
    }

    mIsActive = false;
}
