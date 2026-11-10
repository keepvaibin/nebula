#include "Game/Map/Butterfly.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_tryAppearStarPeace_f1, "tryAppearStarPeace.f1", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeGotoSleepingMario_f1, "exeGotoSleepingMario.f1", 0.05f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeGotoSleepingMario_checkPassBckFrame_1, "exeGotoSleepingMario.checkPassBckFrame.1", 0.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeReadyToPerchOnSleepingMario_checkPassBckFrame_1, "exeReadyToPerchOnSleepingMario.checkPassBckFrame.1", 0.0f);

namespace NrvButterfly {
    NEW_NERVE(HostTypeWait, Butterfly, Wait);
    NEW_NERVE(HostTypeRunAway, Butterfly, RunAway);
    NEW_NERVE(HostTypeHive, Butterfly, Hive);
    NEW_NERVE(HostTypePerchOn, Butterfly, PerchOn);
    NEW_NERVE(HostTypeGotoSleepingMario, Butterfly, GotoSleepingMario);
    NEW_NERVE(HostTypeReadyToPerchOnSleepingMario, Butterfly, ReadyToPerchOnSleepingMario);
    NEW_NERVE(HostTypePerchOnSleepingMario, Butterfly, PerchOnSleepingMario);
};  // namespace NrvButterfly

namespace {
    NEBULA_TUNABLE(f32, sRadius, 30.0f);
    NEBULA_TUNABLE(f32, sPointerRadius, 200.0f);
    NEBULA_TUNABLE(f32, sPointerRadiusFast, 100.0f);
    NEBULA_TUNABLE(f32, sPointerSpeedFast, 3.0f);
    NEBULA_TUNABLE(f32, sAppearStarPeaceScreenRadius, 10.0f);
    NEBULA_TUNABLE(f32, sWaitAnimRate, 1.0f);
    NEBULA_TUNABLE(f32, sHomeRadius, 200.0f);
    NEBULA_TUNABLE(f32, sRunAwayVel, 12.0f);
    NEBULA_TUNABLE(f32, sRunAwayTime, 30.0f);
    NEBULA_TUNABLE(f32, sRunAwayAttenVel, 0.97f);
    NEBULA_TUNABLE(f32, sRunAwayAnimRate, 3.0f);
    NEBULA_TUNABLE(f32, sBackToHomeAccel, 0.01f);
    NEBULA_TUNABLE(f32, sBackToHomeVelMax, 2.0f);
    NEBULA_TUNABLE(f32, sBackToHomeAttenVel, 0.99f);
    NEBULA_TUNABLE(f32, sMoveDistanceMax, 500.0f);
    NEBULA_TUNABLE(f32, sHiveRadius, 100.0f);
    NEBULA_TUNABLE(f32, sHiveAccel, 0.05f);
    NEBULA_TUNABLE(f32, sHiveVelMax, 2.5f);
    NEBULA_TUNABLE(f32, sHiveAnimRate, 1.2f);
    NEBULA_TUNABLE(f32, sPerchOnAnimRate, 0.7f);
    NEBULA_TUNABLE(f32, sAnimInterpolateRate, 0.02f);
    NEBULA_TUNABLE(f32, sSleepingMarioNearDistance, 500.0f);
    NEBULA_TUNABLE(f32, sPerchOnNearDistance, 10.0f);
    NEBULA_TUNABLE(f32, sGotoSleepingMarioWaitTime, 300.0f);
    NEBULA_TUNABLE(f32, sGotoSleepingMarioSpeedMax, 2.0f);
    NEBULA_TUNABLE(f32, sReadyToPerchOnAnimRate, 0.5f);
    NEBULA_TUNABLE(f32, sTransBlendRatio, 0.7f);
    NEBULA_TUNABLE(f32, sTurnLimit, 0.997f);  // ~4.44 degrees
    NEBULA_TUNABLE(f32, sUpdatePostureMinVel, 0.5f);

    NEBULA_TUNABLE_RECORD(Vec, sMarioCapPosOffset, {0.0f, 50.2f, 24.9f});

};  // namespace

