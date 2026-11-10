#include "Game/MapObj/MarioLauncher.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Animation/BckCtrl.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/TriangleFilter.hpp"
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_doAim_f1, "doAim.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_doAim_f2, "doAim.f2", 0.5f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_doFlight_reboundVelocityFromEachCollision_1, "doFlight.reboundVelocityFromEachCollision.1", 0.0f);

namespace {
    NEBULA_TUNABLE(f32, sEjectSpeed, 40.0f);
    NEBULA_TUNABLE(f32, sGravityScale, 0.8f);
    NEBULA_TUNABLE(f32, sGravityRatio, 0.95f);
    NEBULA_TUNABLE(f32, sShellRadius, 80.0f);
    NEBULA_TUNABLE(f32, sShellOffset, 180.0f);
    NEBULA_TUNABLE(f32, sGravityRoll, 0.01f);
    NEBULA_TUNABLE(f32, sLandingSpeedRatio, 0.4f);
    NEBULA_TUNABLE(s32, sLoopStart, 65);
    NEBULA_TUNABLE(s32, sDieStart, 300);
    NEBULA_TUNABLE(s32, sDispLaunch, 90);
    NEBULA_TUNABLE(f32, sPointerCameraYRot, 0.3f);
    NEBULA_TUNABLE(f32, sPointerCameraXRot, 0.3f);
    NEBULA_TUNABLE(f32, sPointerCameraElevation, 0.62f);
    NEBULA_TUNABLE(s32, sKillFrame, 360);
    NEBULA_TUNABLE(s32, sStartActiveFrame, 10);
    NEBULA_TUNABLE(s32, sInterpolateTest1, 15);
    NEBULA_TUNABLE(f32, sSensorRadius, 80.0f);
    NEBULA_TUNABLE(f32, sSensorOffsetY, 20.0f);
    NEBULA_TUNABLE(s32, sPrepFrame, 26);
    NEBULA_TUNABLE(s32, sRejectFrame, 20);
    static const s32 sWaitFrame = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(s32, sLandFrame, 1);
    NEBULA_TUNABLE(f32, sEarthenPipeInScale, 4.0f);
    NEBULA_TUNABLE(f32, sCameraYOffset, 150.0f);
    NEBULA_TUNABLE(f32, sCameraZOffset, 200.0f);
    NEBULA_TUNABLE(f32, sRejectJumpY, 10.0f);
    NEBULA_TUNABLE(f32, sRejectJumpZ, 10.0f);
    NEBULA_TUNABLE(s32, sInvalidCollideFrame, 0);
    NEBULA_TUNABLE(s32, sFrontVecFrame, 1);
    NEBULA_TUNABLE(f32, sReboundScale0, 0.0f);
    NEBULA_TUNABLE(f32, sReboundScale1, 0.0f);
    NEBULA_TUNABLE(f32, sReboundScale2, 0.0f);
};  // namespace

namespace NrvMarioLauncher {
    NEW_NERVE(MarioLauncherNrvWait, MarioLauncher, Wait);
    NEW_NERVE(MarioLauncherNrvReady, MarioLauncher, Ready);
    NEW_NERVE(MarioLauncherNrvPlayerIn, MarioLauncher, PlayerIn);
    NEW_NERVE(MarioLauncherNrvPrep, MarioLauncher, Prep);
    NEW_NERVE(MarioLauncherNrvAim, MarioLauncher, Aim);
    NEW_NERVE(MarioLauncherNrvFlight, MarioLauncher, Flight);
    NEW_NERVE(MarioLauncherNrvKill, MarioLauncher, Kill);
    NEW_NERVE(MarioLauncherNrvLand, MarioLauncher, Land);
    NEW_NERVE(MarioLauncherNrvReject, MarioLauncher, Reject);
};  // namespace NrvMarioLauncher

MarioLauncher::MarioLauncher(const char* pName)
    : LiveActor(pName), mLayout(), mShell(), mHost(), mIsInAttractor(), mCameraTargetMtx("カメラターゲットダミー") {
    lumacore::setClass(this, lumacore::kClassId< MarioLauncher >);
    mEffectMtxSmoke.identity();
    mEffectMtxGravity.identity();
    mPlayerBaseMtx.identity();
    mLauncherBaseMtx.identity();
    mPlayerMtx.identity();
}

