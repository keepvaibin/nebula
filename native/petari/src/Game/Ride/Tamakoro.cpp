#include "Game/Ride/Tamakoro.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <nebula_game_api.h>
#include "Game/GameAudio/AudTamakoroBgmCtrl.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Ride/TamakoroTutorial.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_math.h>
#include "Game/System/NebulaInput.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 mBaseRadius = 150.0f;
    const f32 sBindableUpperDegree = 60.0f;
    const s32 sBindStartTime = 40;
    const f32 sBindStartJumpHeight = 150.0f;
    const f32 sBindStartUpAdjustRate = 0.1f;
    const f32 sBindStartFrontAdjustRate = 0.1f;
    const s32 sBindStartLandTime = 14;
    const f32 sLandStartPositionDegree = 35.0f;
    const f32 sLandStartVelocityDegree = 40.0f;
    const f32 sGravityAccel = 2.0f;
    const f32 sGroundFric = 0.99f;
    const f32 sIdleBckRate = 1.0f;
    const f32 sSoftWalkSpeed = 0.25f;
    const f32 sSoftWalkBckRate = 1.0f;
    const f32 sWalkSpeed = 6.0f;
    const f32 sWalkBckRate = 2.0f;
    const f32 sRunSpeed = 18.0f;
    const f32 sRunBckRate = 3.0f;
    const f32 sRunMaxBckRate = 4.0f;
    const s32 sFallStartTime = 5;
    const f32 sFallOffsetSpeed = 3.0f;
    const f32 sJumpPowerV = 40.0f;
    const f32 sJumpPowerH = 7.0f;
    const f32 sJumpOffsetSpeed = 4.0f;
    const f32 sMarioJumpOffsetMax = 150.0f;
    const f32 sMarioOffsetGravityAccel = 1.0f;
    const f32 sMarioOffsetFric = 0.99f;
    const f32 sNeedLandPower = 20.0f;
    const s32 sLandTime = 2;
    const f32 sLandFric = 0.88f;
    const f32 sNeedBumpWallPower = 10.0f;
    const f32 sCollisionStrongPower = 60.0f;
    const f32 sCollisionMiddlePower = 25.0f;
    const f32 sCollisionWeakPower = 5.0f;
    const f32 sStampReactionV = 30.0f;
    const f32 sStampReactionInvalidSpeed = 15.0f;
    const f32 sExplosionReactionH = 20.0f;
    const f32 sExplosionReactionV = 40.0f;
    const f32 sRaidRailFastSpeed = 30.0f;
    const f32 sRollBckRate = 1.5f;
    const s32 sDashRailTime = 40;
    const f32 sDashRailFric = 0.99f;
    const f32 sDashBallRollSpeed = 60.0f;
    const f32 sEndBindFrontPower = 5.0f;
    const f32 sEndBindJumpPower = 35.0f;
    static const s32 sForceBindEndTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    const f32 sMarioUpAdjustRate = 0.1f;
    const f32 sMarioFrontAdjustRate = 0.25f;
    const f32 sStandDirInterRate = 0.02f;
    const f32 sMarioUpperYAccel = 5.0f;
    const f32 sMarioUpperYFric = 0.9f;
    const f32 sRingMaxDegree = 45.0f;
    const f32 sRingMaxAngleSpeed = 4.0f;
    const f32 sTutorialAccel = 0.5f;
    const f32 sTutorialFric = 0.95f;
    const s32 sBgmStateChangeFrames = 30;

    // Tilt steering (player 1's main controller): the rest angle in the YZ plane, a dead zone, and the tilt that
    // reaches full input.
    const f32 cTiltBaseDegreeYZ = 10.0f;
    const f32 cTiltDegreeMargin = 5.0f;
    const f32 cTiltDegreeRange = 25.0f;

    // Signed angle from rV2 to rV1; both unit length.
    f32 calcSignedAngle(const Vec2f& rV1, const Vec2f& rV2) {
        const f32 orientation = rV1.y * rV2.x - rV1.x * rV2.y;
        f32 angle = MR::acos(rV1.dot(rV2));
        if (orientation < 0.0f) {
            angle = -angle;
        }

        return angle;
    }

    // Removes the dead zone and scales the rest of the tilt to [-1, 1] over the range.
    f32 mapTilt(f32 angle, f32 margin, f32 range) {
        if (MR::abs(angle) < margin) {
            return 0.0f;
        }

        if (angle > 0.0f) {
            return (angle - margin) / (range - margin);
        }

        return (angle + margin) / (range - margin);
    }

    void calcTiltXY(f32* pX, f32* pY) {
        const f32 margin = MR::toRadian(::cTiltDegreeMargin);
        const f32 range = MR::toRadian(::cTiltDegreeRange);
        const f32 baseAngleYZ = MR::toRadian(::cTiltBaseDegreeYZ);

        Vec3f tilt;
        MR::getMainTilt(&tilt, PLAYER_1);

        f32 angleXY = 0.0f;
        Vec2f tiltXY(tilt.x, MR::abs(tilt.y));
        if (!tiltXY.isZero()) {
            MR::normalizeOrZero(&tiltXY);
            angleXY = MR::asin(tiltXY.x);
        }

        f32 angleYZ = 0.0f;
        Vec2f tiltYZ(-tilt.y, tilt.z);
        if (!tiltYZ.isZero()) {
            MR::normalizeOrZero(&tiltYZ);
            angleYZ = ::calcSignedAngle(tiltYZ, Vec2f(MR::cos(baseAngleYZ), MR::sin(baseAngleYZ)));
        }

        *pX = ::mapTilt(angleXY, margin, range);
        *pY = ::mapTilt(angleYZ, margin, range);
    }

    // The tilt as a move direction on the plane facing rGravity, by the camera's axes; returns its strength in [0, 1].
    f32 calcTiltMove(Vec3f* pMoveDir, const Vec3f& rGravity) {
        f32 x = 0.0f;
        f32 y = 0.0f;
        ::calcTiltXY(&x, &y);

        f32 mag = x * x + y * y;
        mag = FloatUtil::sqrt(mag);

        if (mag >= 0.0001f) {
            Rot3f invMtx = MR::getCameraInvViewMtx();

            Vec3f dirX;
            Vec3f dirY;
            Vec3f dirZ;

            invMtx.getXDir(dirX);
            invMtx.getYDir(dirY);
            invMtx.getZDir(dirZ);

            MR::normalizeOrZero(&dirX);
            MR::normalizeOrZero(&dirY);
            MR::normalizeOrZero(&dirZ);

            if (dirY.dot(-rGravity) < 0.0f) {
                dirX = -dirX;
                dirY = -dirY;
                dirZ = -dirZ;
            }

            Rot3f rotMtx;
            rotMtx.setRotate(dirY, -rGravity);
            rotMtx.mult33(dirX);
            rotMtx.mult33(dirZ);
            pMoveDir->set(dirX * x - dirZ * y);

            MR::separateScalarAndDirection(&mag, pMoveDir, *pMoveDir);
        } else {
            pMoveDir->zero();
        }

        if (mag > 1.0f) {
            mag = 1.0f;
        }

        return mag;
    }
};  // namespace

