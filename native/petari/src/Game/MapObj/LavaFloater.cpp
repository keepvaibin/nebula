#include "Game/MapObj/LavaFloater.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/MaterialCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/FloaterFloatingForceTypeNormal.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "math_types.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const cEffectName = "LavaBubble";
};  // namespace

namespace NrvLavaFloater {
    NEW_NERVE(HostTypeFloat, LavaFloater, Float);
    NEW_NERVE(HostTypeSink, LavaFloater, Sink);
};  // namespace NrvLavaFloater

LavaFloater::LavaFloater(const char* pName)
    : LiveActor(pName), mFloatingForce(), mMtxSetter(), mLodCtrlPlanet(), mObjectName(), mGroundPos(gZeroVec), mGroundOffset() {
    lumacore::setClass(this, lumacore::kClassId< LavaFloater >);
}

void LavaFloater::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getObjectName(&mObjectName, rIter);

    const char* pName = mObjectName;
    const char* const names[] = {"LavaHomeFloaterB",  "LavaFloatingStepA", "LavaFloatingStepB",
                                 "LavaFloatingStepC", "LavaFloatingStepD", "FloaterOtakingFloaterA"};

    if (MR::isExistString(pName, names, ARRAY_SIZE(names))) {
        initModelManagerWithAnm(mObjectName, nullptr, true);
        mMtxSetter = MR::initMaterialProjmapEffectMtxSetter(this);
        MR::prepareMaterials(this);
    } else {
        initModelManagerWithAnm(mObjectName, nullptr, false);
    }

    initEffectKeeper(0, 0);
    MR::setEffectHostSRT(this, ::cEffectName, &mGroundPos, &mRotation, nullptr);
    initSound(4, false);
    initHitSensor(1);
    MR::initCollisionParts(this, mObjectName, MR::addBodyMessageSensorMapObj(this), nullptr);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    MR::connectToSceneCollisionMapObj(this);

    if (MR::isEqualString("LavaFloatingStepA", mObjectName)) {
        mLodCtrlPlanet = MR::createLodCtrlPlanet(this, rIter, -1.0f);
    }

    initNerve(GET_NERVE(LavaFloater, HostTypeFloat));
    MR::onCalcGravity(this);
    const char* as = mObjectName;
    mFloatingForce = new FloaterFloatingForceTypeNormal(this, as);
    mFloatingForce->init(rIter);
    mFloatingForce->start();
    makeActorAppeared();
}

void LavaFloater::initAfterPlacement() {
    Vec3f down(mGravity);
    down *= 1000.0f;

    if (!MR::getFirstPolyOnLineToMap(&mGroundPos, nullptr, mPosition, down)) {
        MR::calcMapGroundUpper(&mGroundPos, this);
    }

    mGroundOffset = mPosition.distance(mGroundPos);

    if ((mPosition - mGroundPos).dot(mGravity) < 0.0f) {
        mGroundOffset *= -1.0f;
    }

    if (mMtxSetter != nullptr) {
        Vec3f uselessVec(0.0f, mGroundOffset, 0.0f);
        mMtxSetter->updateMtxUseBaseMtxWithLocalOffset(Vec3f(0.0f, mGroundOffset, 0.0f));
    }

    MR::offCalcGravity(this);
}

void LavaFloater::makeActorAppeared() {
    LiveActor::makeActorAppeared();

    if (mLodCtrlPlanet != nullptr) {
        mLodCtrlPlanet->validate();
    }
}

void LavaFloater::makeActorDead() {
    if (mLodCtrlPlanet != nullptr) {
        mLodCtrlPlanet->invalidate();
    }

    LiveActor::makeActorDead();
}

void LavaFloater::control() {
    mFloatingForce->movement();
    mFloatingForce->updateHostTrans(&mPosition);
    mFloatingForce->updateHostVelocity(&mVelocity);

    if (mLodCtrlPlanet != nullptr) {
        mLodCtrlPlanet->update();
    }
}

void LavaFloater::startClipped() {
    LiveActor::startClipped();
    MR::deleteEffectAll(this);
}

void LavaFloater::endClipped() {
    LiveActor::endClipped();

    if (isNerve(GET_NERVE(LavaFloater, HostTypeSink))) {
        MR::emitEffect(this, ::cEffectName);
    }
}

f32 LavaFloater::getCurrentSinkDepth() const {
    Pos3f depthMtx;
    Vec3f basePos, up;
    depthMtx.set(getBaseMtx());
    depthMtx.getTrans(basePos);
    depthMtx.getYDir(up);
    f32 sign = MR::sign(up.dot(mGroundPos - basePos));
    return sign * mGroundPos.distance(basePos);
}

void LavaFloater::exeFloat() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffect(this, ::cEffectName);
    }

    if (mGroundOffset < getCurrentSinkDepth()) {
        setNerve(GET_NERVE(LavaFloater, HostTypeSink));
    }
}

void LavaFloater::exeSink() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, ::cEffectName);
    }

    if (MR::isNearZero(getCurrentSinkDepth() - mGroundOffset, 1.0f)) {
        setNerve(GET_NERVE(LavaFloater, HostTypeFloat));
    }
}
