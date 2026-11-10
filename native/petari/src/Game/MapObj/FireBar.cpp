#include "Game/MapObj/FireBar.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_FireBar_f1, "FireBar.f1", 140.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 55.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f8, "init.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f9, "init.f9", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f10, "init.f10", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f11, "init.f11", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f12, "init.f12", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f13, "init.f13", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f14, "init.f14", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f2, "updateHitSensor.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f3, "updateHitSensor.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f4, "updateHitSensor.f4", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f5, "updateHitSensor.f5", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFireBarBall_f1, "initFireBarBall.f1", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_fixFireBarBall_f1, "fixFireBarBall.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_fixFireBarBall_f2, "fixFireBarBall.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_fixFireBarBall_f3, "fixFireBarBall.f3", 50.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_controlEmitEffect_isNearPlayerAnyTime_1, "controlEmitEffect.isNearPlayerAnyTime.1", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlEmitEffect_isNearPlayerAnyTime_2, "controlEmitEffect.isNearPlayerAnyTime.2", 5000.0f);

namespace {
    // Distance the player must pass beyond the 5000 range before the ball's flame is deleted.
    NEBULA_TUNABLE(f32, sEffectLeaveMargin, 500.0f);
};  // namespace

namespace NrvFireBar {
    NEW_NERVE(FireBarNrvWait, FireBar, Wait);
};  // namespace NrvFireBar

FireBarBall::FireBarBall(LiveActor* pParent) : ModelObj("ファイアバー玉", "FireBarBall", 0, MR::DrawBufferType_NoShadowedMapObj, -2, -2, false) {
    lumacore::setClass(this, lumacore::kClassId< FireBarBall >);
    mFireBarParent = pParent;
}

void FireBarBall::init(const TableIter& rIter) {
    f32 shadowDropDist = cNum_init_f1;
    MR::getPlacementArg6NoInit(rIter, &shadowDropDist);

    if (0.0f != shadowDropDist) {
        MR::initShadowVolumeCylinder(this, cNum_init_f2);
        MR::setShadowDropLength(this, 0, shadowDropDist);
        s32 shadowDropRotate = -1;
        MR::getPlacementArg4NoInit(rIter, &shadowDropRotate);

        if (shadowDropRotate == 0) {
            Vec3f upVec;
            MR::calcUpVec(&upVec, mFireBarParent);
            upVec.negate();
            MR::setShadowDropDirection(this, 0, upVec);
        }

        s32 cutShadows = -1;
        MR::getPlacementArg2NoInit(rIter, &cutShadows);

        if (cutShadows == 0) {
            MR::setShadowVolumeEndDropOffset(this, 0, cNum_init_f3);
            MR::onShadowVolumeCutDropLength(this, 0);
            MR::onCalcShadow(this, 0);
        }
    }

    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f4);
    makeActorAppeared();
}

void FireBarBall::initAfterPlacement() {
    MR::calcGravity(this);
}

void FireBarBall::control() {
    controlEmitEffect();
}

void FireBarBall::startClipped() {
    mIsEffectNear = false;
    MR::deleteEffect(this, "FireBarBall");
    LiveActor::startClipped();
}

void FireBarBall::endClipped() {
    LiveActor::endClipped();
}

void FireBarBall::controlEmitEffect() {
    // Emit inside 5000, delete only past 5000 plus a margin: a player walking back and forth over the line deleted and
    // made the flame again every few steps, restarting its particles each time.
    mIsEffectNear = MR::isNearPlayerAnyTime(this, mIsEffectNear ? cNum_controlEmitEffect_isNearPlayerAnyTime_1 + ::sEffectLeaveMargin : cNum_controlEmitEffect_isNearPlayerAnyTime_2);
    if (mIsEffectNear) {
        MR::emitEffect(this, "FireBarBall");
    } else {
        MR::deleteEffect(this, "FireBarBall");
    }
}

