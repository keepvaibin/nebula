#include "Game/MapObj/CapsuleCage.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const cDemoCameraName = "注目カメラ";
    const s32 sStepForStartCamera = 50;
    const s32 sStepForEndCamera = NebulaFrameClock::stepsOfSeconds(1);
};  // namespace

namespace NrvCapsuleCage {
    NEW_NERVE(CapsuleCageNrvWait, CapsuleCage, Wait);
    NEW_NERVE(CapsuleCageNrvStartCamera, CapsuleCage, StartCamera);
    NEW_NERVE(CapsuleCageNrvOpen, CapsuleCage, Open);
    NEW_NERVE(CapsuleCageNrvEndCamera, CapsuleCage, EndCamera);
};  // namespace NrvCapsuleCage

CapsuleCage::CapsuleCage(const char* pName) : MapObjActor(pName), mCameraInfo() {
    lumacore::setClass(this, lumacore::kClassId< CapsuleCage >);
}

void CapsuleCage::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupProjmapMtx(false);
    info.setupNerve(GET_NERVE(CapsuleCage, CapsuleCageNrvWait));
    initialize(rIter, info);
    MR::setBodySensorType(this, ATYPE_KEY_SWITCH_AVOID);

    if (!MR::initActorCamera(this, rIter, &mCameraInfo)) {
        mCameraInfo = nullptr;
    }
}

void CapsuleCage::exeWait() {
}

void CapsuleCage::exeStartCamera() {
    if (MR::isFirstStep(this)) {
        MR::startActorCameraTargetSelf(this, mCameraInfo);
    }

    if (MR::isStep(this, ::sStepForStartCamera)) {
        setNerve(GET_NERVE(CapsuleCage, CapsuleCageNrvOpen));
    }
}

void CapsuleCage::exeOpen() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "Move");
        MR::startSound(this, "SE_OJ_CAPSULE_CAGE_BREAK");
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::shakeCameraWeak();
    }

    if (MR::isBckStopped(this)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::shakeCameraWeak();

        if (mCameraInfo != nullptr) {
            setNerve(GET_NERVE(CapsuleCage, CapsuleCageNrvEndCamera));
        } else {
            kill();
        }
    }
}

void CapsuleCage::exeEndCamera() {
    if (MR::isStep(this, ::sStepForEndCamera)) {
        kill();
    }
}

void CapsuleCage::kill() {
    if (mCameraInfo != nullptr) {
        MR::endActorCamera(this, mCameraInfo, false);
        MR::endDemo(this, ::cDemoCameraName);
    }

    MapObjActor::kill();
}

void CapsuleCage::connectToScene(const MapObjActorInitInfo& /*rInfo*/) {
    MR::connectToSceneIndirectMapObj(this);
}

void CapsuleCage::initCaseUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
    MR::listenStageSwitchOnB(this, MR::Functor(this, &CapsuleCage::startOpen));
}

void CapsuleCage::startOpen() {
    MR::invalidateClipping(this);

    if (mCameraInfo != nullptr) {
        MR::requestStartDemoWithoutCinemaFrame(this, ::cDemoCameraName, GET_NERVE(CapsuleCage, CapsuleCageNrvStartCamera), nullptr);
    } else {
        setNerve(GET_NERVE(CapsuleCage, CapsuleCageNrvOpen));
    }
}
