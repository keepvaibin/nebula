#include "Game/MapObj/LavaHomeSeesaw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/MapObj/LavaHomeSeesawRotator.hpp"
#include "Game/MapObj/MapPartsFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_geometry.hpp>

#include "math_types.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initAfterPlacement_f1, "initAfterPlacement.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 0.1f);

LavaHomeSeesaw::LavaHomeSeesaw(const char* pName) : LiveActor(pName), mSeesawRotator(), mMtxSetter(), mGroundPos(gZeroVec), mYOffset() {
    lumacore::setClass(this, lumacore::kClassId< LavaHomeSeesaw >);
}

void LavaHomeSeesaw::initAfterPlacement() {
    Vec3f gravity(mGravity);
    gravity *= cNum_initAfterPlacement_f1;
    if (!MR::getFirstPolyOnLineToMap(&mGroundPos, nullptr, mPosition, gravity)) {
        MR::calcMapGroundUpper(&mGroundPos, this);
    }

    mYOffset = mPosition.distance(mGroundPos);
    if ((mPosition - mGroundPos).dot(mGravity) < 0.0f) {
        mYOffset *= -1.0f;
    }

    Vec3f uselessVec(0.0f, mYOffset, 0.0f);
    mMtxSetter->updateMtxUseBaseMtxWithLocalOffset(Vec3f(0.0f, mYOffset, 0.0f));
    MR::offCalcGravity(this);
}

void LavaHomeSeesaw::control() {
    if (mSeesawRotator != nullptr && mSeesawRotator->isWorking()) {
        mSeesawRotator->movement();
    }
}

void LavaHomeSeesaw::calcAndSetBaseMtx() {
    if (mSeesawRotator == nullptr || !mSeesawRotator->isWorking()) {
        LiveActor::calcAndSetBaseMtx();
    } else {
        Pos3f mtx;
        mtx.identity();
        mtx.set(mSeesawRotator->getRotateMtx());
        mtx.setTrans(mPosition);
        MR::setBaseTRMtx(this, mtx);
    }
}

void LavaHomeSeesaw::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* name;
    MR::getObjectName(&name, rIter);
    initModelManagerWithAnm(name, nullptr, true);
    mMtxSetter = MR::initMaterialProjmapEffectMtxSetter(this);
    MR::prepareMaterials(this);
    initSound(4, false);
    initHitSensor(1);
    MR::initCollisionParts(this, name, MR::addBodyMessageSensorMapObj(this), nullptr);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f1);
    MR::connectToSceneCollisionMapObj(this);
    mSeesawRotator = new LavaHomeSeesawRotator(this, "SE_OJ_LV_LAVA_SEESAW_MOVE", cNum_init_f2);
    mSeesawRotator->init(rIter);
    mSeesawRotator->start();
    makeActorAppeared();
}

void MapPartsFunction::start() {
}

bool MapPartsFunction::isWorking() const {
    return true;
}