void MarioLauncher::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("MarioLauncher", nullptr, false);
    initNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvWait));

    mShell = new MarioLauncherShell("移動用弾丸");
    mShell->init(rIter);
    mLayout = new MarioLauncherLayout();
    mLayout->init(rIter);

    MR::connectToSceneMapObjStrongLight(this);

    initHitSensor(2);
    MR::addHitSensor(this, "Bind", ATYPE_MARIO_LAUNCHER_BIND, 8, ::sSensorRadius, Vec3f(0.0f, ::sSensorOffsetY, 0.0f));
    MR::addHitSensorMtxRide(this, "Shell", 8, ::sShellRadius, mShell->mBaseMtx, Vec3f(0.0f, ::sShellOffset, 0.0f));
    MR::invalidateHitSensor(this, "Shell");

    MR::initCollisionParts(this, "MarioLauncher", getSensor("Bind"), nullptr);
    MR::validateCollisionParts(this);

    initEffectKeeper(0, nullptr);
    initSound(4, false);

    MR::initActorCameraProgrammable(this);
    MR::declareLauncherCamera();
    MR::declareLauncherFlightCamera();

    mLauncherType = LauncherType_Front;
    MR::getPlacementArg0NoInit(rIter, &mLauncherType);

    MR::startBck(this, "Wait");

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnOffA(this, MR::Functor(this, &MarioLauncher::onSwitchA), MR::Functor(this, &MarioLauncher::offSwitchA));
        MR::invalidateHitSensors(this);
    }

    if (MR::useStageSwitchWriteB(this, rIter)) {
    }

    MR::setBindTriangleFilter(mShell, MR::createTriangleFilterDelegator(this, &MarioLauncher::isBreak));
    makeActorAppeared();
}

void MarioLauncherShell::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneMapObjMovement(this);
    initBinder(::sShellRadius, ::sShellOffset, 8);
    makeActorDead();
}

void MarioLauncherLayout::init(const TableIter& /*rIter*/) {
    MR::connectToSceneLayout(this);
    initLayout();
}

void MarioLauncher::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckNoInterpole(this, "Wait");
        MR::validateHitSensor(this, "Bind");
    }
}

void MarioLauncher::exeReady() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("EarthenPipeReady");
        MR::startSound(this, "SE_PM_MARIO_LAUNCHER_PREP");
    }

    s32 maxFrame = MR::getBckFrameMaxPlayer();

    MR::blendMtx(mPlayerBaseMtx, mLauncherBaseMtx, MR::calcNerveRate(this, maxFrame), mPlayerMtx);

    Vec3f up;
    mLauncherBaseMtx.getYDir(up);

    if (MR::isBckStoppedPlayer()) {
        setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvPlayerIn));
    }
}

void MarioLauncher::exePlayerIn() {
    if (MR::isFirstStep(this)) {
        BckCtrlData data;
        data.mLoopMode = 0;
        data.mStartFrame = 0;
        data.mEndFrame = MR::getBckFrameMaxPlayer("EarthenPipeIn");
        data.mPlayFrame = (s32)(data.mEndFrame / ::sEarthenPipeInScale);

        MR::startBckPlayer("EarthenPipeIn", data);
        MR::startSound(mHost, "SE_PV_CATCH");
    }

    if (MR::isBckStoppedPlayer() && doPrep()) {
        setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvPrep));
    }
}

void MarioLauncher::exePrep() {
    if (MR::isStep(this, ::sStartActiveFrame)) {
        if (mLauncherType == LauncherType_Up) {
            MR::startBck(this, "LookUp");
        } else {
            MR::startBck(this, "Active");
        }
        MR::startSound(this, "SE_OJ_MARIO_LAUNCHER_IN");
    }

    Vec3f front;
    MR::calcFrontVec(&front, this);

    if (MR::isStep(this, ::sPrepFrame)) {
        MR::startSound(this, "SE_OJ_MARIO_LAUNCHER_BEND");
    }

    if (MR::isGreaterStep(this, ::sStartActiveFrame) && MR::isBckStopped(this)) {
        if (doAim()) {
            MR::setPlayerFrontVec(front, ::sFrontVecFrame);
            setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvAim));
        }
    }
}

void MarioLauncher::exeAim() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
    }

    if (MR::isStarPointerInScreen(PLAYER_1)) {
        mLayout->appearAt(MR::getStarPointerScreenPositionOrEdge(PLAYER_1));
        MR::requestMarioLauncherGuidance();
    } else {
        mLayout->mLayout->kill();
        MR::requestMarioLauncherGuidance();
    }
    MR::startSystemLevelSE("SE_SY_LV_MARIO_LAUNCHER_AIM");

    if (MR::testSystemTriggerA() || MR::isPressedJump(PLAYER_1)) {
        if (doEject()) {
            setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvFlight));
        }
        return;
    }

    if (MR::testSystemTriggerB() || MR::isPressedFire(PLAYER_1)) {
        if (doReject()) {
            setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvReject));
        }
        return;
    }
}

