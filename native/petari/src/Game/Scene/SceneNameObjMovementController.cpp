#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Camera/CameraContext.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/SensorHitChecker.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Map/LightDirector.hpp"
#include "Game/Map/SleepControllerHolder.hpp"
#include "Game/Map/SwitchWatcherHolder.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/NPC/TalkDirector.hpp"
#include "Game/NameObj/NameObjGroup.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/CinemaFrame.hpp"
#include "Game/Screen/InformationObserver.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    void executeStopCategoriesDemoCommon() {
        MR::suspendAllSceneNameObj();
        MR::getSceneObj< NameObjGroup >(SceneObj_NameObjGroup)->pauseOffAll();
        MR::notifyRequestNameObjMovementOnOff();
        MR::requestEffectStopSceneStart();
        CategoryList::requestMovementOn(MR::MovementType_EffectUpdateCheck);
        CategoryList::requestMovementOn(MR::MovementType_ShadowControllerHolder);
        CategoryList::requestMovementOn(MR::MovementType_LayoutOnPause);
        MR::pauseOffTalkDirector();
        MR::requestMovementOn(MR::getSceneObj< NameObj >(SceneObj_SensorHitChecker));
        MR::requestMovementOn(MR::getSceneObj< NameObj >(SceneObj_CameraContext));
        MR::requestMovementOn(MR::getSceneObj< NameObj >(SceneObj_ClippingDirector));
        MR::requestMovementOn(MR::getSceneObj< NameObj >(SceneObj_LightDirector));
        WaterAreaFunction::pauseOffWaterArea();

        if (MR::isExistSceneObj(SceneObj_CollisionDirector)) {
            MR::requestMovementOn(MR::getSceneObj< CollisionDirector >(SceneObj_CollisionDirector));
        }

        CategoryList::requestMovementOn(MR::MovementType_DemoDirector);
        MR::requestMovementOn(MR::getSceneObj< NameObj >(SceneObj_CinemaFrame));
        MR::requestMovementOn(MR::getSceneObj< NameObj >(SceneObj_InformationObserver));
        MR::requestMovementOn(MR::getSceneObj< NameObj >(SceneObj_SleepControllerHolder));
        CategoryList::requestMovementOn(MR::MovementType_AudCameraWatcher);
        CategoryList::requestMovementOn(MR::MovementType_AudEffectDirector);
    }
};  // namespace

namespace {
    NEW_NERVE(StopSceneStateControlNone, StopSceneStateControl, None);
    NEW_NERVE(StopSceneStateControlStopped, StopSceneStateControl, Stopped);
};  // namespace

StopSceneStateControl::StopSceneStateControl() : NerveExecutor("StopSceneStateControl") {
    initNerve(GET_NERVE_ANON(StopSceneStateControlNone));

    mMovementType = MR::MovementControlType_Resume;
    mRequester = nullptr;
}

// A stop while stopped is ignored.
void StopSceneStateControl::requestStopSceneFor(MR::MovementControlType type, const NameObj* pRequester) {
    if (isNerve(GET_NERVE_ANON(StopSceneStateControlNone))) {
        mMovementType = type;
        mRequester = pRequester;

        setNerve(GET_NERVE_ANON(StopSceneStateControlStopped));
        executeStopCategories(type);
    }
}

void StopSceneStateControl::requestStopSceneOverwrite(const NameObj* pRequester) {
    if (isNerve(GET_NERVE_ANON(StopSceneStateControlStopped))) {
        mRequester = pRequester;
        mMovementType = MR::MovementControlType_DemoWithCameraDirector;

        setNerve(GET_NERVE_ANON(StopSceneStateControlStopped));
        executeStopCategories(MR::MovementControlType_DemoWithCameraDirector);
    }
}

// A play request in the step a stop began is dropped.
void StopSceneStateControl::requestPlaySceneFor(MR::MovementControlType type, const NameObj* pRequester) {
    if (!MR::isNewNerve(this) && isNerve(GET_NERVE_ANON(StopSceneStateControlStopped))) {
        mMovementType = type;
        mRequester = pRequester;

        setNerve(GET_NERVE_ANON(StopSceneStateControlNone));
        executeStopCategories(MR::MovementControlType_Resume);
    }
}

void StopSceneStateControl::executeStopCategories(MR::MovementControlType type) {
    switch (type) {
    case MR::MovementControlType_Resume:
        MR::resumeAllSceneNameObj();
        MR::notifyRequestNameObjMovementOnOff();
        MR::requestEffectStopSceneEnd();
        break;
    case MR::MovementControlType_Demo:
        ::executeStopCategoriesDemoCommon();
        MR::requestMovementOnSwitchWatcher();
        break;
    case MR::MovementControlType_DemoMarioPuppetable:
        ::executeStopCategoriesDemoCommon();
        MR::requestMovementOnSwitchWatcher();
        CategoryList::requestMovementOn(MR::MovementType_Player);
        CategoryList::requestMovementOn(MR::MovementType_PlayerDecoration);
        MR::requestMovementOnImageEffect();
        break;
    case MR::MovementControlType_StopEnemy:
        CategoryList::requestMovementOff(MR::MovementType_Enemy);
        CategoryList::requestMovementOff(MR::MovementType_EnemyDecoration);
        CategoryList::requestMovementOff(MR::MovementType_CollisionEnemy);
        break;
    case MR::MovementControlType_StopPlayer:
        CategoryList::requestMovementOff(MR::MovementType_Player);
        CategoryList::requestMovementOff(MR::MovementType_PlayerDecoration);
        break;
    case MR::MovementControlType_DemoWithCameraDirector:
        ::executeStopCategoriesDemoCommon();
        MR::requestMovementOn(MR::getSceneObj< CameraDirector >(SceneObj_CameraDirector));
        break;
    default:
        break;
    }
}

void StopSceneStateControl::exeNone() {
}

void StopSceneStateControl::exeStopped() {
}

SceneNameObjMovementController::SceneNameObjMovementController()
    : NameObj("SceneNameObjMovementController"), mIsSyncRequested(false), mStopSceneStateControl(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< SceneNameObjMovementController >);
    mStopSceneStateControl = new StopSceneStateControl();
}

void SceneNameObjMovementController::requestStopSceneFor(MR::MovementControlType type, const NameObj* pRequester) {
    mStopSceneStateControl->requestStopSceneFor(type, pRequester);
}

void SceneNameObjMovementController::requestStopSceneOverwrite(const NameObj* pRequester) {
    mStopSceneStateControl->requestStopSceneOverwrite(pRequester);
}

void SceneNameObjMovementController::requestPlaySceneFor(MR::MovementControlType type, const NameObj* pRequester) {
    mStopSceneStateControl->requestPlaySceneFor(type, pRequester);
}

void SceneNameObjMovementController::movement() {
    if (mIsSyncRequested) {
        MR::syncWithFlagsAllSceneNameObj();

        mIsSyncRequested = false;
    }

    mStopSceneStateControl->updateNerve();
}

void SceneNameObjMovementController::notifyRequestNameObjMovementOnOff(bool isRequested) {
    mIsSyncRequested = isRequested;
}

namespace MR {
    SceneNameObjMovementController* getSceneNameObjMovementController() {
        return getSceneObj< SceneNameObjMovementController >(SceneObj_SceneNameObjMovementController);
    }

    void notifyRequestNameObjMovementOnOff() {
        getSceneNameObjMovementController()->notifyRequestNameObjMovementOnOff(true);
    }
};  // namespace MR