namespace NrvTamakoro {
    NEW_NERVE(TamakoroNrvStandByTutorial, Tamakoro, StandByTutorial);
    NEW_NERVE(TamakoroNrvStandByBind, Tamakoro, StandByBind);
    NEW_NERVE(TamakoroNrvBindStart, Tamakoro, BindStart);
    NEW_NERVE(TamakoroNrvBindStartLand, Tamakoro, BindStartLand);
    NEW_NERVE(TamakoroNrvTutorial, Tamakoro, Tutorial);
    NEW_NERVE(TamakoroNrvWait, Tamakoro, Wait);
    NEW_NERVE(TamakoroNrvFall, Tamakoro, Fall);
    NEW_NERVE(TamakoroNrvJump, Tamakoro, Jump);
    NEW_NERVE(TamakoroNrvBumpWall, Tamakoro, BumpWall);
    NEW_NERVE(TamakoroNrvLand, Tamakoro, Land);
    NEW_NERVE(TamakoroNrvJumpHole, Tamakoro, JumpHole);
    NEW_NERVE(TamakoroNrvJumpHoleSetUp, Tamakoro, JumpHoleSetUp);
    NEW_NERVE_ONEND(TamakoroNrvJumpHoleLaunch, Tamakoro, JumpHoleLaunch, JumpHoleLaunch);
    NEW_NERVE(TamakoroNrvDashRail, Tamakoro, DashRail);
    NEW_NERVE(TamakoroNrvDashRailEnd, Tamakoro, DashRailEnd);
    NEW_NERVE(TamakoroNrvRideRail, Tamakoro, RideRail);
    NEW_NERVE(TamakoroNrvRideRailFastStart, Tamakoro, RideRailFastStart);
    NEW_NERVE(TamakoroNrvRideRailFast, Tamakoro, RideRailFast);
    NEW_NERVE(TamakoroNrvRideRailFastEnd, Tamakoro, RideRailFastEnd);
    NEW_NERVE(TamakoroNrvBindEnd, Tamakoro, BindEnd);
};  // namespace NrvTamakoro

Tamakoro::Tamakoro(const char* pName)
    : LiveActor(pName), mTutorial(nullptr), mJointCtrl(nullptr), mBallRotateQuat(0, 0, 0, 1), mMarioRotateQuat(0, 0, 0, 1),
      mRingUp(0, 1, 0), mKickVel(0, 0, 0), mMarioBindRequestPos(0, 0, 0), mMarioUp(0, 1, 0), mMarioPos(0, 0, 0), mMoment(0, 0, 0),
      mMarioFront(0, 0, 1), mDirectionToMario(0, 0, 0), mAccelDir(0, 0, 0), mAccelRate(0.0f), mMarioRotateYAngle(0.0f), mMarioOffset(0.0f),
      mMarioOffsetVelocity(0.0f), mAirTime(0), mBgmCtrl(nullptr), mControlDisabled(false), mHasTutorial(false) {
    lumacore::setClass(this, lumacore::kClassId< Tamakoro >);
    mBgmCtrl = new AudTamakoroBgmCtrl();
}

void Tamakoro::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Tamakoro", nullptr, false);
    MR::connectToScene(this, MR::MovementType_Ride, MR::CalcAnimType_Ride, MR::DrawBufferType_IndirectMapObjStrongLight, MR::DrawType_None);
    MR::makeQuatFromRotate(&mBallRotateQuat, this);
    MR::initShadowVolumeSphere(this, mBaseRadius);
    initEffectKeeper(0, nullptr);
    initSensor();
    initSound(4, false);
    initJointControl();
    MR::onCalcGravity(this);
    initBinder(mBaseRadius, 0.0f, 16);
    initNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByBind));

    MR::declarePowerStar(this);
    bool hasPowerStar = MR::hasPowerStarInCurrentStageWithDeclarer(mName, -1);
    MR::startBva(this, "Tamakoro");
    MR::setBvaFrameAndStop(this, static_cast< s32 >(hasPowerStar));
    MR::useStageSwitchWriteA(this, rIter);

    if (mHasTutorial) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByTutorial));
        mTutorial = new TamakoroTutorial("タマコロチュートリアル");
        mTutorial->mHost = this;
        MR::initChildObj(mTutorial, rIter, 0);
    }
    makeActorAppeared();
}