Butterfly::Butterfly(const char* pName) : LiveActor(pName), mHomePos(gZeroVec), mColorIndex(), mPerchTarget(), mStarPieceAppeared() {
    mQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void Butterfly::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mHomePos.set(mPosition);
    initModelManagerWithAnm("Butterfly", nullptr, false);
    MR::connectToSceneMapObj(this);

    // Colors are stored in an animation for the purposes of easy indexing
    MR::getPlacementArg0NoInit(rIter, &mColorIndex);
    MR::startBrk(this, "Butterfly");
    MR::setBrkFrameAndStop(this, mColorIndex);

    initHitSensor(1);
    MR::addHitSensorMtxAnimal(this, "body", 8, ::sRadius, MR::getJointMtx(this, "buttBody"), Vec3f(0.0f, 0.0f, 0.0f));
    initBinder(::sRadius, 0.0f, 0);
    initSound(2, false);

    s32 noStarPiece = -1;
    MR::getPlacementArg1NoInit(rIter, &noStarPiece);
    if (noStarPiece == -1) {
        MR::declareStarPiece(this, 1);
    } else {
        mStarPieceAppeared = true;
    }

    MR::initStarPointerTarget(this, ::sPointerRadiusFast);
    MR::setClippingTypeSphere(this, ::sRadius);
    MR::setClippingFar50m(this);
    initNerve(GET_NERVE(Butterfly, HostTypeWait));
    makeActorAppeared();
    MR::startBck(this, "Butterfly");
    MR::setBckFrameAtRandom(this);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    }
}

void Butterfly::initAfterPlacement() {
    if (MR::isEqualStageName("HeavensDoorGalaxy")) {
        Vec3f gravity;
        MR::calcGravityVector(this, &gravity, nullptr, 0);
        MR::makeQuatUpNoSupport(&mQuat, -gravity);
    } else {
        mQuat.setEuler(DEG_TO_RAD(mRotation));
    }
}

void Butterfly::control() {
    updatePosture();
    s32 port = 0;
    if (!MR::isStarPointerInScreenAnyPort(&port)) {
        return;
    }

    if (MR::getStarPointerScreenSpeed(port) < ::sPointerSpeedFast) {
        MR::setStarPointerTargetRadius3d(this, ::sPointerRadius);
    } else {
        MR::setStarPointerTargetRadius3d(this, ::sPointerRadiusFast);
    }
    tryAppearStarPeace(port);
}

void Butterfly::calcAndSetBaseMtx() {
    Pos3f mtx;
    mtx.setQT(mQuat, mPosition);
    MR::setBaseTRMtx(this, mtx);
}

void Butterfly::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!MR::isSensorNpc(pReceiver)) {
        return;
    }

    if (!isNerve(GET_NERVE(Butterfly, HostTypeHive))) {
        return;
    }

    mPerchTarget = pReceiver;
    setNerve(GET_NERVE(Butterfly, HostTypePerchOn));
}

void Butterfly::updatePosture() {
    Vec3f gravity;
    MR::calcGravityVector(this, &gravity, nullptr, 0);

    Vec3f up;
    mQuat.getYDir(up);
    Vec3f forward;
    mQuat.getZDir(forward);

    Vec3f vel = mVelocity;
    if (vel.length() < ::sUpdatePostureMinVel) {
        mQuat.normalize();
        return;
    }

    MR::vecKillElement(vel, gravity, &vel);
    if (MR::isNearZero(vel)) {
        mQuat.normalize();
        return;
    }

    MR::normalize(&vel);
    Vec3f targetForward = vel.killElement(up);
    MR::normalizeOrZero(&targetForward);
    if (!MR::isNearZero(targetForward)) {
        Vec3f newForward;
        MR::turnVecToVecCos(&newForward, forward, targetForward, ::sTurnLimit, up);
        Quat4f turn;
        turn.setRotate(forward, newForward, 1.0f);
        mQuat.mult(turn);
    }
    mQuat.normalize();
}

void Butterfly::addRunAwayVelocity() {
    Vec2f pointerScreenPos;
    pointerScreenPos.set(*MR::getStarPointerScreenPosition(*MR::getStarPointerLastPointedPort(this)));
    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, mPosition);
    Vec2f runAwayDir = screenPos - pointerScreenPos;

    if (!MR::isNearZero(runAwayDir)) {
        MR::normalize(&pointerScreenPos);
    }

    Vec3f xDir = MR::getCamXdir();
    Vec3f invYDir = -MR::getCamYdir();

    Vec3f vel = xDir * runAwayDir.x + invYDir * runAwayDir.y;

    Vec3f gravity;
    MR::calcGravityVector(this, &gravity, nullptr, 0);

    if (0.0f < vel.dot(gravity)) {
        MR::vecKillElement(vel, gravity, &vel);
    }

    vel.setLength(::sRunAwayVel);
    mVelocity += vel;
}

