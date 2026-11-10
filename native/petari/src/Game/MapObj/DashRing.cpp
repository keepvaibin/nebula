#include "Game/MapObj/DashRing.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1000.0f);

namespace {
    // Passing: within this far of the ring's plane and this far of its centre.
    NEBULA_TUNABLE(f32, cPassPlaneDistance, 20.0f);
    NEBULA_TUNABLE(f32, cPassRadius, 200.0f);
    // Farther than this, the ring turns a little to face the player.
    NEBULA_TUNABLE(f32, cTurnDistance, 300.0f);
    NEBULA_TUNABLE(f32, cTurnRate, 0.03f);
    // After a pass it follows the player this long, then rests before coming back.
    NEBULA_TUNABLE(u16, cActiveSteps, 15);
    const u16 cRevivalSteps = NebulaFrameClock::stepsOfSeconds(3);
    // Following the player farther than this from home sends it back.
    NEBULA_TUNABLE(f32, cFollowLimit, 2000.0f);
    // Defaults for args 0-2 (arg 2 in hundredths).
    const s32 cDefaultBoostTime = NebulaFrameClock::stepsOfSeconds(5);
    const s32 cDefaultMaxDuration = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, cDefaultSpeedScale, 2.5f);
    NEBULA_TUNABLE(f32, cSpeedScaleArgUnit, 100.0f);

    void startAnim(LiveActor* pActor, const char* pName) {
        MR::startBck(pActor, pName);
        MR::startBrk(pActor, pName);
    }
};  // namespace

// A ring the swimming player passes through for a speed boost: arg 0 the boost steps, arg 1 the longest dash, arg 2
// the speed scale. It turns toward the player unless arg 3 is set. The player reads the three from the sensor's host.
void DashRing::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    mActiveTime = 0;
    mRevivalTime = 0;

    initModelManagerWithAnm("DashRing", nullptr, false);
    MR::connectToSceneMapObj(this);

    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::validateClipping(this);
    calcAxis();

    mBoostTime = cDefaultBoostTime;
    mMaxDuration = cDefaultMaxDuration;
    mSpeedScale = cDefaultSpeedScale;
    mDoTurn = true;

    if (MR::checkTableEntries(rIter)) {
        s32 boostTime = -1;
        s32 maxDuration = -1;
        s32 speedScale = -1;

        MR::getPlacementArg0NoInit(rIter, &boostTime);
        MR::getPlacementArg1NoInit(rIter, &maxDuration);

        if (boostTime != -1) {
            mBoostTime = boostTime;
        }

        if (maxDuration != -1) {
            mMaxDuration = maxDuration;
        }

        if (MR::getPlacementArg2NoInit(rIter, &speedScale) && speedScale != -1) {
            mSpeedScale = speedScale / cSpeedScaleArgUnit;
        }

        s32 noTurn = -1;
        MR::getPlacementArg3NoInit(rIter, &noTurn);

        if (noTurn != -1) {
            mDoTurn = false;
        }
    }

    initSound(3, false);
    initEffectKeeper(3, nullptr);

    appear();
    startAnim(this, "Loop");

    mInitialPosition = mPosition;
}

void DashRing::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (mRevivalTime != 0 || !MR::isSensorPlayer(pReceiver)) {
        return;
    }

    Vec3f dir(MR::getSensorPos(pReceiver) - mPosition);
    Vec3f radial;
    f32 along = MR::vecKillElement(dir, mAxis, &radial);

    if (MR::abs(along) < cPassPlaneDistance && radial.length() < cPassRadius) {
        MR::sendArbitraryMsg(ACTMES_PASS_RING, pReceiver, getSensor("body"));

        if (mActiveTime != 0) {
            return;
        }

        mActiveTime = cActiveSteps;
        MR::invalidateClipping(this);
        MR::startSystemSE("SE_SY_GET_DASH_RING");
        startAnim(this, "Start");
        return;
    }

    if (mActiveTime != 0 || !mDoTurn) {
        return;
    }

    if (!(MR::abs(along) > cTurnDistance || radial.length() > cTurnDistance)) {
        return;
    }

    if (MR::normalizeOrZero(&dir)) {
        return;
    }

    // Toward whichever side of the ring faces the player.
    f32 diffFront = MR::diffAngleAbs(mAxis, dir);

    if (diffFront > MR::diffAngleAbs(mAxis, -dir)) {
        MR::vecBlendSphere(mAxis, -dir, &mAxis, cTurnRate);
    } else {
        MR::vecBlendSphere(mAxis, dir, &mAxis, cTurnRate);
    }

    calcSubAxis();
}

// Passed, it rides just ahead of the player, then hides and comes back home.
void DashRing::control() {
    if (mActiveTime != 0) {
        mActiveTime--;

        if (mActiveTime == 0) {
            mRevivalTime = cRevivalSteps;
            MR::invalidateHitSensors(this);
            startAnim(this, "End");
            return;
        }

        mPosition = *MR::getPlayerPos() + *MR::getPlayerVelocity();

        if ((mPosition - mInitialPosition).length() > cFollowLimit) {
            mPosition = mInitialPosition;
            mActiveTime = 0;
        }

        return;
    }

    if (mRevivalTime != 0) {
        mRevivalTime--;

        if (mRevivalTime != 0) {
            return;
        }

        mPosition = mInitialPosition;
        startAnim(this, "Revival");
        MR::validateHitSensors(this);
        return;
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        startAnim(this, "Loop");
        MR::validateClipping(this);
    }
}

void DashRing::calcAndSetBaseMtx() {
    Pos3f pos;
    MR::makeMtxFrontSidePos(&pos, mAxis, mSubAxis, mPosition);
    NebulaMtxCopy(pos, getBaseMtx());
}

// The ring faces along its local Y.
void DashRing::calcAxis() {
    Vec3f axis(0.0f, 1.0f, 0.0f);

    Mtx mtxTR;
    MR::makeMtxTR(mtxTR, 0.0f, 0.0f, 0.0f, mRotation.x, mRotation.y, mRotation.z);

    NebulaMtxMultVec(mtxTR, &axis, &axis);
    mAxis = axis;

    calcSubAxis();
}

// A unit vector across the axis: in the XY plane unless the axis lies flat.
void DashRing::calcSubAxis() {
    Vec3f axis(mAxis);
    Vec3f subAxis;

    if (!MR::isNearZero(axis.y)) {
        subAxis.x = axis.y;
        subAxis.y = -axis.x;
        subAxis.z = 0.0f;
    } else {
        subAxis.x = axis.z;
        subAxis.y = 0.0f;
        subAxis.z = -axis.x;
    }

    MR::normalizeOrZero(&subAxis);
    mSubAxis = subAxis;
}