void Tamakoro::initSensor() {
    initHitSensor(3);
    MR::addHitSensor(this, "Body", ATYPE_SPHERE_PLAYER, 8, mBaseRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "Hit", ATYPE_SPHERE_PLAYER_HIT, 8, mBaseRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensor(this, "Bind", ATYPE_SPHERE_PLAYER_BIND, 8, mBaseRadius + 15.0f, Vec3f(0.0f, 0.0f, 0.0f));
}

void Tamakoro::initJointControl() {
    mJointCtrl = MR::createJointDelegatorWithNullChildFunc(this, &Tamakoro::ringMtxCallBack, "Ring");
}

bool Tamakoro::ringMtxCallBack(Pos3f* pMtx, const JointControllerInfo& /*pJointCtrlInfo*/) {
    Vec3f trans;
    pMtx->getTrans(trans);

    if (MR::isNearZero(mAccelDir) || MR::isSameDirection(mAccelDir, mRingUp)) {
        MR::makeMtxUpNoSupport(pMtx, mRingUp);
    } else {
        MR::makeMtxUpFront(pMtx, mRingUp, mAccelDir);
    }

    pMtx->setTrans(trans);
    return true;
}

void Tamakoro::control() {
    updateMoment();
    MR::rotateQuatMoment(&mBallRotateQuat, mMoment);
    updateAirTime();
    updateRingUpVec();
    f32 speed = mVelocity.length();
    if (isNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHole)) || isNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHoleSetUp))) {
        speed = 0.0f;
    }
    mBgmCtrl->control(speed, mControlDisabled, ::sBgmStateChangeFrames);
    mControlDisabled = false;
}

void Tamakoro::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mBallRotateQuat);
    mJointCtrl->registerCallBack();
}

void Tamakoro::updateBindActorMatrix() {
    if (isUseMarioOffset()) {
        mMarioPos.scaleAdd(mBaseRadius, mDirectionToMario, mPosition);
        if (mMarioOffset > 0.0f) {
            mMarioPos.scaleAdd(-mMarioOffset, mGravity, mMarioPos);
        }
    }

    Pos3f mtx;
    mtx.setQT(mMarioRotateQuat, mMarioPos);
    MR::setPlayerBaseMtx(mtx);
}

void Tamakoro::updateRingUpVec() {
    Vec3f up = -mGravity;

    if (!MR::isNearZero(mAccelDir)) {
        Vec3f tiltUp;
        tiltUp.scaleAdd(mAccelRate, mAccelDir, up);
        MR::normalizeOrZero(&tiltUp);
        MR::turnVecToVecDegree(&up, up, tiltUp, ::sRingMaxDegree, Vec3f(0, 1, 0));
    }

    MR::turnVecToVecDegree(&mRingUp, mRingUp, up, ::sRingMaxAngleSpeed, Vec3f(0, 1, 0));
}

void Tamakoro::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pSender->isType(ATYPE_SPHERE_PLAYER) && MR::tryGetItem(pSender, pReceiver)) {
        return;
    }

    if ((MR::isSensorPlayer(pReceiver) || MR::isSensorEnemy(pReceiver)) && isEnablePushPlayer()) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (isEnableEnemyAttack() && MR::isSensorEnemy(pReceiver) && pSender->isType(ATYPE_SPHERE_PLAYER)) {
        if (MR::sendMsgPlayerTrample(pReceiver, pSender)) {
            f32 downVel = mVelocity.dot(mGravity);
            if (downVel < ::sStampReactionInvalidSpeed) {
                downVel = -::sStampReactionV - downVel;
                mVelocity.add(mGravity * downVel);
                MR::tryRumblePadWeak(this, PLAYER_1);
                MR::shakeCameraNormalWeak();
            }
            return;
        }
    }

    if (isEnableBallBind() && pSender == getSensor("Body")) {
        if (isBindedSphereDash(pSender, pReceiver)) {
            setNerve(GET_NERVE(Tamakoro, TamakoroNrvDashRail));
            return;
        }

        if (isBindedJumpHole(pSender, pReceiver)) {
            setNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHole));
            return;
        }

        if (isBindedBallRail(pSender, pReceiver)) {
            setNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRail));
            return;
        }
    }
}

bool Tamakoro::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgStarPieceReflect(msg);
}

bool Tamakoro::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgExplosionAttack(msg) && isEnableEnemyAttack()) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::setVelocitySeparateHV(this, pSender, pReceiver, ::sExplosionReactionH, ::sExplosionReactionV);
        MR::startSoundPlayer("SE_PV_UPSET");
        return true;
    }
    return false;
}

