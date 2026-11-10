#include "Game/Enemy/Onimasu.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcTargetPose_f1, "calcTargetPose.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcTargetPose_f2, "calcTargetPose.f2", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcTurnDirection_f1, "calcTurnDirection.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcTurnDirection_f2, "calcTurnDirection.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateStompVelocity_f1, "updateStompVelocity.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_emitEffectLand_f1, "emitEffectLand.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_getPolygonOnRailPoint_f1, "getPolygonOnRailPoint.f1", 2000.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeMove_1, "exeMove.1", 1);

namespace {
    static const s32 sWaitTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    static const s32 sTimeToNextPoint = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sGravityScalar, 1.6f);
    NEBULA_TUNABLE(s32, sWaitTimeQuick, 45);
    NEBULA_TUNABLE(s32, sTimeToNextPointQuick, 15);
    NEBULA_TUNABLE(f32, sGravityScalarQuick, 6.4f);
    NEBULA_TUNABLE(f32, sCamShakeDistanceStrong, 2000.0f);
    NEBULA_TUNABLE(f32, sCamShakeDistanceWeak, 3500.0f);
    NEBULA_TUNABLE(f32, sSensorRadius, 800.0f);
};  // namespace

namespace NrvOnimasu {
    NEW_NERVE(HostTypeWaitForSwitchOn, Onimasu, WaitForSwitchOn);
    NEW_NERVE(HostTypeWait, Onimasu, Wait);
    NEW_NERVE(HostTypeWaitForStamp, Onimasu, Wait);
    NEW_NERVE(HostTypeJump, Onimasu, Move);
    NEW_NERVE(HostTypeStamp, Onimasu, Stamp);
};  // namespace NrvOnimasu

Onimasu::Onimasu(const char* pName) : LiveActor(pName), mRotateAxis(1.0f, 0.0f, 0.0f), mClipCenter(gZeroVec) {
    lumacore::setClass(this, lumacore::kClassId< Onimasu >);
    mMoveEffectMtx.identity();
    mStartQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
    mTargetQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
    mPoseQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void Onimasu::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initRailRider(rIter);
    initFromRailPoint();
    initModelManagerWithAnm("Onimasu", nullptr, false);

    MR::connectToSceneCollisionEnemyNoShadowedMapObjStrongLight(this);

    MR::initLightCtrl(this);
    initEffectKeeper(0, nullptr);

    MR::setEffectHostMtx(this, "Move", mMoveEffectMtx);

    initSound(4, false);

    initHitSensor(1);
    MR::initCollisionParts(this, "Onimasu", MR::addHitSensor(this, "body", ATYPE_MAP_OBJ_PRESS, 8, ::sSensorRadius, Vec3f(0.0f, 0.0f, 0.0f)),
                           nullptr);

    MR::initAndSetRailClipping(&mClipCenter, this, cNum_init_f1, cNum_init_f2);

    MR::setGroupClipping(this, rIter, 16);

    MR::addBaseMatrixFollowTarget(this, rIter, nullptr, nullptr);

    MR::onCalcGravity(this);

    if (MR::useStageSwitchReadA(this, rIter)) {
        initNerve(GET_NERVE(Onimasu, HostTypeWaitForSwitchOn));
    } else {
        initNerve(GET_NERVE(Onimasu, HostTypeWait));
    }

    MR::useStageSwitchSleep(this, rIter);

    makeActorAppeared();
}

void Onimasu::initAfterPlacement() {
    collectRailPointInfo();
    MR::moveCoordAndTransToRailStartPoint(this);
    MR::setRailDirectionToEnd(this);

    Pos3f mtx;
    mtx.identity();
    MR::makeMtxTR(mtx.toMtxPtr(), this);
    MR::setBaseTRMtx(this, mtx);

    if (MR::isEqualStageName("FactoryGalaxy")) {
        mtx.getXDir(mRotateAxis);
        MR::normalize(&mRotateAxis);
    }

    mtx.getQuat(mStartQuat);
    mTargetQuat.set(mStartQuat);
    mPoseQuat.set(mTargetQuat);

    if (MR::isEqualStageName("FactoryGalaxy")) {
        f32 turnDirection = 0.0f;
        if (calcTurnDirection(&turnDirection) && turnDirection == -1.0f) {
            setNerve(GET_NERVE(Onimasu, HostTypeWaitForStamp));
        }
    }
}

