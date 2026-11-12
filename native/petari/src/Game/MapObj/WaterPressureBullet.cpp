#include "Game/MapObj/WaterPressureBullet.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace NrvWaterPressureBullet {
    NEW_NERVE(WaterPressureBulletNrvFly, WaterPressureBullet, Fly);
    NEW_NERVE(WaterPressureBulletNrvSpinKill, WaterPressureBullet, SpinKill);
};  // namespace NrvWaterPressureBullet

WaterPressureBullet::WaterPressureBullet(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< WaterPressureBullet >);
    mFront.x = 0.0f;
    mFront.y = 0.0f;
    mFront.z = 0.0f;
    mSide.x = 0.0f;
    mSide.y = 0.0f;
    mSide.z = 0.0f;
    mHost = 0;
    mSpeed = 0.0f;
    mHostActor = nullptr;
    mIsNoGravity = false;
    mIsGroundSlide = false;
    mIsInvalidSpinKill = false;
    mCameraInfo = nullptr;
}

void WaterPressureBullet::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("WaterBullet", nullptr, false);
    MR::connectToSceneMapObjStrongLight(this);
    initHitSensor(2);
    MR::addHitSensor(this, "body", ATYPE_WATER_PRESSURE_BULLET, 4, 100.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "binder", ATYPE_WATER_PRESSURE_BULLET_BIND, 4, 100.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(100.0f, 0.0f, 0);
    initEffectKeeper(0, nullptr);
    initSound(6, false);
    MR::initStarPointerTarget(this, 100.0f, Vec3f(0, 0, 0));
    MR::initShadowVolumeSphere(this, 75.0f);
    MR::setShadowDropLength(this, nullptr, 1500.0f);
    MR::registerDemoSimpleCastAll(this);
    initNerve(GET_NERVE(WaterPressureBullet, WaterPressureBulletNrvFly));
    makeActorDead();
}

void WaterPressureBullet::kill() {
    if (MR::isPlayerInRush() && mHostActor != nullptr) {
        MR::startBckPlayer("GCaptureBreak", 0);
        MR::endBindAndPlayerJumpWithRollLanding(this, mVelocity, 0);
        mHostActor = nullptr;
        endHostCamera();
    }

    MR::emitEffect(this, "Break");
    MR::startSound(this, "SE_OJ_W_PRESS_BUBBLE_BREAK");
    LiveActor::kill();
}

void WaterPressureBullet::control() {
    if (isNotBound() && MR::isStarPointerPointing2POnTriggerButton(this, "弱", true, false)) {
        kill();
    } else {
        Vec3f targetDir;

        if (MR::isNearZero(mVelocity)) {
            targetDir.set(*getGravity());
        } else {
            targetDir.set(*getVelocity());
        }

        MR::turnVecToVecCosOnPlane(&mFront, targetDir, mSide, MR::cosDegree(2.5f));
    }
}

void WaterPressureBullet::calcAndSetBaseMtx() {
    Pos3f pos;
    MR::makeMtxFrontSidePos(&pos, mFront, mSide, mPosition);
    MR::setBaseTRMtx(this, pos);
}

void WaterPressureBullet::shotWaterBullet(LiveActor* pActor, const Pos3f& rPos, f32 speed, bool isNoGravity, bool isGroundSlide,
                                          bool isInvalidSpinKill, ActorCameraInfo** pInfo) {
    mSpeed = speed;
    mHost = pActor;
    mIsNoGravity = isNoGravity;
    mIsGroundSlide = isGroundSlide;
    mIsInvalidSpinKill = isInvalidSpinKill;
    mCameraInfo = pInfo;
    rPos.getZDir(mFront);
    mVelocity.scale(mSpeed, mFront);
    rPos.getTrans(mPosition);
    rPos.getXDir(mSide);
    mRotation.zero();
    makeActorAppeared();
    MR::validateHitSensors(this);
    MR::invalidateClipping(this);
    MR::setShadowDropLength(this, nullptr, 1500.0f);

    if (!mIsNoGravity) {
        MR::onCalcGravity(this);
    }

    setNerve(GET_NERVE(WaterPressureBullet, WaterPressureBulletNrvFly));
}