bool Tamakoro::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgAutoRushBegin(msg) && requestBind(pSender)) {
        NebulaHost::gameContext(NEBULA_CONTEXT_TILT_SECTION, 1);
        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        updateBindActorMatrix();
        return true;
    }

    if (msg == ACTMES_RUSH_FORCE_CANCEL) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByBind));
        return true;
    }

    if (msg == ACTMES_SET_UP_JUMP_HOLE && isNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHole))) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHoleSetUp));
        return true;
    }

    if (msg == ACTMES_SHOOT_JUMP_HOLE && isNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHoleSetUp))) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHoleLaunch));
        return true;
    }

    if (msg == ACTMES_END_JUMP_HOLE && isNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHoleLaunch))) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvFall));
        return true;
    }

    if (msg == ACTMES_END_RAIL_DASH && isNerve(GET_NERVE(Tamakoro, TamakoroNrvDashRail))) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvDashRailEnd));
        return true;
    }

    if (msg == ACTMES_END_BALL_RAIL && isRideRail()) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvWait));
        return true;
    }

    if (msg == ACTMES_END_BALL_BIND && requestEndBind()) {
        return true;
    }

    return false;
}

bool Tamakoro::requestBind(HitSensor* pSensor) {
    if (!isNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByBind))) {
        return false;
    }

    // Only from above: within sBindableUpperDegree of up.
    Vec3f vThisToSensor = MR::getSensorPos(pSensor) - mPosition;
    MR::normalizeOrZero(&vThisToSensor);
    f32 cosPositionDegree = vThisToSensor.dot(-mGravity);
    if (cosPositionDegree < MR::cosDegree(::sBindableUpperDegree)) {
        return false;
    }

    MR::invalidateClipping(this);

    MR::makeQuatRotateDegree(&mMarioRotateQuat, *MR::getPlayerRotate());
    mMarioPos.set(*MR::getPlayerPos());
    mMarioRotateQuat.getZDir(mMarioFront);
    mMarioFront.orthogonalize(mGravity);
    if (MR::normalizeOrZero(&mMarioFront)) {
        MR::makeAxisVerticalZX(&mMarioFront, mGravity);
    }

    mMarioBindRequestPos.set(mMarioPos);
    mMarioRotateYAngle = 0.0f;
    mAccelRate = 0.0f;
    mMarioOffset = 0.0f;
    mMarioOffsetVelocity = 0.0f;

    mAccelDir.zero();
    MR::zeroVelocity(this);
    MR::invalidateHitSensor(this, "Bind");

    Vec3f vel(*MR::getPlayerVelocity());
    MR::normalizeOrZero(&vel);
    f32 cosVelocityDegree = -vThisToSensor.dot(vel);

    // Falling onto the top lands on the ball; anything else climbs on.
    if (cosVelocityDegree > MR::cosDegree(::sLandStartVelocityDegree) && cosPositionDegree > MR::cosDegree(::sLandStartPositionDegree)) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvBindStartLand));
    } else {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvBindStart));

        Vec3f vPlayerToThis = mPosition - mMarioPos;
        vPlayerToThis.orthogonalize(mGravity);

        if (!MR::normalizeOrZero(&vPlayerToThis)) {
            if (vPlayerToThis.dot(mMarioFront) > 0.0f) {
                mMarioFront.set(vPlayerToThis);
            } else {
                mMarioFront.set(-vPlayerToThis);
            }
        }
    }

    if (!isNeedTutorial()) {
        MR::stopStageBGM(40);
    }
    return true;
}

bool Tamakoro::requestEndBind() {
    if (!isNerve(GET_NERVE(Tamakoro, TamakoroNrvBindEnd)) && !isNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByBind))) {
        MR::startBckPlayer("SwingRopeSpin");
        MR::endBindAndPlayerForceJump(this, mMarioFront * (-::sEndBindFrontPower) + mGravity * (-::sEndBindJumpPower), 0);
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
        MR::invalidateClipping(this);
        MR::zeroVelocity(this);
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvBindEnd));
        return true;
    }
    return false;
}

void Tamakoro::startRide() {
    if (isNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByTutorial))) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByBind));
    }
}

bool Tamakoro::requestTutorialEnd() {
    if (isNerve(GET_NERVE(Tamakoro, TamakoroNrvTutorial))) {
        MR::zeroVelocity(this);

        if (MR::isEqualStageName("HellProminenceGalaxy")) {
            MR::startStageBGM("BGM_TAMAKORO_2", false);
        } else {
            MR::startStageBGM("BGM_TAMAKORO", false);
        }

        mBgmCtrl->init();
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvWait));
        return true;
    }
    return false;
}

bool Tamakoro::tryFall() {
    if (mAirTime > ::sFallStartTime) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvFall));
        return true;
    }
    return false;
}

bool Tamakoro::tryJump() {
    if (MR::isPressedJump(PLAYER_1) && MR::isBindedGround(this)) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvJump));
        return true;
    }
    return false;
}

bool Tamakoro::tryBumpWall() {
    if (MR::calcHitPowerToWall(this) > ::sNeedBumpWallPower) {
        f32 rebound = -0.6f;
        if (isEnebleHitCollisionMessage()) {
            if (MR::sendMsgToWallSensor(ACTMES_BALL_DASH_WALL, getSensor("Body"))) {
                rebound = 0.3f;
            }
        }
        MR::reboundVelocityFromCollision(this, rebound, 0.0f, 1.0f);
        mMarioOffsetVelocity = 2.0f;
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvBumpWall));
        return true;
    }
    return false;
}

