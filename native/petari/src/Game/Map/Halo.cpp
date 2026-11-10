#include "Game/Map/Halo.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isDistanceAppear_f1, "isDistanceAppear.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isDistanceAppear_f2, "isDistanceAppear.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isDistanceAppear_f3, "isDistanceAppear.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isDistanceDisappear_f1, "isDistanceDisappear.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isDistanceDisappear_f2, "isDistanceDisappear.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isDistanceDisappear_f3, "isDistanceDisappear.f3", 100.0f);

namespace NrvHalo {
    NEW_NERVE(HostTypeAppear, Halo, Appear);
    NEW_NERVE(HostTypeDisappear, Halo, Disappear);
};  // namespace NrvHalo

namespace NrvPowerStarHalo {
    NEW_NERVE(HostTypeWaitScenarioOpeningEnd, PowerStarHalo, WaitScenarioOpeningEnd);
};  // namespace NrvPowerStarHalo

namespace {
    static const HaloParam sParams[] = {{"ZoneHalo", 4.0f}, {"PowerStarHalo", 30.0f}};

    static const HaloParam* getParam(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(sParams); i++) {
            if (MR::isEqualString(pName, sParams[i].mName)) {
                return &sParams[i];
            }
        }

        return nullptr;
    }
};  // namespace

Halo::Halo(const char* pName) : MapObjActor(pName), mDistance(70.0f) {
}

void Halo::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupNerve(GET_NERVE(Halo, HostTypeAppear));
    info.setupClippingRadius(::getParam(mObjectName)->mClippingRadius * cNum_init_f1);
    info.setupFarClipping(-1.0f);
    info.setupAffectedScale();
    initialize(rIter, info);
    MR::getPlacementArg0NoInit(rIter, &mDistance);
}

void Halo::appear() {
    MapObjActor::appear();

    if (isDistanceDisappear()) {
        const char* pAnimName = "Disappear";

        MR::tryStartAllAnim(this, pAnimName);

        if (MR::isExistBtk(this, pAnimName)) {
            MR::setBtkFrame(this, MR::getBtkCtrl(this)->getEnd());
        }

        if (MR::isExistBpk(this, pAnimName)) {
            MR::setBpkFrame(this, MR::getBpkCtrl(this)->getEnd());
        }

        if (MR::isExistBtp(this, pAnimName)) {
            MR::setBtpFrame(this, MR::getBtpCtrl(this)->getEnd());
        }

        if (MR::isExistBrk(this, pAnimName)) {
            MR::setBrkFrame(this, MR::getBrkCtrl(this)->getEnd());
        }

        MR::hideModel(this);
        setNerve(GET_NERVE(Halo, HostTypeDisappear));
    } else {
        MR::startAllAnim(this, "Appear");
        setNerve(GET_NERVE(Halo, HostTypeAppear));
    }
}

f32 Halo::getDistance() const {
    return MR::calcDistanceToPlayer(this);
}

void Halo::connectToScene(const MapObjActorInitInfo& /*rIter*/) {
    MR::connectToSceneAir(this);
}

bool Halo::isDistanceAppear() const {
    f32 offset = (mDistance + cNum_isDistanceAppear_f1) * cNum_isDistanceAppear_f2;
    f32 distance = mDistance * cNum_isDistanceAppear_f3;

    if (distance < offset) {
        return offset < getDistance();
    }

    return getDistance() < offset;
}

bool Halo::isDistanceDisappear() const {
    f32 offset = (mDistance + cNum_isDistanceDisappear_f1) * cNum_isDistanceDisappear_f2;
    f32 distance = mDistance * cNum_isDistanceDisappear_f3;

    if (distance < offset) {
        return getDistance() < distance;
    }

    return distance < getDistance();
}

void Halo::exeAppear() {
    if (isDistanceDisappear()) {
        MR::startAllAnim(this, "Disappear");
        setNerve(GET_NERVE(Halo, HostTypeDisappear));
    }
}

void Halo::exeDisappear() {
    if (!MR::isHiddenModel(this)) {
        bool isHideModel;
        const char* pAnimName = "Disappear";

        if (MR::isExistBtk(this, pAnimName) && MR::isBtkStopped(this)) {
            isHideModel = true;
        } else if (MR::isExistBpk(this, pAnimName) && MR::isBpkStopped(this)) {
            isHideModel = true;
        } else if (MR::isExistBtp(this, pAnimName) && MR::isBtpStopped(this)) {
            isHideModel = true;
        } else if (MR::isExistBrk(this, pAnimName) && MR::isBrkStopped(this)) {
            isHideModel = true;
        } else {
            isHideModel = false;
        }

        if (isHideModel) {
            MR::hideModel(this);
        }
    }

    if (isDistanceAppear()) {
        MR::showModel(this);
        MR::startAllAnim(this, "Appear");
        setNerve(GET_NERVE(Halo, HostTypeAppear));
    }
}

PowerStarHalo::PowerStarHalo(const char* pName) : Halo(pName), mAppearDuringOpeningCam(-1) {
}

void PowerStarHalo::init(const TableIter& rIter) {
    Halo::init(rIter);
    MR::getPlacementArg1NoInit(rIter, &mAppearDuringOpeningCam);

    if (mAppearDuringOpeningCam == -1) {
        setNerve(GET_NERVE(PowerStarHalo, HostTypeWaitScenarioOpeningEnd));
    }
}

void PowerStarHalo::appear() {
    if (isNerve(GET_NERVE(PowerStarHalo, HostTypeWaitScenarioOpeningEnd))) {
        makeActorAppeared();
    } else {
        Halo::appear();
    }
}

f32 PowerStarHalo::getDistance() const {
    return mPosition.distance(MR::getCamPos());
}

void PowerStarHalo::exeWaitScenarioOpeningEnd() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
    }

    if (!MR::isStageStateScenarioOpeningCamera()) {
        MR::showModel(this);
        Halo::appear();
    }
}