void MarioLauncher::exeFlight() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_PM_MARIO_LAUNCHER_LAUNCH");
        MR::startSound(mHost, "SE_PV_SHOOT_UP");
    }

    MR::startLevelSound(mHost, "SE_PM_LV_MARIO_LAUNCHER_FLY");

    if (MR::isStep(this, ::sLoopStart)) {
        MR::startSound(mHost, "SE_PV_JUMP_JOY");
        MR::startBckPlayer("CannonFlyLoop", ::sInterpolateTest1);
    }

    if (MR::isStep(this, ::sDieStart)) {
        MR::startBckPlayer("DieCannonFly", ::sInterpolateTest1);
    }

    if (MR::isStep(this, ::sDispLaunch)) {
        MR::showModel(this);
    }

    if (MR::isGreaterEqualStep(this, ::sKillFrame)) {
        MR::forceKillPlayerByAbyss();
        setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvKill));
        return;
    }

    if (doFlight()) {
        if (doLanding(-1)) {
        }
    }
}

void MarioLauncher::exeKill() {
    mShell->calcBaseMtx();
    mPlayerMtx.set(mShell->getBaseMtx());
    MR::calcPlayerJointMtx(&mEffectMtxSmoke, "Center");
    mEffectMtxGravity = mEffectMtxSmoke;
}

void MarioLauncherShell::calcBaseMtx() {
    Quat4f rot;

    Vec3f vel = mVelocity;
    Vec3f grav = mGravity;
    mReboundDir.set< f32 >(mPosition.x - mBaseMtx[0][3],   //
                           mPosition.y - mBaseMtx[1][3],   //
                           mPosition.z - mBaseMtx[2][3]);  //

    if (!MR::isNearZero(vel)) {
        MR::normalize(&vel);

        Vec3f front;
        mBaseMtx.getYDir(front);
        Vec3f down;
        mBaseMtx.getZDir(down);

        if (!MR::isNearZero(grav)) {
            MR::normalize(&grav);

            if (down.dot(grav) < 0.0f) {
                rot.setRotate(-down, grav, ::sGravityRoll);
            } else {
                rot.setRotate(down, grav, ::sGravityRoll);
            }

            mRotQuat.mult(rot);
            mBaseMtx.setQuat(mRotQuat);
        }

        rot.setRotate(front, vel);
        mRotQuat.mult(rot);
        mBaseMtx.setQuat(mRotQuat);
    }

    mBaseMtx[0][3] = mPosition.x;
    mBaseMtx[1][3] = mPosition.y;
    mBaseMtx[2][3] = mPosition.z;
}

MtxPtr MarioLauncherShell::getBaseMtx() const {
    return const_cast< Pos3f& >(mBaseMtx);
}

void MarioLauncher::exeReject() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_MARIO_LAUNCHER_OUT");
    }

    if (MR::isStep(this, ::sRejectFrame - 2)) {
        MR::showPlayer();
    }

    if (MR::isStep(this, ::sRejectFrame)) {
        Vec3f jumpY;
        MR::extractMtxYDir(getBaseMtx(), &jumpY);
        Vec3f jumpZ;
        MR::extractMtxZDir(getBaseMtx(), &jumpZ);
        MR::endBindAndPlayerJump(this, jumpY * ::sRejectJumpY - jumpZ * ::sRejectJumpZ, 0);
        MR::startSoundPlayer("SE_PV_JUMP_S");
    }

    if (MR::isStep(this, ::sWaitFrame)) {
        MR::validateHitSensor(this, "Bind");
        setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvWait));
    }
}

void MarioLauncher::exeLand() {
    if (MR::isGreaterEqualStep(this, ::sLandFrame)) {
        MR::endLauncherFlightCamera();
        MR::validateClipping(this);
        MR::validateClipping(mShell);
        setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvWait));
    }
}

void MarioLauncher::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorRide(pSender)) {
        if (MR::tryGetItem(pSender, pReceiver)) {
            return;
        }
    }
}

bool MarioLauncher::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, "Shell")) {
        return doLanding(msg);
    }

    return false;
}

bool MarioLauncher::receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensor(pReceiver, "Shell")) {
        MR::sendMsgPlayerPunch(pSender, pReceiver);
        return doLanding(-1);
    }

    return false;
}