bool Tamakoro::tryLand() {
    const f32 power = MR::calcHitPowerToGround(this);

    if (power > ::sNeedLandPower) {
        f32 rebound = 0.0f;
        if (isEnebleHitCollisionMessage()) {
            if (MR::sendMsgToWallSensor(ACTMES_BALL_DASH_GROUND, getSensor("Body"))) {
                rebound = 0.3f;
            }
        }
        mMarioOffsetVelocity = -power;
        mMarioOffset -= power;
        MR::reboundVelocityFromCollision(this, rebound, 0.0f, 1.0f);
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvLand));
        return true;
    }

    if (MR::isGreaterStep(this, ::sLandTime) && MR::isBindedGround(this)) {
        mMarioOffsetVelocity = -power;
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvWait));
        return true;
    }

    return false;
}

void Tamakoro::reactionCollision() {
    const f32 wallHitPower = MR::calcHitPowerToWall(this);
    const f32 groundHitPower = MR::calcHitPowerToGround(this);
    const f32 hitPower = wallHitPower >= groundHitPower ? wallHitPower : groundHitPower;

    if (wallHitPower > ::sCollisionWeakPower) {
        MR::emitEffectHit(this, *MR::getWallHitPos(this), *MR::getWallNormal(this), "WallHit");
    }

    if (groundHitPower > ::sCollisionMiddlePower) {
        MR::emitEffectHit(this, *MR::getGroundHitPos(this), *MR::getGroundNormal(this), "Land");
    }

    if (wallHitPower > ::sCollisionWeakPower) {
        MR::startSound(this, "SE_SM_IRONSPH_HIT");
        MR::startSoundPlayer("SE_PV_GUARD");
    }

    if (hitPower > ::sCollisionStrongPower) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraStrong();
    } else if (hitPower > ::sCollisionMiddlePower) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormalWeak();
    } else if (hitPower > ::sCollisionWeakPower) {
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::shakeCameraWeak();
    }
}

void Tamakoro::exeStandByTutorial() {
    updateVelocityNormal(0.95f);
}

void Tamakoro::exeStandByBind() {
    updateVelocityNormal(0.95f);
}

void Tamakoro::exeBindStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SlideStomachRecover");
        MR::tryRumblePadWeak(this, PLAYER_1);
        mDirectionToMario.set(-mGravity);
        if (MR::normalizeOrZero(&mMarioFront)) {
            MR::makeAxisVerticalZX(&mMarioFront, mGravity);
        }
        MR::emitEffectHit(this, mMarioPos, mMarioPos - mPosition, "TamakoroLand");
    }

    // An arc from the bind request position to the top of the ball over sBindStartTime.
    f32 time = MR::calcNerveRate(this, ::sBindStartTime);
    Vec3f expectMario;
    expectMario.scaleAdd(mBaseRadius, mDirectionToMario, mPosition);
    Vec3f horizontalVec;
    MR::vecBlend(mMarioBindRequestPos, expectMario, &horizontalVec, time);
    time = (time * 2.0f) - 1.0f;
    const Vec3f upVec = mGravity * (-(time * time * -::sBindStartJumpHeight + mBaseRadius));

    MR::blendQuatUpFront(&mMarioRotateQuat, -mGravity, mMarioFront, ::sBindStartUpAdjustRate, ::sBindStartFrontAdjustRate);

    mMarioPos = horizontalVec + upVec;

    if (MR::isGreaterStep(this, ::sBindStartTime)) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvBindStartLand));
    }
}

void Tamakoro::exeBindStartLand() {
    if (MR::isFirstStep(this)) {
        if (!isNeedTutorial()) {
            if (MR::isEqualStageName("HellProminenceGalaxy")) {
                MR::startStageBGM("BGM_TAMAKORO_2", false);
            } else {
                MR::startStageBGM("BGM_TAMAKORO", false);
            }

            mBgmCtrl->init();
        }
        MR::startBckPlayer("Land", "BallLand");
        MR::tryRumblePadMiddle(this, PLAYER_1);

        mDirectionToMario.set(mMarioPos - mPosition);
        if (MR::normalizeOrZero(&mDirectionToMario)) {
            mDirectionToMario.set(-mGravity);
        }
        mMarioPos.scaleAdd(mBaseRadius, mDirectionToMario, mPosition);

        mMarioUp = (mDirectionToMario - mGravity) / 2.0f;
        if (MR::normalizeOrZero(&mMarioUp)) {
            mMarioUp.set(-mGravity);
        }

        MR::emitEffectHit(this, mMarioPos, mMarioPos - mPosition, "TamakoroLand");
        MR::startSoundPlayer("SE_PV_CATCH");
        MR::startSound(this, "SE_SM_RIDE_START");

        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }
    }

    MR::blendQuatUpFront(&mMarioRotateQuat, mMarioUp, mMarioFront, ::sBindStartUpAdjustRate, ::sBindStartFrontAdjustRate);

    if (MR::isGreaterStep(this, ::sBindStartLandTime)) {
        if (isNeedTutorial()) {
            mTutorial->requestStart();
            setNerve(GET_NERVE(Tamakoro, TamakoroNrvTutorial));
        } else {
            // A plain Tamakoro (not TamakoroWithTutorial) has no tutorial.
            if (mTutorial != nullptr) {
                mTutorial->requestEnd();
            }
            setNerve(GET_NERVE(Tamakoro, TamakoroNrvWait));
        }
    }
}

void Tamakoro::exeTutorial() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("タマコロ移動");
        MR::zeroVelocity(this);
        mAccelDir.zero();
        mAccelRate = 0.0f;
        mMoment.zero();
        mKickVel.zero();
    }

    mAccelRate = ::calcTiltMove(&mAccelDir, mGravity);

    mKickVel.scaleAdd(mAccelRate * ::sTutorialAccel, mAccelDir, mKickVel);

    Vec3f moment;
    MR::calcMomentRollBall(&moment, mKickVel, -mGravity, mBaseRadius);
    MR::vecBlend(mMoment, moment, &mMoment, 0.25f);
    mKickVel.mult(::sTutorialFric);

    updateMoveBckBlend(mMoment.length() * mBaseRadius);
    updateMarioPose(3.0f);
    updateMarioOffset();
}

