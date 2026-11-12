#include "Game/Map/PlanetMap.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Map/OceanHomeMapCtrl.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const char* cFollowJointName = "Move";

    const PlanetMapClippingInfo sClippingInfo[] = {{"PhantomShipA", 3000.0f, {800.0f, 1300.0f, 0.0f}}};
    const PlanetMapClippingInfo* findClippingInfo(const char* pName) {
        for (u32 i = 0; i < ARRAY_SIZE(::sClippingInfo); i++) {
            const PlanetMapClippingInfo* pInfo = &::sClippingInfo[i];
            if (MR::isEqualStringCase(pName, pInfo->mName)) {
                return pInfo;
            }
        }

        return nullptr;
    }
};  // namespace

PlanetMap::PlanetMap(const char* pName, const char* pModelName)
    : LiveActor(pName), mModelName(pModelName), mClippingCenter(0.0f, 0.0f, 0.0f), mLODCtrl(), mBloomModel() {
    lumacore::setClass(this, lumacore::kClassId< PlanetMap >);
}

void PlanetMap::init(const TableIter& rIter) {
    MR::getObjectName(&mModelName, rIter);

    MR::initDefaultPos(this, rIter);
    initModel(mModelName, rIter);
    MR::connectToScenePlanet(this);
    initEffectKeeper(0, nullptr);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);

    if (MR::isExistCollisionResource(this, mModelName)) {
        MtxPtr jointMTX = nullptr;
        if (MR::isExistJoint(this, ::cFollowJointName)) {
            jointMTX = MR::getJointMtx(this, ::cFollowJointName);
        }

        MR::initCollisionParts(this, mModelName, getSensor("body"), jointMTX);
    }

    MR::tryCreateCollisionMoveLimit(this, getSensor("body"));
    MR::tryCreateCollisionWaterSurface(this, getSensor("body"));
    initSound(4, false);

    if (MR::isExistAnim(this, mModelName)) {
        MR::startAllAnim(this, mModelName);
    } else if (!MR::isExistEffectTexMtx(this)) {
        calcAnim();
        MR::offCalcAnim(this);
    }

    initClipping(rIter);
    OceanHomeMapFunction::tryEntryOceanHomeMap(this);
    makeActorAppeared();
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    }

    if (MR::tryRegisterDemoCast(this, rIter) && MR::isRegisteredDemoActionAppear(this)) {
        makeActorDead();
    }

    MR::useStageSwitchSleep(this, rIter);
}

void PlanetMap::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    tryEmitMyEffect();
    mLODCtrl->validate();
}

void PlanetMap::makeActorDead() {
    mLODCtrl->invalidate();
    tryDeleteMyEffect();
    LiveActor::makeActorDead();
}

void PlanetMap::initClipping(const TableIter& rIter) {
    f32 radius = 0.0f;
    if (MR::isValidInfo(rIter)) {
        MR::getPlacementArg1NoInit(rIter, &radius);
    }

    const PlanetMapClippingInfo* pInfo = ::findClippingInfo(mModelName);

    if (pInfo != nullptr) {
        radius = pInfo->mRadius;
        mClippingCenter.set(mPosition + Vec3f(pInfo->mOffset));
        MR::setClippingTypeSphere(this, pInfo->mRadius, &mClippingCenter);
    } else {
        if (radius <= 0.0f) {
            MR::calcModelBoundingRadius(&radius, this);
            radius += 100.0f;
        }

        MR::setClippingTypeSphere(this, radius);
    }

    if (MR::isValidInfo(rIter)) {
        MR::setGroupClipping(this, rIter, 128);
    }
}

void PlanetMap::initModel(const char* pModelName, const TableIter& rIter) {
    initModelManagerWithAnm(pModelName, nullptr, false);
    f32 farClippingDistance = getFarClipDistance();
    mLODCtrl = MR::createLodCtrlPlanet(this, rIter, farClippingDistance);
    initBloomModel(pModelName);
    MR::createWaterModel(this, nullptr);
    MR::createIndirectPlanetModel(this, nullptr);
}

void PlanetMap::initBloomModel(const char* pModelName) {
    if (!MR::isExistSubModel(pModelName, "Bloom")) {
        mBloomModel = nullptr;
        return;
    }

    MR::FormattedName str("%sBloom", mModelName);
    const char* name = mName;
    mBloomModel = MR::createModelObjBloomModel(name, str.c_str(), getBaseMtx());
    mBloomModel->mScale.set(mScale);

    f32 radiusBound = 500.0f;
    MR::calcModelBoundingRadius(&radiusBound, this);
    MR::setClippingFarMax(mBloomModel);
    MR::setClippingTypeSphere(mBloomModel, radiusBound);
}

void PlanetMap::control() {
    mLODCtrl->update();
}

void PlanetMap::startClipped() {
    LiveActor::startClipped();
    tryDeleteMyEffect();
}

void PlanetMap::endClipped() {
    LiveActor::endClipped();
    tryEmitMyEffect();
}

bool PlanetMap::tryEmitMyEffect() {
    if (!MR::isRegisteredEffect(this, mModelName)) {
        return false;
    }

    MR::emitEffect(this, mModelName);
    return true;
}

bool PlanetMap::tryDeleteMyEffect() {
    if (!MR::isRegisteredEffect(this, mModelName)) {
        return false;
    }

    MR::deleteEffect(this, mModelName);
    return true;
}

FurPlanetMap::FurPlanetMap(const char* pName) : PlanetMap(pName, nullptr) {
    lumacore::setClass(this, lumacore::kClassId< FurPlanetMap >);
}

void FurPlanetMap::init(const TableIter& rIter) {
    PlanetMap::init(rIter);
    MR::initFurPlanet(this);
}

RailPlanetMap::RailPlanetMap(const char* pName) : PlanetMap(pName, nullptr) {
    lumacore::setClass(this, lumacore::kClassId< RailPlanetMap >);
}

void RailPlanetMap::init(const TableIter& rIter) {
    PlanetMap::init(rIter);
    initRailRider(rIter);
}

f32 PlanetMap::getFarClipDistance() const {
    return -1.0f;
}