void WaterPressureBullet::exeFly() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Shot");
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startBck(this, "Move");
    }

    if (isBound() && MR::isBckOneTimeAndStopped(mHostActor)) {
        MR::startBckPlayer("WaterBulletWait");
    }

    if (isBound()) {
        MR::startLevelSound(this, "SE_OJ_LV_W_PRESS_BUBBLE_SUS");
    }

    if (!mIsNoGravity) {
        mVelocity.scaleAdd(0.4f, mGravity, mVelocity);
    }

    if (MR::isSpin(PLAYER_1) && isBound() && !mIsInvalidSpinKill) {
        MR::startSound(mHostActor, "SE_PV_TWIST_START");
        MR::startSound(mHostActor, "SE_PM_SPIN_ATTACK");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        setNerve(GET_NERVE(WaterPressureBullet, WaterPressureBulletNrvSpinKill));
        return;
    }

    if (MR::isBinded(this) || MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
        if (mIsGroundSlide && isBound() && MR::isBindedGroundSand(this)) {
            const Vec3f& vel = mVelocity;
            const Vec3f& grav = mGravity;
            mVelocity.scaleAdd(-grav.dot(vel), grav, vel);
        } else {
            kill();
            return;
        }
    }

    if ((!mIsInvalidSpinKill && MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(3))) || (mIsInvalidSpinKill && MR::isGreaterEqualStep(this, NebulaFrameClock::stepsOfSeconds(5)))) {
        kill();
    }
}

void WaterPressureBullet::exeSpinKill() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("Spin2nd", 0);
        mVelocity.zero();
        MR::invalidateHitSensors(this);

        if (MR::isPlayerInRush()) {
            if (isBound()) {
                MR::endBindAndPlayerJump(this, mVelocity, 0);
                mHostActor = nullptr;
                endHostCamera();
            }
        }
    }

    kill();
}

void WaterPressureBullet::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorMapObj(pReceiver)) {
        kill();
    }
}

bool WaterPressureBullet::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgFireBallAttack(msg)) {
        kill();
        return true;
    }

    return false;
}

bool WaterPressureBullet::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isDead(this)) {
        return false;
    }

    if (MR::isMsgAutoRushBegin(msg) && MR::isSensorPlayer(pSender) && !isBound()) {
        if (MR::isDemoActive()) {
            kill();

            return false;
        }

        if (!inviteMario(pSender)) {
            return false;
        } else {
            MR::startSound(this, "SE_OJ_W_PRESS_BUBBLE_IN");
            MR::startSound(mHostActor, "SE_PV_CATCH");

            return true;
        }
    } else if (msg == ACTMES_IS_RUSH_TAKEOVER) {
        return true;
    } else if (msg == ACTMES_RUSH_CANCEL) {
        kill();

        return true;
    } else if (msg == ACTMES_UPDATE_BASEMTX && isBound()) {
        updateSuffererMtx();

        return true;
    }

    return false;
}

bool WaterPressureBullet::startHostCamera() const {
    if (mHost != nullptr && mCameraInfo != nullptr) {
        MR::startActorCameraNoTarget(mHost, *mCameraInfo);
        return true;
    }

    return false;
}

bool WaterPressureBullet::endHostCamera() const {
    if (mHost != nullptr && mCameraInfo != nullptr) {
        MR::endActorCamera(mHost, *mCameraInfo, true);
        return true;
    }

    return false;
}

bool WaterPressureBullet::inviteMario(HitSensor* pSensor) {
    MR::tryRumblePadMiddle(this, PLAYER_1);

    if (MR::isOnGroundPlayer() && MR::isNearAngleDegree(mVelocity, mGravity, 60.0f)) {
        if (mIsGroundSlide) {
            MR::killVelocityVertical(this);
        } else {
            kill();
            MR::sendArbitraryMsg(ACTMES_ENEMY_ATTACK_FLIP_VERYWEAK, pSensor, getSensor("body"));

            return false;
        }
    }

    mHostActor = pSensor->mHost;
    MR::startBckWithInterpole(this, "Touch", 0);
    MR::startBckPlayer("WaterBulletStart", 2);
    startHostCamera();
    MR::setShadowDropLength(this, nullptr, 2000.0f);

    return true;
}

void WaterPressureBullet::updateSuffererMtx() {
    Pos3f pos;
    pos.identity();

    if (isNerve(GET_NERVE(WaterPressureBullet, WaterPressureBulletNrvSpinKill))) {
        Vec3f front;
        MR::calcFrontVec(&front, mHostActor);
        MR::calcMtxFromGravityAndZAxis(&pos, mHostActor, mGravity, front);
    } else {
        MtxPtr mtx = getBaseMtx();
        pos.setInline(mtx);
    }

    MR::setBaseTRMtx(mHostActor, pos);
}