void Tamakoro::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("タマコロ移動");
    }
    reactionCollision();
    addVelocityOperate();
    updateVelocityNormal(::sGroundFric);
    updateMoveBckBlend(mMoment.length() * mBaseRadius);
    updateMarioPose(3.0f);
    updateMarioOffset();

    if (!tryJump()) {
        tryFall();
    }
}

void Tamakoro::exeFall() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("Fall", "BallFall");
    }
    mMarioOffset += ::sFallOffsetSpeed;
    mMarioOffset = MR::clamp(mMarioOffset, 0.0f, ::sMarioJumpOffsetMax);

    reactionCollision();

    if (!tryLand()) {
        addVelocityOperate();
        updateVelocityNormal(::sGroundFric);
        updateMarioPose(3.0f);
    }
}

void Tamakoro::exeJump() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BallJump", "BallJump");
        MR::startSoundPlayer("SE_PV_JUMP_M");
        MR::startSound(this, "SE_SM_BALLOONSPH_JUMP");
        MR::addVelocityToGravity(this, (-::sJumpPowerV - mVelocity.dot(mGravity)));
        MR::addVelocityMoveToDirection(this, mAccelDir, mAccelRate * ::sJumpPowerH);
    }

    mMarioOffset += ::sJumpOffsetSpeed;
    mMarioOffset = MR::clamp(mMarioOffset, 0.0f, ::sMarioJumpOffsetMax);

    reactionCollision();

    if (tryBumpWall() || tryLand()) {
        return;
    }

    addVelocityOperate();
    updateVelocityNormal(::sGroundFric);
    updateMarioPose(3.0f);
}

void Tamakoro::exeBumpWall() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BallHit");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormalWeak();
    }

    reactionCollision();

    addVelocityOperate();
    updateVelocityNormal(::sGroundFric);
    updateMarioPose(3.0f);
    updateMarioOffset();

    if (!tryJump() && MR::isGreaterStep(this, 15)) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvWait));
    }
}

void Tamakoro::exeLand() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("Land");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormalWeak();
        MR::startSound(this, "SE_SM_IRONSPH_LAND");
    }

    reactionCollision();

    addVelocityOperate();
    updateVelocityNormal(::sLandFric);
    updateMarioPose(3.0f);
    updateMarioOffset();

    if (!tryJump() && MR::isGreaterStep(this, 15)) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvWait));
    }
}

void Tamakoro::exeJumpHole() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::offBind(this);
        MR::startBckPlayerJ("タマコロしゃがみ");
        mAccelDir.zero();
        mAccelRate = 0.0f;
        mMarioOffsetVelocity = 0.0f;
    }
    updateSquatBckBlend(mMoment.length() * mBaseRadius);
    updateMarioPose(3.0f);
    updateMarioOffset();
}

void Tamakoro::exeJumpHoleSetUp() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }
    updateSquatBckBlend(0.0f);
    updateMarioPose(3.0f);
    updateMarioOffset();
}

void Tamakoro::exeJumpHoleLaunch() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BallJump");
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraStrong();
        MR::emitEffect(this, "BigJumpBlur");
    }
    mControlDisabled = true;

    mMarioOffset += 3.0f;
    mMarioOffset = MR::clamp(mMarioOffset, 0.0f, ::sMarioJumpOffsetMax);

    updateMarioPose(3.0f);
}

void Tamakoro::endJumpHoleLaunch() {
    MR::onBind(this);
    MR::deleteEffect(this, "BigJumpBlur");
    MR::emitEffect(this, "BigJumpLand");
}

void Tamakoro::exeDashRail() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BallRoll");
        MR::startSoundPlayer("SE_PV_JUMP_JOY");
        MR::startSound(this, "SE_SM_IRONSPH_RAILDASH");
        MR::tryRumblePadStrong(this, PLAYER_1);
        mAccelDir.zero();
        mAccelRate = 0.0f;
    }
    mControlDisabled = true;
    updateMarioPose(10.0f);
    updateMarioOffset();
    MR::setBckRatePlayer(mVelocity.length() / ::sDashBallRollSpeed);
}

void Tamakoro::exeDashRailEnd() {
    if (MR::isFirstStep(this)) {
        mMarioOffsetVelocity = 0.0f;
    }

    if (MR::isLessStep(this, ::sDashRailTime)) {
        MR::setBckRatePlayer(mVelocity.length() / ::sDashBallRollSpeed);
    }

    if (MR::isStep(this, ::sDashRailTime)) {
        MR::startBckPlayer("Fall", "BallFall");
    }

    mMarioOffset += 3.0f;
    mMarioOffset = MR::clamp(mMarioOffset, 0.0f, ::sMarioJumpOffsetMax);

    if (tryBumpWall() || tryLand()) {
        return;
    }

    updateVelocityNormal(::sDashRailFric);
    updateMarioPose(10.0f);
}

void Tamakoro::exeRideRail() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayerJ("タマコロ移動");
        MR::tryRumblePadMiddle(this, PLAYER_1);
    }

    f32 speed = updateRideRail();
    updateMoveBckBlend(speed);
    if (speed > ::sRaidRailFastSpeed) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRailFastStart));
    }
}

