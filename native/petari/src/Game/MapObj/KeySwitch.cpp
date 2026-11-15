#include "Game/MapObj/KeySwitch.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    static const char* const cDemoName = "カギ出現";
};  // namespace

namespace NrvKeySwitch {
    NEW_NERVE(KeySwitchNrvDemoStart, KeySwitch, DemoStart);
    NEW_NERVE(KeySwitchNrvAppear, KeySwitch, Appear);
    NEW_NERVE(KeySwitchNrvWait, KeySwitch, Wait);
};  // namespace NrvKeySwitch

KeySwitch::KeySwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< KeySwitch >);
    mCameraInfo = 0;
    mCurDemoFrame = -1;
}

void KeySwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("KeySwitch", 0, false);
    MR::connectToSceneMapObjDecoration(this);
    initHitSensor(1);
    Vec3f box;
    box.x = 0.0f;
    box.y = 105.0f;
    box.z = 0.0f;
    MR::addHitSensorMapObjSimple(this, "body", 8, 105.0f, box);
    initBinder(105.0f, 105.0f, 0);
    initEffectKeeper(0, 0);
    initSound(4, false);
    MR::initShadowVolumeCylinder(this, 90.0f);

    if (MR::isValidInfo(rIter)) {
        MR::initActorCamera(this, rIter, &mCameraInfo);
    }

    bool usesSwitch = false;

    if (MR::isValidInfo(rIter)) {
        bool uses = MR::useStageSwitchReadAppear(this, rIter);
        if (uses) {
            usesSwitch = true;
            MR::syncStageSwitchAppear(this);
        }

        MR::needStageSwitchWriteA(this, rIter);
    }

    if (usesSwitch || !MR::isValidInfo(rIter)) {
        initNerve(GET_NERVE(KeySwitch, KeySwitchNrvDemoStart));

    } else {
        initNerve(GET_NERVE(KeySwitch, KeySwitchNrvWait));
    }

    if (usesSwitch || !MR::isValidInfo(rIter)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void KeySwitch::initKeySwitchByOwner(const TableIter& rIter) {
    initWithoutIter();
    MR::needStageSwitchWriteA(this, rIter);
    MR::initActorCamera(this, rIter, &mCameraInfo);
}

void KeySwitch::appearKeySwitch(const Vec3f& rVec) {
    MR::resetPosition(this, rVec);
    MR::onCalcGravity(this);
    MR::invalidateClipping(this);
    appear();
}

void KeySwitch::exeDemoStart() {
    if (!mCameraInfo) {
        setNerve(GET_NERVE(KeySwitch, KeySwitchNrvAppear));
        return;
    }

    if (MR::tryStartDemoWithoutCinemaFrame(this, ::cDemoName)) {
        MR::startActorCameraTargetSelf(this, mCameraInfo);
        mCurDemoFrame = 0;
        setNerve(GET_NERVE(KeySwitch, KeySwitchNrvAppear));
    }
}

void KeySwitch::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Rotation");
        mVelocity.scale(-40.0f, mGravity);
        MR::invalidateClipping(this);
        MR::startSound(this, "SE_OJ_KEY_SWITCH_APPEAR");
    }

    MR::setBckRate(this, MR::calcNerveValue(this, 0xB4, 3.0f, 1.5f));
    MR::addVelocityToGravity(this, 2.0f);

    bool isSettled = false;

    if (MR::isBindedGround(this)) {
        if (mVelocity.length() >= 10.0f) {
            s32 mag = mVelocity.length();
            mag *= 2;
            if (mag > 0x64) {
                mag = 0x64;
            }

            MR::startSound(this, "SE_OJ_KEY_SWITCH_BOUND", mag);
            Vec3f neg;
            neg.negate(mGravity);
            MR::calcReboundVelocity(&mVelocity, neg, 0.60f, 0.7f);
        } else {
            isSettled = true;
        }
    }

    if (!tryAvoid()) {
        if (isSettled) {
            MR::validateClipping(this);
            setNerve(GET_NERVE(KeySwitch, KeySwitchNrvWait));
        }
    }
}

void KeySwitch::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(this, "Rotation");
        MR::setBckRate(this, 1.0f);
        mVelocity.zero();
        MR::offCalcGravity(this);
    }
}

void KeySwitch::appear() {
    LiveActor::appear();
}

void KeySwitch::kill() {
    MR::onSwitchA(this);
    MR::startSystemSE("SE_SY_READ_RIDDLE_S");
    MR::emitEffect(this, "Get");
    LiveActor::kill();
}

void KeySwitch::calcAndSetBaseMtx() {
    Vec3f front;
    MR::calcFrontVec(&front, this);
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, front);
    MR::setBaseTRMtx(this, mtx);
}

void KeySwitch::control() {
    if (mCurDemoFrame != -1 && mCameraInfo) {
        if (mCurDemoFrame >= 0x28) {
            MR::endActorCamera(this, mCameraInfo, false);
            MR::endDemo(this, ::cDemoName);
            mCameraInfo = 0;
            mCurDemoFrame = -1;
        } else {
            mCurDemoFrame = mCurDemoFrame + 1;
        }
    }
}

bool KeySwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgStarPieceReflect(msg);
}

bool KeySwitch::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    if (isNerve(GET_NERVE(KeySwitch, KeySwitchNrvAppear)) && MR::isLessEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        return false;
    }

    if (MR::isMsgItemGet(msg)) {
        MR::startSound(this, "SE_OJ_KEY_SWITCH_GET");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        kill();
        return true;
    }

    return false;
}

bool KeySwitch::tryAvoid() {
    LiveActor* sensorActor;
    HitSensor* sensor = nullptr;

    if (MR::isBindedGround(this)) {
        sensor = MR::getGroundSensor(this);
    } else if (MR::isBindedWall(this)) {
        sensor = MR::getWallSensor(this);
    }

    if (sensor == nullptr) {
        return false;
    }

    if (sensor->mType != ATYPE_KEY_SWITCH_AVOID) {
        return false;
    }

    sensorActor = sensor->mHost;
    Vec3f up;
    MR::calcUpVec(&up, sensorActor);
    Vec3f away;
    away.sub(mPosition, sensorActor->mPosition);
    Vec3f awayOnPlane = away.killElement(up);

    if (MR::normalizeOrZero(&awayOnPlane)) {
        MR::calcFrontVec(&awayOnPlane, sensorActor);
    }

    mVelocity.scale(10.0f, awayOnPlane);
    return true;
}
