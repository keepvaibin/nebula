#include "Game/MapObj/ShootingStar.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sFallHeight = 2000.0f;
    const f32 sFallRotateYAngle = 10.0f;
    const f32 sFallVelScale = 25.0f;
    const s32 sShootingLimitTime = NebulaFrameClock::stepsOfSeconds(6);
    const s32 sPreShootingTime = 20;
    const s32 sWaitForNextShootTime = NebulaFrameClock::stepsOfSeconds(4);
    const f32 sLaunchYVel = 40.0f;
    const f32 sLaunchHVel = 15.0f;
    const s32 sDefaultStarPieceNum = 5;
};  // namespace

namespace NrvShootingStar {
    NEW_NERVE(HostTypeNrvPreShooting, ShootingStar, PreShooting);
    NEW_NERVE(HostTypeNrvShooting, ShootingStar, Shooting);
    NEW_NERVE(HostTypeNrvWaitForNextShoot, ShootingStar, WaitForNextShoot);
};  // namespace NrvShootingStar

ShootingStar::ShootingStar(const char* pName)
    : LiveActor(pName), mBasePos(0, 0, 0), mFallAxis(0, 0, 1), mGravityPreShoot(0, -1, 0), mNumBurstStarPiece(sDefaultStarPieceNum),
      mWaitForNextShootTime(sWaitForNextShootTime), mFallHeight(sFallHeight) {
    lumacore::setClass(this, lumacore::kClassId< ShootingStar >);
}

// Falls along its local -Y from its fall height onto the placed point and bursts into star pieces, again after a wait
// until its declared pieces run out. Args: 0 pieces per burst, 1 the wait, 2 the fall height, 3 the pieces in all.
void ShootingStar::init(const TableIter& rIter) {
    initModelManagerWithAnm("ShootingStar", nullptr, false);
    MR::connectToSceneMapObj(this);
    MR::initDefaultPos(this, rIter);
    mBasePos = mPosition;

    s32 burstNum = sDefaultStarPieceNum;
    MR::getPlacementArg0NoInit(rIter, &burstNum);
    mNumBurstStarPiece = burstNum;

    s32 waitTime = sWaitForNextShootTime;
    MR::getPlacementArg1NoInit(rIter, &waitTime);
    mWaitForNextShootTime = waitTime;

    f32 fallHeight = -1.0f;
    MR::getPlacementArg2NoInit(rIter, &fallHeight);
    if (fallHeight != -1.0f) {
        mFallHeight = fallHeight;
    }

    s32 totalNum = sDefaultStarPieceNum;
    MR::getPlacementArg3NoInit(rIter, &totalNum);
    MR::declareStarPiece(this, totalNum);

    initBinder(100.0f, 0.0f, 0);
    MR::setBindTriangleFilter(this, MR::createTriangleFilterFunc(MR::isWallCodeNoAction));
    initNerve(GET_NERVE(ShootingStar, HostTypeNrvPreShooting));
    initEffectKeeper(0, "ShootingStar");
    initSound(4, false);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "message", 1, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initShadowVolumeSphere(this, 30.0f);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::listenStageSwitchOnOffAppear(this, MR::Functor(this, &ShootingStar::appearPreShooting), MR::Functor< LiveActor >(this, &LiveActor::kill));
        makeActorDead();
    } else {
        makeActorAppeared();
    }

    LiveActor::calcAndSetBaseMtx();
    Pos3f matrix = getBaseMtx();
    matrix.getYDir(mFallAxis);
    MR::startBpk(this, "ShootingStar");
}

void ShootingStar::appearPreShooting() {
    appear();
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(ShootingStar, HostTypeNrvPreShooting));
}

void ShootingStar::control() {
    mRotation.y = MR::repeatDegree(mRotation.y + sFallRotateYAngle);
}

// Appears at the top of its fall, growing.
void ShootingStar::exePreShooting() {
    if (MR::isFirstStep(this)) {
        MR::calcGravityVector(this, &mGravityPreShoot, nullptr, 0);
        MR::showModel(this);
        MR::emitEffect(this, "ShootingStarAppear");
        mPosition.set(mBasePos);
        mPosition.add(mFallAxis * mFallHeight);
        mVelocity.zero();

        if (!MR::isValidSwitchAppear(this)) {
            MR::invalidateClipping(this);
        }
    }

    mScale.set(static_cast< f32 >(getNerveStep()) / sPreShootingTime);

    if (MR::isGreaterStep(this, sPreShootingTime)) {
        setNerve(GET_NERVE(ShootingStar, HostTypeNrvShooting));
    }
}

// Falls until it hits something (bursting), or starts over after the time limit.
void ShootingStar::exeShooting() {
    if (MR::isFirstStep(this)) {
        mVelocity = -mFallAxis * sFallVelScale;
        MR::emitEffect(this, "ShootingStarBlur");
        mScale.set(1.0f, 1.0f, 1.0f);
    }

    MR::startLevelSound(this, "SE_OJ_LV_STAR_PIECE_FALL");

    if (MR::isGreaterStep(this, sShootingLimitTime)) {
        setNerve(GET_NERVE(ShootingStar, HostTypeNrvPreShooting));
        MR::deleteEffect(this, "ShootingStarBlur");
        return;
    }

    if (!MR::isBinded(this)) {
        return;
    }

    setNerve(GET_NERVE(ShootingStar, HostTypeNrvWaitForNextShoot));

    if (MR::appearStarPiece(this, mPosition, mNumBurstStarPiece, sLaunchHVel, sLaunchYVel, false)) {
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
    } else {
        MR::startSound(this, "SE_OJ_STAR_PIECE_NOT_BURST");
    }

    MR::deleteEffect(this, "ShootingStarBlur");
}

void ShootingStar::exeWaitForNextShoot() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::emitEffect(this, "ShootingStarBreak");
        mVelocity.zero();

        if (!MR::isValidSwitchAppear(this)) {
            MR::validateClipping(this);
        }

        MR::offCalcAnim(this);
        MR::invalidateShadow(this, nullptr);
    }

    if (MR::getDeclareRemnantStarPieceCount(this) == 0) {
        kill();
    }

    if (MR::isGreaterStep(this, mWaitForNextShootTime)) {
        MR::onCalcAnim(this);
        MR::validateShadow(this, nullptr);
        setNerve(GET_NERVE(ShootingStar, HostTypeNrvPreShooting));
    }
}
