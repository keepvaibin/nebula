#include "Game/Map/Air.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvAir {
    NEW_NERVE(HostTypeIn, Air, In);
    NEW_NERVE(HostTypeOut, Air, Out);
};  // namespace NrvAir

Air::Air(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< Air >);
    mIsChanged = 0;
    mIsWaitStartCamera = 0;
    mDistance = 70.0f;
}

void Air::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* object_name = 0;
    MR::getObjectName(&object_name, rIter);
    initModel(object_name);
    MR::connectToSceneAir(this);
    MR::useStageSwitchReadA(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mDistance);
    s32 waitStartCamera = -1;
    MR::getPlacementArg1NoInit(rIter, &waitStartCamera);
    mIsWaitStartCamera = waitStartCamera + 1;
    MR::tryStartAllAnim(this, object_name);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    setFarClipping();
    MR::setGroupClipping(this, rIter, 0x10);
    initNerve(GET_NERVE(Air, HostTypeOut));
    MR::registerDemoSimpleCastAll(this);
    bool registered = MR::tryRegisterDemoCast(this, rIter);

    if (registered) {
        // Hidden until a demo fades it out or in.
        const bool isDemoAction = MR::tryRegisterDemoActionFunctor(this, MR::Functor(this, &Air::appearFadeOut), "消滅") ||
                                  MR::tryRegisterDemoActionFunctor(this, MR::Functor(this, &Air::appearFadeIn), nullptr);

        if (isDemoAction) {
            makeActorDead();
        }
    } else {
        appear();
    }
}

void Air::appear() {
    LiveActor::appear();

    f32 appearDist = 100.0f * mDistance;
    f32 dist = MR::calcDistanceToPlayer(this);

    if (dist < appearDist) {
        MR::tryStartAllAnim(this, "Appear");
        MR::setAllAnimFrameAtEnd(this, "Appear");
        setNerve(GET_NERVE(Air, HostTypeIn));
    } else {
        MR::tryStartAllAnim(this, "Disappear");
        MR::setAllAnimFrameAtEnd(this, "Disappear");
        MR::hideModel(this);
        setNerve(GET_NERVE(Air, HostTypeOut));
    }
}

bool Air::isDrawing() const {
    if (!MR::isDead(this)) {
        return !MR::isHiddenModel(this);
    }

    return false;
}

void Air::initModel(const char* pModelName) {
    initModelManagerWithAnm(pModelName, 0, false);
}

bool Air::tryChange() {
    if (!MR::isValidSwitchA(this)) {
        return false;
    }

    if (mIsChanged) {
        return false;
    }

    if (MR::isOnSwitchA(this)) {
        MR::tryStartAllAnim(this, "Change");
        mIsChanged = 1;
        return true;
    }

    return false;
}

void Air::appearFadeIn() {
    makeActorAppeared();
    MR::tryStartAllAnim(this, "Appear");
    setNerve(GET_NERVE(Air, HostTypeIn));
}

void Air::appearFadeOut() {
    makeActorAppeared();
    MR::tryStartAllAnim(this, "Disappear");
    setNerve(GET_NERVE(Air, HostTypeOut));
}

void Air::exeIn() {
    bool isCameraNotEnd = mIsWaitStartCamera && !MR::isStartAnimCameraEnd();

    if (!isCameraNotEnd) {
        tryChange();
        f32 disappearDist = (100.0f * (20.0f + mDistance));
        if (disappearDist < MR::calcDistanceToPlayer(this)) {
            MR::tryStartAllAnim(this, "Disappear");
            setNerve(GET_NERVE(Air, HostTypeOut));
        }
    }
}

void Air::exeOut() {
    bool isCameraNotEnd = mIsWaitStartCamera && !MR::isStartAnimCameraEnd();

    if (!isCameraNotEnd) {
        tryChange();

        if (!MR::isHiddenModel(this) && MR::isAnyAnimStopped(this, "Disappear")) {
            MR::hideModel(this);
        }

        f32 distMult = (100.0f * mDistance);

        if (MR::calcDistanceToPlayer(this) < distMult) {
            MR::showModel(this);
            MR::tryStartAllAnim(this, "Appear");
            setNerve(GET_NERVE(Air, HostTypeIn));
        }
    }
}

ProjectionMapAir::ProjectionMapAir(const char* pName) : Air(pName) {
    lumacore::setClass(this, lumacore::kClassId< ProjectionMapAir >);
}

void ProjectionMapAir::initModel(const char* pModelName) {
    initModelManagerWithAnm(pModelName, nullptr, true);
    ProjmapEffectMtxSetter* mtxSetter = MR::initMaterialProjmapEffectMtxSetter(this);
    MR::prepareMaterials(this);
    mtxSetter->updateMtxUseBaseMtx();
}

void Air::setFarClipping() {
    MR::setClippingFarMax(this);
}

AirFar100m::AirFar100m(const char* pName) : Air(pName) {
    lumacore::setClass(this, lumacore::kClassId< AirFar100m >);
}

void AirFar100m::setFarClipping() {
    MR::setClippingFar100m(this);
}

PriorDrawAir::PriorDrawAir(const char* pName) : Air(pName) {
    lumacore::setClass(this, lumacore::kClassId< PriorDrawAir >);
    MR::createSceneObj(SceneObj_PriorDrawAirHolder);
    MR::getSceneObj< PriorDrawAirHolder >(SceneObj_PriorDrawAirHolder)->add(this);
}

PriorDrawAirHolder::PriorDrawAirHolder() : NameObj("先描画大気保持") {
    lumacore::setClass(this, lumacore::kClassId< PriorDrawAirHolder >);
    mAirCount = 0;
}

void PriorDrawAirHolder::add(PriorDrawAir* pAir) {
    s32 airCnt = mAirCount++;
    mAirs.at(airCnt) = pAir;
}

bool PriorDrawAirHolder::isExistValidDrawAir() const {
    const PriorDrawAir* const* curAir = mAirs.begin();

    for (; curAir != mAirs.begin() + mAirCount; curAir++) {
        if ((*curAir)->isDrawing()) {
            return true;
        }
    }

    return false;
}

namespace MR {
    bool isExistPriorDrawAir() {
        if (!MR::isExistSceneObj(SceneObj_PriorDrawAirHolder)) {
            return false;
        }

        return MR::getSceneObj< PriorDrawAirHolder >(SceneObj_PriorDrawAirHolder)->isExistValidDrawAir();
    }
};  // namespace MR
