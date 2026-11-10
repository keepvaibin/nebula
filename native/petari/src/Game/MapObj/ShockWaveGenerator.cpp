#include "Game/MapObj/ShockWaveGenerator.hpp"
#include "Game/System/NebulaRegion.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_isHitCylinder_f1, "isHitCylinder.f1", 140.0f);

namespace {
    static const char* cDemoCameraName = "衝撃波カメラ";
    NEBULA_TUNABLE(s32, sStepForGenerate, 50);
    NEBULA_TUNABLE(s32, sStepForDemoEcho, 1);
    NEBULA_TUNABLE(f32, sHitCylinderRadius, 400.0f);
    NEBULA_TUNABLE(f32, sEnableCameraStartDistance, 2000.0f);
};  // namespace

namespace NrvShockWaveGenerator {
    NEW_NERVE(ShockWaveGeneratorNrvWait, ShockWaveGenerator, Wait);
    NEW_NERVE(ShockWaveGeneratorNrvDemoEcho, ShockWaveGenerator, DemoEcho);
    NEW_NERVE(ShockWaveGeneratorNrvGenerate, ShockWaveGenerator, Generate);
};  // namespace NrvShockWaveGenerator

ShockWaveGenerator::ShockWaveGenerator(const char* pName) : LiveActor(pName), mCameraInfo() {
}

void ShockWaveGenerator::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("ShockWaveGenerator", nullptr, false);
    MR::connectToSceneIndirectMapObj(this);
    initHitSensor(3);
    MR::addBodyMessageSensorMapObj(this);
    MR::addHitSensorMapObjSimple(this, "spin", 8, ::sHitCylinderRadius, Vec3f(0.0f, cNum_init_f1, 0.0f));
    MR::addHitSensorEnemyAttack(this, "shock", 16, cNum_init_f2, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, "ShockWaveGenerator", getSensor("body"), nullptr);

    if (!MR::initActorCamera(this, rIter, &mCameraInfo)) {
        mCameraInfo = nullptr;
    }

    initEffectKeeper(0, nullptr);
    initSound(2, false);

    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f3);
    MR::useStageSwitchSleep(this, rIter);
    initNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvWait));
    makeActorAppeared();
}

void ShockWaveGenerator::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "SpinHit");
        MR::startBtk(this, "SpinHit");
        MR::setBrkFrameAndStop(this, 0.0f);
        MR::setBtkFrameAndStop(this, 0.0f);
        if (NebulaRegion::isEurope()) {
            MR::validateClipping(this);
        }
    }
}

void ShockWaveGenerator::exeDemoEcho() {
    if (MR::isFirstStep(this)) {
        MR::startActorCameraTargetSelf(this, mCameraInfo);
    }

    MR::tryRumblePadVeryWeak(this, PLAYER_1);

    if (MR::isStep(this, ::sStepForDemoEcho)) {
        MR::endDemo(this, ::cDemoCameraName);
        setNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvGenerate));
    }
}

void ShockWaveGenerator::exeGenerate() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "SpinHit");
        sendMsgShockWaveToNearEnemy();
        MR::emitEffect(this, "ShockWave");
        MR::shakeCameraWeak();
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    if (MR::isStep(this, ::sStepForGenerate)) {
        if (mCameraInfo != nullptr) {
            MR::endActorCamera(this, mCameraInfo, false);
        }
        setNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvWait));
    }
}

bool ShockWaveGenerator::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    bool isActive = isNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvGenerate)) ||
                    isNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvDemoEcho));

    if (isActive) {
        return false;
    }

    if (MR::isDemoActive()) {
        return false;
    }

    if (pReceiver == getSensor("body") && MR::isMsgStarPieceAttack(msg)) {
        startShockWave();
        return true;
    }

    if (pReceiver == getSensor("spin") && MR::isMsgPlayerSpinAttack(msg)) {
        if (isHitCylinder(pSender, pReceiver)) {
            startShockWave();
            return true;
        }
    }

    return false;
}

bool ShockWaveGenerator::isPlayerInCameraStartRange() const {
    return mCameraInfo != nullptr && MR::isNearPlayerAnyTime(this, ::sEnableCameraStartDistance);
}

void ShockWaveGenerator::startShockWave() {
    if (NebulaRegion::isEurope()) {
        MR::invalidateClipping(this);
    }

    MR::startSound(this, "SE_OJ_SHOCK_WAVE_GENERATE");
    if (mCameraInfo != nullptr) {
        if (isPlayerInCameraStartRange()) {
            if (!NebulaRegion::isEurope()) {
                MR::requestStartDemoWithoutCinemaFrame(this, ::cDemoCameraName, GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvDemoEcho), nullptr);
            } else if (MR::tryStartDemoWithoutCinemaFrame(this, ::cDemoCameraName)) {
                setNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvDemoEcho));
            }
        } else {
            setNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvGenerate));
        }
        return;
    }

    MR::stopSceneForDefaultHit(1);
    setNerve(GET_NERVE(ShockWaveGenerator, ShockWaveGeneratorNrvGenerate));
}

void ShockWaveGenerator::sendMsgShockWaveToNearEnemy() {
    HitSensor* other;
    HitSensor* shockSensor = getSensor("shock");
    for (s32 idx = 0; idx < shockSensor->mSensorCount; idx++) {
        other = shockSensor->mSensors[idx];
        if (MR::isSensorEnemy(other)) {
            MR::sendMsgToEnemyAttackShockWave(other, shockSensor);
        }
    }
}

bool ShockWaveGenerator::isHitCylinder(HitSensor* pSender, HitSensor* pReceiver) const {
    Vec3f diff;
    diff.sub(pSender->mPosition, pReceiver->mPosition);
    Vec3f up;
    MR::calcUpVec(&up, this);
    Vec3f horiz = diff.killElement(up);
    return horiz.length() <= pSender->getRadius() + cNum_isHitCylinder_f1;
}