bool Butterfly::tryRunAway() {
    s32 port = *MR::getStarPointerLastPointedPort(this);
    if (MR::isStarPointerInScreen(port) == 0) {
        return false;
    }

    if (!MR::isStarPointerPointing2P(this, nullptr, false, false)) {
        return false;
    }

    if (MR::isHeldFire(port)) {
        return false;
    }

    if (::sMoveDistanceMax < mPosition.distance(mHomePos)) {
        return false;
    }

    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, mPosition);
    Vec2f pointerScreenPos;
    pointerScreenPos.set(*MR::getStarPointerScreenPosition(port));
    if (MR::getStarPointerScreenVelocity(port)->dot(screenPos - pointerScreenPos) <= 0.0f) {
        return false;
    }

    if (!MR::tryStarPointerCheckWithoutRumble(this, false)) {
        return false;
    }

    addRunAwayVelocity();
    setNerve(GET_NERVE(Butterfly, HostTypeRunAway));
    return true;
}

bool Butterfly::tryHive() {
    s32 port = 0;
    if (!MR::isStarPointerInScreenAnyPort(&port)) {
        return false;
    }

    if (::sMoveDistanceMax < mPosition.distance(mHomePos)) {
        return false;
    }

    Vec2f screenPos;
    Vec2f pointerScreenPos = *MR::getStarPointerScreenPosition(port);
    MR::calcScreenPosition(&screenPos, mPosition);
    if (::sHiveRadius < pointerScreenPos.distance(screenPos)) {
        return false;
    }

    if (!MR::isHeldFire(port)) {
        return false;
    }

    setNerve(GET_NERVE(Butterfly, HostTypeHive));
    return true;
}

bool Butterfly::tryPerchOnSleepingMario() {
    if (::sSleepingMarioNearDistance < MR::calcDistanceToPlayer(this)) {
        return false;
    }

    if (!MR::isPlayerSleeping()) {
        return false;
    }

    setNerve(GET_NERVE(Butterfly, HostTypeGotoSleepingMario));
    return true;
}

bool Butterfly::tryAppearStarPeace(s32 port) {
    if (mStarPieceAppeared) {
        return false;
    }

    f32 distance;
    if (!MR::calcStarPointerScreenDistanceToTarget(this, &distance, port)) {
        return false;
    }

    if (::sAppearStarPeaceScreenRadius < distance) {
        return false;
    }

    if (!MR::appearStarPiece(this, mPosition, 1, ::sAppearStarPeaceScreenRadius, cNum_tryAppearStarPeace_f1, false)) {
        return false;
    }

    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    mStarPieceAppeared = true;
    return true;
}

void Butterfly::exeWait() {
    if (tryRunAway()) {
        return;
    }

    if (tryHive()) {
        return;
    }

    if (tryPerchOnSleepingMario()) {
        return;
    }

    MR::setBckRate(this, MR::converge(MR::getBckRate(this), ::sWaitAnimRate, ::sAnimInterpolateRate));
    mVelocity.x *= ::sBackToHomeAttenVel;
    mVelocity.y *= ::sBackToHomeAttenVel;
    mVelocity.z *= ::sBackToHomeAttenVel;

    Vec3f backToHomeVel = mHomePos - mPosition;
    if (MR::isNearZero(backToHomeVel)) {
        return;
    }

    MR::normalize(&backToHomeVel);
    mVelocity.add(backToHomeVel * ::sBackToHomeAccel);

    if (::sBackToHomeVelMax < mVelocity.length()) {
        mVelocity.setLength(::sBackToHomeVelMax);
    }

    if (!MR::isInvalidClipping(this)) {
        return;
    }

    if (mHomePos.distance(mPosition) < ::sHomeRadius) {
        MR::validateClipping(this);
    }
}

void Butterfly::exeRunAway() {
    if (MR::isFirstStep(this) && !MR::isInvalidClipping(this)) {
        MR::invalidateClipping(this);
    }

    MR::setBckRate(this, MR::converge(MR::getBckRate(this), ::sRunAwayAnimRate, ::sAnimInterpolateRate));
    mVelocity.x *= ::sRunAwayAttenVel;
    mVelocity.y *= ::sRunAwayAttenVel;
    mVelocity.z *= ::sRunAwayAttenVel;

    if (MR::isStep(this, ::sRunAwayTime)) {
        setNerve(GET_NERVE(Butterfly, HostTypeWait));
    }
}