FireBar::FireBar(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< FireBar >);
    mFireBalls = nullptr;
    mFireBallCount = 5;
    mFront.x = 0.0f;
    mFront.y = 0.0f;
    mFront.z = 0.0f;
    mFireBarSpeed = -1.0f;
    mStickCount = 1;
    mStickDistance = cNum_FireBar_f1;
}

void FireBar::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("FireBarCore", 0, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(2);
    MR::addBodyMessageSensorMapObj(this);
    MR::addHitSensorCallbackEnemyAttack(this, "fire", 8, cNum_init_f5);
    MR::initCollisionParts(this, "FireBarCore", getSensor("body"), 0);
    initEffectKeeper(0, 0);
    initSound(3, false);
    MR::calcGravity(this);

    f32 shadowDropDist = cNum_init_f6;
    MR::getPlacementArg6NoInit(rIter, &shadowDropDist);
    MR::getPlacementArg3NoInit(rIter, &shadowDropDist);
    shadowDropDist -= cNum_init_f7;

    if (shadowDropDist > 0.0f) {
        Vec3f up_vec;
        Vec3f scaled_vec;
        Vec3f drop_direction;
        Vec3f shadow_box;
        shadow_box.x = cNum_init_f8;
        shadow_box.y = cNum_init_f9;
        shadow_box.z = cNum_init_f10;
        MR::initShadowVolumeBox(this, shadow_box);
        MR::calcUpVec(&up_vec, this);
        scaled_vec.scaleAdd(cNum_init_f11, up_vec, mPosition);
        MR::setShadowDropPosition(this, 0, scaled_vec);
        MR::setShadowVolumeStartDropOffset(this, 0, cNum_init_f12);
        MR::setShadowDropLength(this, 0, shadowDropDist);

        s32 rotateShadow = -1;
        MR::getPlacementArg4NoInit(rIter, &rotateShadow);

        if (rotateShadow == 0) {
            MR::calcUpVec(&drop_direction, this);
            drop_direction.negate();
            MR::setShadowDropDirection(this, 0, drop_direction);
        }
    }

    s32 numFireBalls = 5;
    f32 fireBallSpeed = -1.0f;
    MR::getPlacementArg0NoInit(rIter, &numFireBalls);
    MR::getPlacementArg1NoInit(rIter, &fireBallSpeed);
    MR::getPlacementArg5NoInit(rIter, &mStickDistance);
    MR::getPlacementArg7NoInit(rIter, &mStickCount);

    // A negative count of balls or sticks (a placement other than the unset -1) makes none, not a negative array.
    if (numFireBalls < 0) {
        numFireBalls = 0;
    }

    if (mStickCount < 0) {
        mStickCount = 0;
    }

    mFireBarSpeed = cNum_init_f13 * fireBallSpeed;
    mFireBallCount = mStickCount * numFireBalls;
    MR::calcFrontVec(&mFront, this);
    initFireBarBall(rIter);
    MR::setGroupClipping(this, rIter, 0x20);
    MR::setClippingTypeSphere(this, (mFireBallCount + 1) * cNum_init_f14);
    MR::useStageSwitchSleep(this, rIter);
    initNerve(GET_NERVE(FireBar, FireBarNrvWait));
    makeActorAppeared();
}

void FireBar::exeWait() {
    Vec3f up_vec;
    MR::calcUpVec(&up_vec, this);
    MR::rotateVecDegree(&mFront, up_vec, mFireBarSpeed);
    MR::startLevelSound(this, "SE_OJ_LV_FIRE_BAR_CENTER");
    fixFireBarBall();
}

void FireBar::makeActorAppeared() {
    for (s32 i = 0; i < mFireBallCount; i++) {
        mFireBalls[i]->makeActorAppeared();
    }

    LiveActor::makeActorAppeared();
}

void FireBar::makeActorDead() {
    for (s32 i = 0; i < mFireBallCount; i++) {
        mFireBalls[i]->makeActorDead();
    }

    LiveActor::makeActorDead();
}