bool MarioLauncher::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (doReady(pSender)) {
            setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvReady));
            return true;
        }
    }

    if (MR::isMsgRushCancel(msg)) {
        return doLanding(-1);
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        MR::setPlayerBaseMtx(mPlayerMtx);
        return true;
    }

    return false;
}

bool MarioLauncher::doReady(HitSensor* pSensor) {
    if (!isNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvWait))) {
        return false;
    }

    mHost = pSensor->mHost;
    mPlayerBaseMtx.set(MR::getPlayerBaseMtx());
    mPlayerMtx.set(mPlayerBaseMtx);
    mLauncherBaseMtx.set(getBaseMtx());

    MR::tryPlayerKillTakingActor();

    return true;
}

bool MarioLauncher::doPrep() {
    if (!isNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvPlayerIn))) {
        return false;
    }

    MR::invalidateHitSensor(this, "Bind");
    MR::invalidateClipping(this);
    MR::invalidateClipping(mShell);

    Pos3f mtx;
    mtx.set(getBaseMtx());
    MR::addTransMtxLocalY(mtx, ::sCameraYOffset);
    if (mLauncherType == LauncherType_Front) {
        MR::addTransMtxLocalZ(mtx, ::sCameraZOffset);
    }
    mCameraTargetMtx.setMtx(mtx);
    mShell->mBaseMtx.set(mtx);
    mShell->mPosition.set(mPosition);
    mShell->mRotation.set(mRotation);
    mShell->mGravity.zero();

    MR::hidePlayer();
    MR::resetPlayerStatus();
    MR::setPlayerStateWait();

    return true;
}

bool MarioLauncher::doAim() {
    mLayout->appearAt(MR::getStarPointerScreenPositionOrEdge(PLAYER_1));
    MR::requestMarioLauncherGuidance();
    mLayout->appear();

    MR::startSystemSE("SE_SY_MARIO_LAUNCHER_IN");
    MR::startLauncherCamera(CameraTargetArg(&mCameraTargetMtx));
    MR::resetCameraMan();

    mPlayerMtx.set(mCameraTargetMtx.mMatrix);

    if (mLauncherType == LauncherType_Up) {
        MR::setLauncherCameraAngle(::sPointerCameraYRot, ::sPointerCameraXRot, MR::pi() / cNum_doAim_f1, -1.0f);
    } else {
        MR::setLauncherCameraAngle(::sPointerCameraYRot, ::sPointerCameraXRot + cNum_doAim_f2, ::sPointerCameraElevation, -1.0f);
    }

    if (MR::isValidSwitchB(this)) {
        MR::onSwitchB(this);
    }

    MR::startStarPointerModeMarioLauncher(this);
    setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvAim));

    return true;
}

bool MarioLauncher::doReject() {
    if (MR::isValidSwitchB(this)) {
        MR::offSwitchB(this);
    }

    Pos3f mtx;
    mtx.set(getBaseMtx());
    MR::addTransMtxLocalY(mtx, ::sCameraYOffset);
    MR::addTransMtxLocalZ(mtx, 0.0f);
    mPlayerMtx.set(mtx);

    MR::showModel(this);
    MR::startBckNoInterpole(this, "Wait");
    MR::endLauncherCamera();
    MR::resetCameraMan();
    MR::endStarPointerMode(this);
    MR::invalidateHitSensor(this, "Shell");
    mLayout->kill();

    return true;
}

void MarioLauncherLayout::kill() {
    mLayout->kill();
    LayoutActor::kill();
}

bool MarioLauncher::doEject() {
    if (!MR::isStarPointerInScreen(PLAYER_1)) {
        return false;
    }

    Vec3f back;
    MR::calcUpVec(&back, this);
    if (mLauncherType == LauncherType_Up) {
        MR::calcFrontVec(&back, this);
        back *= -1.0f;
    }

    Vec3f shootDir;
    MR::calcWorldRayDirectionFromScreen(&shootDir, *MR::getStarPointerScreenPosition(PLAYER_1));
    MR::normalize(&shootDir);

    mShell->getPosition().set(mCameraTargetMtx.getPosition());
    mShell->mGravity.zero();
    mShell->mVelocity.set(shootDir * ::sEjectSpeed);
    MR::makeMtxUpFront(&mShell->mBaseMtx, shootDir, -back);

    mShell->mBaseMtx.getQuat(mShell->mRotQuat);
    mShell->makeActorAppeared();

    MR::showPlayer();
    MR::startBckPlayer("CannonFlyStart");

    MR::shakeCameraStrong();
    MR::tryRumblePadStrong(this, PLAYER_1);

    MR::setEffectHostMtx(this, "LocusSmoke", mEffectMtxSmoke);
    MR::setEffectHostMtx(this, "Gravity", mEffectMtxGravity);

    MR::validateHitSensor(this, "Shell");
    mLayout->kill();

    MR::emitEffect(this, "LocusSmoke");
    mIsInAttractor = false;
    MR::endStarPointerMode(this);
    MR::endLauncherCamera();
    MR::startLauncherFlightCamera(60);

    return true;
}