void Onimasu::calcTargetPose() {
    Quat4f rotationChange;
    rotationChange.set(0.0f, 0.0f, 0.0f, 1.0f);

    if (MR::isSameDirection(getLastPointNormal(), getNextPointNormal())) {
        f32 turnDirection = 0.0f;
        if (calcTurnDirection(&turnDirection)) {
            Pos3f mtx;
            mtx.identity();
            const f32 pi = MR::pi();
            mtx.makeRotate(getLastPointNormal(), pi * turnDirection);
            mtx.mult(mRotateAxis, mRotateAxis);

            MR::normalize(&mRotateAxis);
        } else {
            Vec3f lastPointPos;
            MR::calcRailPointPos(&lastPointPos, this, getLastPointNo());

            Vec3f nextPointPos;
            MR::calcRailPointPos(&nextPointPos, this, getNextPointNo());

            Vec3f railDir(nextPointPos - lastPointPos);
            MR::normalize(&railDir);

            Vec3f down;
            calcGravityDir(&down);
            mRotateAxis.cross(railDir, down);
            MR::normalize(&mRotateAxis);
        }
    } else {
        Quat4f normalRotation;
        normalRotation.set(0.0f, 0.0f, 0.0f, 1.0f);
        normalRotation.setRotate(getLastPointNormal(), getNextPointNormal());
        rotationChange.set(normalRotation);
        normalRotation.getRotate(mRotateAxis);
        MR::normalize(&mRotateAxis);
    }

    mStartQuat.set(mTargetQuat);

    Quat4f quarterTurn;
    quarterTurn.set(0.0f, 0.0f, 0.0f, 1.0f);
    quarterTurn.setRotate(mRotateAxis, MR::pi() / cNum_calcTargetPose_f1);
    rotationChange.mult(quarterTurn);
    mTargetQuat.mult(rotationChange);

    Vec3f startFront;
    mStartQuat.getZDir(startFront);

    Vec3f targetFront;
    mTargetQuat.getZDir(targetFront);

    if (MR::isSameDirection(startFront, targetFront)) {
        Quat4f adjustment;
        adjustment.setRotate(mRotateAxis, MR::pi() * cNum_calcTargetPose_f2);
        mStartQuat.mult(adjustment);
    }
}

void Onimasu::updatePose() {
    f32 nerveRate = static_cast< f32 >(getNerveStep()) / getTimeToNextPoint();
    if (1.0f < nerveRate) {
        nerveRate = 1.0f;
    }

    mPoseQuat.set(mStartQuat);
    mPoseQuat.slerp(mTargetQuat, nerveRate);
    mPoseQuat.normalize();
}

void Onimasu::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    baseMtx.identity();
    baseMtx.setQT(mPoseQuat, mPosition);

    MR::setBaseTRMtx(this, baseMtx);
}

void Onimasu::land() {
    emitEffectLand();
    MR::startRumbleWithShakeCameraNormalWeak(this, "中", "弱", ::sCamShakeDistanceStrong, ::sCamShakeDistanceWeak);

    Vec3f railPointPos;
    MR::calcRailPointPos(&railPointPos, this, getNextPointNo());

    mPosition.set(railPointPos);
    mVelocity.zero();

    MR::moveCoordToRailPoint(this, getNextPointNo());
}

bool Onimasu::calcTurnDirection(f32* pTurnDir) const {
    s32 turnType = -1;
    MR::getCurrentRailPointArg0NoInit(this, &turnType);

    if (turnType == -1) {
        return false;
    }

    if (turnType == 0) {
        *pTurnDir = cNum_calcTurnDirection_f1;
        return true;
    }

    if (turnType == 1) {
        *pTurnDir = -cNum_calcTurnDirection_f2;
        return true;
    }

    if (turnType == 2) {
        *pTurnDir = -1.0f;
        return true;
    }

    return false;
}

void Onimasu::updateStompVelocity() {
    f32 speed = getGravityScalar() * getTimeToNextPoint() * cNum_updateStompVelocity_f1;

    Vec3f gravityDir;
    gravityDir.set(-getNextPointNormal());
    mVelocity.set(-gravityDir * speed);
}