void FireBar::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver) && MR::isSensorEnemyAttack(pSender)) {
        MR::sendMsgEnemyAttackFire(pReceiver, pSender);
    }
}

void FireBar::updateHitSensor(HitSensor* pSensor) {
    f32 minDistance = FLOAT_MAX;

    Vec3f position = mPosition;

    s32 fireBallCount = mStickCount != 0 ? mFireBallCount / mStickCount : 0;

    Vec3f up;
    MR::calcUpVec(&up, this);

    Vec3f start;
    start.scale(mStickDistance, mFront);
    start.scaleAdd(cNum_updateHitSensor_f1, up, start);

    Vec3f end;
    end.scale(mStickDistance + cNum_updateHitSensor_f2 * (fireBallCount - 1), mFront);
    end.scaleAdd(cNum_updateHitSensor_f3, up, end);

    for (s32 i = 0; i < mStickCount; i++) {
        Vec3f startWorld;
        startWorld.add(start, *getPosition());
        Vec3f endWorld;
        endWorld.add(end, *getPosition());

        Vec3f footPos;
        MR::calcPerpendicFootToLineInside(&footPos, *MR::getPlayerCenterPos(), startWorld, endWorld);

        f32 distance = footPos.distance(*MR::getPlayerCenterPos());

        if (minDistance > distance) {
            position.set(footPos);
            minDistance = distance;
        }

        if (i + 1 != mStickCount) {
            Vec3f up;
            MR::calcUpVec(&up, this);
            MR::rotateVecDegree(&start, up, cNum_updateHitSensor_f4 / mStickCount);
            MR::rotateVecDegree(&end, up, cNum_updateHitSensor_f5 / mStickCount);
        }
    }

    pSensor->mPosition.set(position);
}

void FireBar::initFireBarBall(const TableIter& rIter) {
    mFireBalls = new FireBarBall*[mFireBallCount];
    s32 totalNum = mStickCount != 0 ? mFireBallCount / mStickCount : 0;
    f32 startVal = 0.0f;

    for (s32 i = 0; i < mFireBallCount; i++) {
        mFireBalls[i] = new FireBarBall(this);
        mFireBalls[i]->init(rIter);
        MR::startBtk(mFireBalls[i], "FireBarBall");
        s32 div = totalNum != 0 ? i / totalNum : 0;
        div *= totalNum;

        startVal = !(i - div) ? 0.0f : cNum_initFireBarBall_f1 + startVal;
        const s32 frameMax = MR::getBtkFrameMax(mFireBalls[i]);
        startVal = frameMax != 0 ? static_cast< s32 >(startVal) % frameMax : static_cast< s32 >(startVal);
        MR::setBtkFrame(mFireBalls[i], startVal);
    }

    fixFireBarBall();
}

void FireBar::fixFireBarBall() {
    Vec3f scaled;
    scaled.scale(cNum_fixFireBarBall_f1, mFront);

    Vec3f final_pos;
    final_pos.zero();

    s32 totalNum = mStickCount != 0 ? mFireBallCount / mStickCount : 0;

    for (s32 i = 0; i < mFireBallCount; i++) {
        s32 div = totalNum != 0 ? i / totalNum : 0;
        div *= totalNum;

        if (i - div == 0) {
            Vec3f up_vec;
            MR::calcUpVec(&up_vec, this);
            MR::rotateVecDegree(&scaled, up_vec, cNum_fixFireBarBall_f2 / mStickCount);
            Vec3f norm;
            MR::normalize(scaled, &norm);
            final_pos.scaleAdd(mStickDistance, norm, mPosition);
            final_pos.scaleAdd(cNum_fixFireBarBall_f3, up_vec, final_pos);
        } else {
            final_pos.add(scaled);
        }

        mFireBalls[i]->mPosition.set(final_pos);
    }
}