bool MarioLauncher::doFlight() {
    bool isInAttractor = false;

    MR::calcPlayerJointMtx(&mEffectMtxSmoke, "Center");
    mEffectMtxGravity = mEffectMtxSmoke;

    Vec3f attract;
    MR::calcAttractMarioLauncherOrZero(mShell, &attract, nullptr, 0);

    if (!MR::isNearZero(attract)) {
        isInAttractor = true;
    }

    Vec3f currGrav;
    currGrav = mShell->mGravity;
    attract *= (1.0f - ::sGravityRatio);
    currGrav *= ::sGravityRatio;
    mShell->mGravity = attract + currGrav;

    if (isInAttractor != mIsInAttractor) {
        mIsInAttractor = isInAttractor;
    }

    Quat4f gravRot;
    Vec3f grav = mShell->mGravity;

    if (isInAttractor) {
        MR::normalizeOrZero(&grav);

        Vec3f up, gravityEffectPos;

        mEffectMtxGravity.getTrans(gravityEffectPos);
        mEffectMtxGravity.setTrans(Vec3f(0.0f, 0.0f, 0.0f));
        mEffectMtxGravity.getYDir(up);

        gravRot.setRotate(-up, grav);

        Pos3f gravRotMtx;
        gravRotMtx.setQuat(gravRot);

        Pos3f rot;
        rot.concat(gravRotMtx, mEffectMtxGravity);
        mEffectMtxGravity.set(rot);
        mEffectMtxGravity.setTrans(gravityEffectPos);
        MR::emitEffect(this, "Gravity");
    } else {
        MR::deleteEffect(this, "Gravity");
    }

    if (testHitCollision()) {
        MR::reboundVelocityFromEachCollision(mShell, ::sReboundScale0, ::sReboundScale1, ::sReboundScale2, cNum_doFlight_reboundVelocityFromEachCollision_1);
        return MR::isGreaterEqualStep(this, ::sInvalidCollideFrame);
    }

    MR::addVelocityToGravity(mShell, ::sGravityScale);
    MR::restrictVelocity(mShell, ::sEjectSpeed);
    mShell->calcBaseMtx();

    mPlayerMtx.set(mShell->getBaseMtx());
    if (MR::isInWater(mShell->mPosition)) {
        return true;
    }

    return false;
}

bool MarioLauncher::doLanding(s32 msg) {
    if (!MR::isPlayerInBind()) {
        return false;
    }

    if (MR::isValidSwitchB(this)) {
        MR::offSwitchB(this);
    }

    MR::invalidateHitSensor(this, "Shell");
    MR::deleteEffect(this, "LocusSmoke");
    MR::deleteEffect(this, "Gravity");

    if (!MR::isBindedGround(mShell)) {
        MR::startBckPlayer("WallHit", ::sInterpolateTest1);
    }

    if (msg != -1) {
        MR::endBindAndPlayerDamageMsg(this, msg, mShell->mReboundDir * ::sLandingSpeedRatio);
    } else {
        MR::endBindAndPlayerJump(this, mShell->mReboundDir * ::sLandingSpeedRatio, 0);
    }

    mShell->makeActorDead();
    MR::showModel(this);
    mHost = nullptr;

    MR::startBck(this, "Wait");
    setNerve(GET_NERVE(MarioLauncher, MarioLauncherNrvLand));

    return true;
}

bool MarioLauncher::testHitCollision() {
    return MR::isBinded(mShell);
}

bool MarioLauncher::isBreak(const Triangle* pTriangle) {
    if (pTriangle->getSensor() == getSensor("Bind")) {
        return true;
    }

    return MR::sendArbitraryMsg(ACTMES_LAUNCHER_BREAKABLE, pTriangle->getSensor(), getSensor("Shell"));
}

void MarioLauncher::onSwitchA() {
    MR::validateHitSensor(this, "Bind");
}

void MarioLauncher::offSwitchA() {
    MR::invalidateHitSensor(this, "Bind");
}

void MarioLauncherShell::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
}
