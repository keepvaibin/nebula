#include "Game/MapObj/SpringJetWater.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/CollisionShapeUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 180.0f);

namespace {
    NEBULA_TUNABLE(f32, sSensorRadius, 300.0f);
    NEBULA_TUNABLE(f32, sSensorOffsetY, 300.0f);
    NEBULA_TUNABLE(f32, sHitCylinderRange, 10.0f);
    NEBULA_TUNABLE(f32, sHitSphereRadius, 70.0f);
    NEBULA_TUNABLE(f32, sHitSphereOffsetY, 350.0f);
    NEBULA_TUNABLE(f32, sClippingRadius, 1000.0f);
    static const s32 sEndTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sHitCylinderHeight, 280.0f);
};  // namespace

namespace NrvSpringJetWater {
    NEW_NERVE(HostTypeWait, SpringJetWater, Wait);
    NEW_NERVE(HostTypeJet, SpringJetWater, Jet);
    NEW_NERVE(HostTypeEnd, SpringJetWater, End);
};  // namespace NrvSpringJetWater

SpringJetWater::SpringJetWater(const char* pName) : LiveActor(pName), mObjName(), mUp(0.0f, 1.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< SpringJetWater >);
}

void SpringJetWater::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    Pos3f mtx;
    mtx.identity();
    mtx.setEuler(mRotation * (PI / cNum_init_f1));
    mtx.getYDir(mUp);
    MR::normalize(&mUp);
    MR::getObjectName(&mObjName, rIter);
    initEffectKeeper(0, mObjName);
    initSound(4, false);
    initHitSensor(1);

    MR::addHitSensorMapObj(this, "body", 0x10, ::sSensorRadius, Vec3f(0.0f, ::sSensorOffsetY, 0.0f));
    MR::setClippingTypeSphere(this, ::sClippingRadius);
    initNerve(GET_NERVE(SpringJetWater, HostTypeWait));
    MR::connectToSceneMapObjMovement(this);
    MR::tryRegisterDemoCast(this, rIter);
    appear();
}

void SpringJetWater::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(SpringJetWater, HostTypeWait));
}

void SpringJetWater::startClipped() {
    if (isNerve(GET_NERVE(SpringJetWater, HostTypeWait))) {
        MR::deleteEffect(this, "Wait");
    }

    LiveActor::startClipped();
}

void SpringJetWater::endClipped() {
    LiveActor::endClipped();

    if (isNerve(GET_NERVE(SpringJetWater, HostTypeWait))) {
        MR::emitEffect(this, "Wait");
    }
}

void SpringJetWater::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        f32 playerRadius = pReceiver->getRadius() * 1.0f;

        HitSphere sphere(mPosition + mUp * ::sHitSphereOffsetY, ::sHitSphereRadius);

        if (sphere.isHit(HitSphere(pReceiver->getPosition(), playerRadius))) {
            if (MR::sendArbitraryMsg(ACTMES_FOUNTAINJUMP, pReceiver, pSender)) {
                return;
            }
        }

        HitCylinder cyl(mPosition, mUp * ::sHitCylinderHeight);

        f32 proj, ortho;
        cyl.getParams(pReceiver->getPosition(), &proj, &ortho);

        if (0.0f <= proj && proj <= 1.0f && ortho < playerRadius + ::sHitCylinderRange) {
            if (MR::sendArbitraryMsg(ACTMES_FOUNTAINJUMP, pReceiver, pSender)) {
                return;
            }
        }
    }
}

void SpringJetWater::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensor(this, "body");
        MR::emitEffect(this, "Wait");
    }

    MR::startLevelSound(this, "SE_OJ_LV_SPR_JET_WATER_WAIT");

    if (MR::isDemoPartFirstStep("湧き水上昇")) {
        MR::deleteEffect(this, "Wait");
        setNerve(GET_NERVE(SpringJetWater, HostTypeJet));
    }
}

void SpringJetWater::exeJet() {
    if (MR::isFirstStep(this)) {
        MR::validateHitSensor(this, "body");
        MR::emitEffect(this, "Jet");
        MR::startSound(this, "SE_OJ_SPR_JET_WATER_JET_ST");
        MR::setStageBGMState(1, 180);
    }

    MR::startLevelSound(this, "SE_OJ_LV_SPR_JET_WATER_JET");

    if (MR::isDemoPartLastStep("湧き水上昇")) {
        setNerve(GET_NERVE(SpringJetWater, HostTypeEnd));
    }
}

void SpringJetWater::exeEnd() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensor(this, "body");
        MR::deleteEffect(this, "Jet");
    }

    if (MR::isStep(this, ::sEndTime)) {
        kill();
    }
}