void Onimasu::calcGravityDir(Vec3f* pDir) const {
    pDir->set(-getNextPointNormal());
}

f32 Onimasu::getGravityScalar() const {
    return MR::isGalaxyQuickCometAppearInCurrentStage() ? ::sGravityScalarQuick : ::sGravityScalar;
}

s32 Onimasu::getTimeToNextPoint() const {
    bool quick = MR::isGalaxyQuickCometAppearInCurrentStage();
    return quick ? ::sTimeToNextPointQuick : ::sTimeToNextPoint;
}

s32 Onimasu::getWaitTime() const {
    bool quick = MR::isGalaxyQuickCometAppearInCurrentStage();
    return quick ? ::sWaitTimeQuick : ::sWaitTime;
}

void Onimasu::emitEffectLand() {
    Vec3f railPointPos;
    MR::calcRailPointPos(&railPointPos, this, getNextPointNo());

    railPointPos.add((-getNextPointNormal() * ::sSensorRadius) * cNum_emitEffectLand_f1);

    if (MR::isSameDirection(mRotateAxis, getNextPointNormal())) {
        MR::makeMtxUpNoSupportPos(&mMoveEffectMtx, getNextPointNormal(), railPointPos);
    } else {
        MR::makeMtxUpSidePos(&mMoveEffectMtx, getNextPointNormal(), mRotateAxis, railPointPos);
    }

    MR::emitEffect(this, "Move");
    MR::startSound(this, "SE_OJ_ONIMASU_LAND");
}

void Onimasu::exeWaitForSwitchOn() {
    if (MR::isOnSwitchA(this)) {
        f32 turnDirection = 0.0f;
        if (calcTurnDirection(&turnDirection) && turnDirection == -1.0f) {
            setNerve(GET_NERVE(Onimasu, HostTypeWaitForStamp));
        } else {
            setNerve(GET_NERVE(Onimasu, HostTypeWait));
        }
    }
}

void Onimasu::exeWait() {
    if (MR::isStep(this, getWaitTime())) {
        if (isNerve(GET_NERVE(Onimasu, HostTypeWaitForStamp))) {
            setNerve(GET_NERVE(Onimasu, HostTypeStamp));
        } else {
            setNerve(GET_NERVE(Onimasu, HostTypeJump));
        }
    }
}

void Onimasu::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ONIMASU_JUMP");

        incrementNextPoint();
        calcTargetPose();
        startMoveInner();
    }

    if (MR::isStep(this, cStep_exeMove_1)) {
        MR::offCalcGravity(this);
    }

    updatePose();
    updatePoseInner();

    if (MR::isStep(this, getTimeToNextPoint())) {
        land();

        f32 turnDirection = 0.0f;
        if (calcTurnDirection(&turnDirection) && turnDirection == -1.0f) {
            setNerve(GET_NERVE(Onimasu, HostTypeWaitForStamp));
        } else {
            setNerve(GET_NERVE(Onimasu, HostTypeWait));
        }
    }
}

void Onimasu::exeStamp() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ONIMASU_JUMP");
        updateStompVelocity();
    }

    Vec3f gravityDir;
    calcGravityDir(&gravityDir);

    Vec3f& rVelocity = mVelocity;
    rVelocity.add(gravityDir * getGravityScalar());

    if (MR::isStep(this, getTimeToNextPoint())) {
        land();
        setNerve(GET_NERVE(Onimasu, HostTypeWait));
    }
}

namespace OnimasuFunction {
    bool getPolygonOnRailPoint(Vec3f* pPosition, Vec3f* pNormal, const LiveActor* pActor, int pointNo) {
        Vec3f railPosition;
        MR::calcRailPointPos(&railPosition, pActor, pointNo);

        Vec3f gravity(0.0f, 1.0f, 0.0f);
        MR::calcGravityVector(pActor, railPosition, &gravity, nullptr, 0);
        Vec3f direction(gravity);
        gravity.setLength(cNum_getPolygonOnRailPoint_f1);

        Triangle triangle;
        if (!MR::getFirstPolyOnLineToMapExceptSensor(pPosition, &triangle, railPosition, gravity, pActor->getSensor("body"))) {
            return false;
        }

        pNormal->set(*triangle.getFaceNormal());
        return true;
    }
};  // namespace OnimasuFunction