void Tamakoro::exeRideRailFastStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BallRollStart");
        MR::setBckRatePlayer(::sRollBckRate);
    }
    updateRideRail();
    mControlDisabled = true;
    if (MR::isBckStoppedPlayer()) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRailFast));
    }
}

void Tamakoro::exeRideRailFast() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BallRoll");
        MR::setBckRatePlayer(::sRollBckRate);
    }
    mControlDisabled = true;
    f32 speed = updateRideRail();

    MR::setBckRatePlayer(mVelocity.length() / ::sDashBallRollSpeed);

    if (speed < ::sRaidRailFastSpeed) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRailFastEnd));
    }
}

void Tamakoro::exeRideRailFastEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("BallRollEnd");
        MR::setBckRatePlayer(::sRollBckRate);
    }
    updateRideRail();
    if (MR::isBckStoppedPlayer()) {
        setNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRail));
    }
}

void Tamakoro::exeBindEnd() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
        MR::shakeCameraStrong();
        mAccelDir.zero();
        mAccelRate = 0.0f;
        if (MR::isValidSwitchA(this)) {
            MR::offSwitchA(this);
        }
        MR::emitEffect(this, "Break");
        MR::startSound(this, "SE_SM_RIDE_END");
        MR::startSoundPlayer("SE_PV_JUMP_S");
        MR::stopStageBGM(10);
    }

    if ((MR::isGreaterStep(this, 12) && MR::isOnGroundPlayer()) || MR::isGreaterStep(this, ::sForceBindEndTime)) {
        MR::startLastStageBGM();
        MR::requestAppearPowerStar(this, mPosition);
        kill();
    }
}

void Tamakoro::addVelocityOperate() {
    mAccelRate = ::calcTiltMove(&mAccelDir, mGravity);

    mVelocity.scaleAdd(mAccelRate * (MR::isBindedGround(this) ? 0.4f : 0.2f), mAccelDir, mVelocity);
}

// Rolls along the velocity; returns the roll speed before the pose update.
f32 Tamakoro::updateRideRail() {
    mAccelDir.set(mVelocity);
    mAccelRate = MR::normalizeOrZero(&mAccelDir) ? 0.5f : 0.0f;

    const f32 speed = mMoment.length() * mBaseRadius;

    updateMarioPose(15.0f);
    updateMarioOffset();

    return speed;
}

void Tamakoro::updateVelocityNormal(f32 friction) {
    MR::reboundVelocityFromCollision(this, 0.0f, 0.0f, 1.0f);
    MR::addVelocityToGravity(this, ::sGravityAccel);
    MR::attenuateVelocity(this, MR::isBindedGround(this) ? friction : 0.995f);
}

void Tamakoro::updateMoment() {
    if (isGroundMomentBall()) {
        Vec3f moment;
        const Vec3f* up;
        // Read through up after the branch, so it lives for the whole block.
        Vec3f gravityUp;
        if (MR::isBindedGround(this)) {
            up = MR::getGroundNormal(this);
        } else {
            gravityUp = -mGravity;
            up = &gravityUp;
        }

        MR::calcMomentRollBall(&moment, mVelocity, *up, mBaseRadius);
        MR::vecBlend(mMoment, moment, &mMoment, 0.25f);
        startRotateLevelSound();
    } else {
        mMoment.mult(0.98f);
    }
}

void Tamakoro::updateAirTime() {
    if (MR::isBindedGround(this)) {
        mAirTime = 0;
    } else {
        mAirTime++;
    }
}

void Tamakoro::updateMarioPose(f32 degree) {
    Vec3f velH = MR::getVelocityHorizon(this);

    if (MR::isNearZero(velH)) {
        if (mAccelRate > 0.001f) {
            MR::turnDirectionDegree(this, &mMarioFront, mAccelDir, degree * mAccelRate);
        }
    } else {
        MR::turnDirectionDegree(this, &mMarioFront, velH, degree);
    }

    Vec3f zDir;
    Vec3f xDir;
    mMarioRotateQuat.getXDir(xDir);
    mMarioRotateQuat.getZDir(zDir);

    if (mAccelRate > 0.0f) {
        const f32 turn = MR::normalize(1.0f - mAccelDir.dot(zDir), 0.1f, 1.0f);
        const f32 sign = mAccelDir.dot(xDir) >= 0.0f ? 1.0f : -1.0f;
        mMarioRotateYAngle += (sign * turn) * ::sMarioUpperYAccel;
    }
    mMarioRotateYAngle *= ::sMarioUpperYFric;

    Vec3f up = -mGravity;
    mMarioUp.set(-mGravity);

    Vec3f rotAxis = mAccelDir.cross(mGravity);

    if (!MR::normalizeOrZero(&rotAxis)) {
        MR::rotateVecDegree(&up, rotAxis, mAccelRate * 35.0f);
        MR::rotateVecDegree(&mMarioUp, rotAxis, mAccelRate * 20.0f);
    }
    MR::vecBlendSphere(mDirectionToMario, up, &mDirectionToMario, ::sStandDirInterRate);

    if (MR::normalizeOrZero(&mDirectionToMario)) {
        mDirectionToMario.set(-mGravity);
    }
    MR::blendQuatUpFront(&mMarioRotateQuat, mMarioUp, mMarioFront, ::sMarioUpAdjustRate, ::sMarioFrontAdjustRate);

    MR::setPlayerUpperRotateY(PI_180 * mMarioRotateYAngle);
}

