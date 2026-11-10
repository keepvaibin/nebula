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
#include "Game/System/LumaCore.hpp"

DemoDirector::DemoDirector(const char* pName)
    : NameObj(pName), mIsActive(), mExecutor(), mIsWaitingCamera(), mCastGroupHolder(), mSimpleCastHolder(new DemoSimpleCastHolder(512, 64, 128)), mResourceHolder(),
      mStartRequestHolder(new DemoStartRequestHolder()), mDemoStarter(), mDemoName(), mMovementType(MR::MovementControlType_None), mUseCinemaFrame(true) {
    lumacore::setClass(this, lumacore::kClassId< DemoDirector >);
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

void DemoDirector::startDemoProgrammable(NameObj* pStarter, const char* pDemoName, bool useCinemaFrame, MR::MovementControlType movementType) {
    NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_START, 0, nullptr, pDemoName);
    startDemo(pStarter, pDemoName, useCinemaFrame, movementType);
}

void DemoDirector::startDemoTimeKeep(NameObj* pStarter, const char* pDemoName, MR::MovementControlType movementType, bool useCinemaFrame,
                                     const char* pPartName) {
    NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_START, 0, nullptr, pDemoName);
    startDemo(pStarter, pDemoName, useCinemaFrame, movementType);
    startDemoExecutor(pStarter, pDemoName, movementType, pPartName);
}

void DemoDirector::startDemoExecutor(NameObj* pStarter, const char* pDemoName, MR::MovementControlType movementType, const char* pPartName) {
    mExecutor = DemoFunction::findDemoExecutor(pDemoName);

    if (pPartName != nullptr) {
        mExecutor->startPart(pStarter, pDemoName, pPartName, movementType);
    } else {
        mExecutor->start(pStarter, pDemoName, movementType);
    }
}

const char* DemoDirector::getCurrentDemoName() const {
    if (mIsActive) {
        return mDemoName;
    }

    return nullptr;
}

void DemoDirector::endDemo(NameObj* pRequester, const char* pDemoName, bool waitCameraInterpolating) {
    NebulaMods::fire(NEBULA_MOD_EVENT_CUTSCENE_END, 0, nullptr, pDemoName);
    if (DemoStartRequestUtil::isExistStartDemoRequest(mStartRequestHolder)) {
        MR::sendMsgToAllLiveActor(ACTMES_END_DEMO, nullptr);

        mExecutor = nullptr;

        doDemoEndRequest();
        startDemoRequested();
    } else if (waitCameraInterpolating && !MR::isCameraInterpolatingNearlyEnd() && mMovementType != MR::MovementControlType_StopEnemy) {
        mIsWaitingCamera = true;

        MR::sendMsgToAllLiveActor(ACTMES_END_DEMO, nullptr);

        mExecutor = nullptr;

        MR::getSceneObj< SceneNameObjMovementController >(SceneObj_SceneNameObjMovementController)->requestStopSceneOverwrite(pRequester);
    } else {
        MR::sendMsgToAllLiveActor(ACTMES_END_DEMO, nullptr);

        mExecutor = nullptr;

        doDemoEndRequest();
    }
}

bool DemoDirector::isExistTimeKeepDemo(const char* pDemoName) const {
    for (int i = 0; i < mCastGroupHolder->getObjNum(); i++) {
        if (MR::isName(mCastGroupHolder->getCastGroup(i), pDemoName)) {
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

    PlacementId idInfo = PlacementId(demoGroupId, rIter);

    if (mCastGroupHolder->tryRegisterDemoActor(pActor, rIter, idInfo)) {
        return true;
    }

    return mCastSubGroupHolder->tryRegisterDemoActor(pActor, rIter, idInfo);
}

bool DemoDirector::registerDemoCast(LiveActor* pActor, const char* pDemoName, const TableIter& rIter) {
    if (mCastGroupHolder->tryRegisterDemoActor(pActor, pDemoName, rIter)) {
        return true;
    }

    return mCastSubGroupHolder->tryRegisterDemoActor(pActor, pDemoName, rIter);
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

void DemoDirector::startDemo(NameObj* pObj, const char* pDemoName, bool useCinemaFrame, MR::MovementControlType movementType) {
    mDemoStarter = pObj;
    mIsActive = true;
    mDemoName = pDemoName;
    mMovementType = movementType;
    mUseCinemaFrame = useCinemaFrame;

    mSimpleCastHolder->movementOnAllCasts();
    MR::initStarPieceGetCSSound();
}

void DemoDirector::startDemoRequested() {
    DemoStartRequestUtil::startDemo(mStartRequestHolder);

    const DemoStartInfo& info = *mStartRequestHolder->getCurrentInfo();

    startDemo(DemoStartRequestUtil::getDemoStarter(info), info.mDemoName, info.mFrameType == DemoStartInfo::CinemaFrameType_Frame, info.mMovementType);
    DemoStartRequestUtil::popStartDemoRequest(mStartRequestHolder);
}

void DemoDirector::doDemoEndRequest() {
    MR::getSceneNameObjMovementController()->requestPlaySceneFor(mMovementType, mDemoStarter);
    MR::activateDefaultGameLayout();
    MR::endStarPointerMode(mDemoStarter);

    if (mUseCinemaFrame) {
        MR::tryFrameToScreenCinemaFrame();
    }

    if (MR::isMarioRemoteDemo(mMovementType)) {
        MarioAccess::endRemoteDemo(nullptr);
    }

    mIsActive = false;
}