void Butterfly::exeHive() {
    if (tryRunAway()) {
        return;
    }

    s32 port = 0;
    if (!MR::isStarPointerInScreenAnyPort(&port)) {
        setNerve(GET_NERVE(Butterfly, HostTypeWait));
        return;
    }

    if (::sMoveDistanceMax < mPosition.distance(mHomePos)) {
        setNerve(GET_NERVE(Butterfly, HostTypeWait));
        return;
    }

    Vec3f pos;
    MR::calcStarPointerWorldPointingPos(&pos, mPosition, port);
    Vec3f vel = pos - mPosition;
    mVelocity += vel * ::sHiveAccel;

    if (::sHiveVelMax < mVelocity.length()) {
        mVelocity.setLength(::sHiveVelMax);
    }

    MR::setBckRate(this, MR::converge(MR::getBckRate(this), ::sHiveAnimRate, ::sAnimInterpolateRate));
}

void Butterfly::exePerchOn() {
    HitSensor* bodySensor = getSensor("body");
    if (MR::isNear(bodySensor, mPerchTarget, bodySensor->getRadius() + mPerchTarget->getRadius())) {
        mVelocity.zero();
        MR::setBckRate(this, MR::converge(MR::getBckRate(this), ::sPerchOnAnimRate, ::sAnimInterpolateRate));
    } else {
        // PerchOn can only happen from Hive
        // This runs the Hive nerve until the perch target is near
        Vec3f vel = mPerchTarget->getPosition() - mPosition;
        vel.setLength(::sHiveAccel);
        mVelocity += vel;

        if (::sHiveVelMax < mVelocity.length()) {
            mVelocity.setLength(::sHiveVelMax);
        }

        MR::setBckRate(this, MR::converge(MR::getBckRate(this), ::sHiveAnimRate, ::sAnimInterpolateRate));
    }
    tryRunAway();
}

void Butterfly::exeGotoSleepingMario() {
    if (MR::isLessStep(this, ::sGotoSleepingMarioWaitTime)) {
        return;
    }

    Pos3f jointMtx;
    MR::calcPlayerJointMtx(&jointMtx, "CapPosition");

    Vec3f posOffset;
    jointMtx.mult(::sMarioCapPosOffset, posOffset);

    Vec3f vel = posOffset - mPosition;
    // This can really only be ::sHiveAccel, but this nerve is only accessible by Wait
    vel.setLength(cNum_exeGotoSleepingMario_f1);
    mVelocity += vel;

    if (::sGotoSleepingMarioSpeedMax < mVelocity.length()) {
        mVelocity.setLength(::sGotoSleepingMarioSpeedMax);
    }

    if (mPosition.distance(posOffset) < ::sPerchOnNearDistance && MR::checkPassBckFrame(this, cNum_exeGotoSleepingMario_checkPassBckFrame_1)) {
        mVelocity.zero();
        setNerve(GET_NERVE(Butterfly, HostTypeReadyToPerchOnSleepingMario));
    }
}

void Butterfly::exeReadyToPerchOnSleepingMario() {
    Pos3f jointMtx;
    MR::calcPlayerJointMtx(&jointMtx, "CapPosition");

    Vec3f posOffset;
    jointMtx.mult(::sMarioCapPosOffset, posOffset);
    MR::vecBlend(mPosition, posOffset, &mPosition, ::sTransBlendRatio);

    MR::setBckRate(this, MR::converge(MR::getBckRate(this), ::sReadyToPerchOnAnimRate, ::sAnimInterpolateRate));
    if (MR::checkPassBckFrame(this, cNum_exeReadyToPerchOnSleepingMario_checkPassBckFrame_1)) {
        setNerve(GET_NERVE(Butterfly, HostTypePerchOnSleepingMario));
    }
}

void Butterfly::exePerchOnSleepingMario() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    Pos3f jointMtx;
    MR::calcPlayerJointMtx(&jointMtx, "CapPosition");

    jointMtx.mult(::sMarioCapPosOffset, mPosition);

    if (!MR::isPlayerSleeping()) {
        MR::startBck(this, "Butterfly");
        setNerve(GET_NERVE(Butterfly, HostTypeWait));
    }
}
