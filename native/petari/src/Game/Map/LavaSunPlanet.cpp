#include "Game/Map/LavaSunPlanet.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/MapObj/MapPartsRotator.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

LavaSunPlanet::LavaSunPlanet(const char* pName) : LiveActor(pName), mRotator(), mObjName(), mLodCtrl() {
    lumacore::setClass(this, lumacore::kClassId< LavaSunPlanet >);
}

void LavaSunPlanet::control() {
    if (mRotator != nullptr) {
        mRotator->movement();
    }

    mLodCtrl->update();
}

void LavaSunPlanet::calcAndSetBaseMtx() {
    if (mRotator == nullptr) {
        LiveActor::calcAndSetBaseMtx();
    } else {
        Pos3f mtx;
        mtx.set(mRotator->getRotateMtx());
        mtx.setTrans(mPosition);
        MR::setBaseTRMtx(this, mtx);
    }
}

void LavaSunPlanet::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getObjectName(&mObjName, rIter);
    initModelManagerWithAnm(mObjName, nullptr, true);
    ProjmapEffectMtxSetter* planetEffect = MR::initMaterialProjmapEffectMtxSetter(this);
    MR::prepareMaterials(this);
    MR::connectToScenePlanet(this);
    initHitSensor(1);
    MR::initCollisionParts(this, mObjName, MR::addBodyMessageSensorMapObj(this), nullptr);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    MR::setClippingFarMax(this);
    mLodCtrl = MR::createLodCtrlPlanet(this, rIter, -1.0f);

    if (MR::isExistAnim(this, mObjName)) {
        MR::startAllAnim(this, mObjName);
    } else if (!MR::isExistEffectTexMtx(this)) {
        calcAnim();
        MR::offCalcAnim(this);
    }

    if (MR::isEqualString(mObjName, "LavaRotatePlanet")) {
        mRotator = new MapPartsRotator(this);
        mRotator->init(rIter);
        mRotator->start();
    }

    makeActorAppeared();
    planetEffect->updateMtxUseBaseMtx();
}