void Tamakoro::updateMarioOffset() {
    mMarioOffsetVelocity = (mMarioOffsetVelocity - ::sMarioOffsetGravityAccel) * ::sMarioOffsetFric;
    mMarioOffset += mMarioOffsetVelocity;
    mMarioOffset = MR::clamp(mMarioOffset, 0.0f, ::sMarioJumpOffsetMax);
}

void Tamakoro::updateMoveBckBlend(f32 pSpeed) {
    f32 idleBckWeight = 0.0f;
    f32 softWalkBckWeight = 0.0f;
    f32 walkBckWeight = 0.0f;
    f32 runBckWeight = 0.0f;
    f32 rate = 0.0f;
    f32 softWalkSpeed = ::sSoftWalkSpeed;
    f32 walkSpeed = ::sWalkSpeed;
    f32 runSpeed = ::sRunSpeed;

    if (pSpeed < softWalkSpeed) {
        softWalkBckWeight = MR::normalize(pSpeed, 0.0f, softWalkSpeed);
        idleBckWeight = 1.0f - softWalkBckWeight;
        rate = MR::getLinerValue(softWalkBckWeight, ::sIdleBckRate, ::sSoftWalkBckRate, 1.0f);
    } else if (pSpeed < walkSpeed) {
        walkBckWeight = MR::normalize(pSpeed, softWalkSpeed, walkSpeed);
        softWalkBckWeight = 1.0f - walkBckWeight;
        rate = MR::getLinerValue(walkBckWeight, ::sSoftWalkBckRate, ::sWalkBckRate, 1.0f);
    } else if (pSpeed < runSpeed) {
        runBckWeight = MR::normalize(pSpeed, walkSpeed, runSpeed);
        walkBckWeight = 1.0f - runBckWeight;
        rate = MR::getLinerValue(runBckWeight, ::sWalkBckRate, ::sRunBckRate, 1.0f);
    } else {
        runBckWeight = 1.0f;
        rate = (pSpeed / runSpeed) * ::sRunBckRate;
        if (rate > ::sRunMaxBckRate) {
            rate = ::sRunMaxBckRate;
        }
    }

    MR::setBckBlendWeight(idleBckWeight, softWalkBckWeight, walkBckWeight, runBckWeight);
    MR::setBckRatePlayer(rate);
}

void Tamakoro::updateSquatBckBlend(f32 pSpeed) {
    // Squat to squat-move over 0..6.
    const f32 moveWeight = MR::normalize(pSpeed, 0.0f, 6.0f);
    const f32 rate = MR::getLinerValue(moveWeight, 1.0f, 2.0f, 1.0f);

    MR::setBckBlendWeight(1.0f - moveWeight, moveWeight);
    MR::setBckRatePlayer(rate);
}

bool Tamakoro::isEnablePushPlayer() const {
    return isNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByTutorial)) || isNerve(GET_NERVE(Tamakoro, TamakoroNrvStandByBind));
}

bool Tamakoro::isEnableEnemyAttack() const {
    return isNerve(GET_NERVE(Tamakoro, TamakoroNrvWait)) || isNerve(GET_NERVE(Tamakoro, TamakoroNrvFall)) ||
           isNerve(GET_NERVE(Tamakoro, TamakoroNrvJump)) || isNerve(GET_NERVE(Tamakoro, TamakoroNrvBumpWall)) ||
           isNerve(GET_NERVE(Tamakoro, TamakoroNrvDashRailEnd)) || isNerve(GET_NERVE(Tamakoro, TamakoroNrvLand));
}

// The same nerves as isEnableEnemyAttack.
bool Tamakoro::isEnableBallBind() const {
    return isEnableEnemyAttack();
}

bool Tamakoro::isEnebleHitCollisionMessage() const {
    return isNerve(GET_NERVE(Tamakoro, TamakoroNrvJump));
}

bool Tamakoro::isUseMarioOffset() const {
    return !isNerve(GET_NERVE(Tamakoro, TamakoroNrvBindStart)) && !isNerve(GET_NERVE(Tamakoro, TamakoroNrvBindStartLand));
}

bool Tamakoro::isGroundMomentBall() const {
    return MR::isBindedGround(this) || isRideRail() || isNerve(GET_NERVE(Tamakoro, TamakoroNrvDashRail)) ||
           isNerve(GET_NERVE(Tamakoro, TamakoroNrvJumpHole));
}

bool Tamakoro::isRideRail() const {
    return isNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRail)) || isNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRailFastStart)) ||
           isNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRailFast)) || isNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRailFastEnd));
}

bool Tamakoro::isNeedTutorial() const {
    if (mTutorial == nullptr) {
        return false;
    }
    return mTutorial->isNeedTutorial();
}

void Tamakoro::startRotateLevelSound() {
    f32 mag = mMoment.length();

    if (mag <= 0.001f) {
        return;
    }

    s32 lvl = ((mag * 10000.0f) / TWO_PI);
    if (isNerve(GET_NERVE(Tamakoro, TamakoroNrvRideRail))) {
        MR::startLevelSound(this, "SE_SM_LV_IRONSPH_ROLL_RAIL", lvl);
    } else {
        MR::startLevelSound(this, "SE_SM_LV_IRONSPH_ROLL1", lvl);
    }
}

namespace MR {
    NameObj* createTamakoroWithTutorial(const char* pName) {
        Tamakoro* tamakoro = new Tamakoro(pName);
        tamakoro->setTutorial();
        return tamakoro;
    }
};  // namespace MR
