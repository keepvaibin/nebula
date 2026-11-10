#include "Game/MapObj/FallDownBridge.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 2600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 9.090277f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f4, "control.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f5, "control.f5", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f6, "control.f6", 9.090277f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f7, "control.f7", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f8, "control.f8", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f9, "control.f9", 0.99f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f1, "receiveOtherMsg.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f2, "receiveOtherMsg.f2", 0.45f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_f3, "receiveOtherMsg.f3", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAndSetBaseMtx_f1, "calcAndSetBaseMtx.f1", 400.0f);

namespace {
    const Vec3f sBaseClipCentor = Vec3f(0.0f, 1250.0f, 0.0f);
    // const f32 sBlockDepth = 0.0f;
    // const f32 sFallAccel = 0.0f;
    // const f32 sFallDegreeFreq = 0.0f;
    // const f32 sFixFallDegree = 0.0f;
    // const f32 sFixDegreeVelocity = 0.0f;
    // const f32 sReboundRate = 0.0f;
    // const f32 sNormalPushPower = 0.0f;
    // const f32 sDashPushPower = 0.0f;
    // const f32 sTestPushPower = 0.0f;
    NEBULA_TUNABLE(s32, sQuakeTime, 80);
    NEBULA_TUNABLE(f32, sShakeCameraNormalVelocity, 1.0f);
    NEBULA_TUNABLE(f32, sShakeCameraWeakVelocity, 0.5f);
    // const f32 sLimitFallDownDgree = 0.0f;
};  // namespace

namespace NrvFallDownBridge {
    NEW_NERVE(FallDownBridgeNrvWait, FallDownBridge, Wait);
    NEW_NERVE(FallDownBridgeNrvQuake, FallDownBridge, Quake);
};  // namespace NrvFallDownBridge

FallDownBridge::FallDownBridge(const char* pName) : LiveActor(pName), mClipCenter(0.0f, 1250.0f, 0.0f), mAngle(), mAngleSpeed(), mMinAngle(0.0f), mMaxAngle(90.0f), mIsFallen() {
}

void FallDownBridge::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    const char* pName = nullptr;
    MR::getObjectName(&pName, rIter);
    initModelManagerWithAnm(pName, nullptr, false);

    MR::connectToSceneMapObj(this);

    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));

    MR::initCollisionParts(this, pName, getSensor("body"), nullptr);
    initSound(4, false);

    MR::setClippingTypeSphere(this, cNum_init_f1, &mClipCenter);

    initNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvWait));

    appear();
}

void FallDownBridge::control() {
    // Tips away from its balance angle (about 9.09 degrees either side), faster the further it leans.
    f32 tip = MR::sin(MR::toRadian(MR::abs(mAngle) - cNum_control_f1));

    if (mAngle < 0.0f) {
        tip = -tip;
    }

    mAngleSpeed += tip * cNum_control_f2;
    mAngle += mAngleSpeed;

    if (!mIsFallen && MR::abs(mAngleSpeed) >= cNum_control_f3) {
        MR::startLevelSound(this, "SE_OJ_LV_CHAL_FD_BRIDGE_FL");
    }

    bool isAtStop = false;

    if (mAngle > mMaxAngle) {
        mAngle = mMaxAngle;
        isAtStop = true;
    }

    if (mAngle < mMinAngle) {
        mAngle = mMinAngle;
        isAtStop = true;
    }

    if (isAtStop) {
        if (MR::abs(mAngleSpeed) > ::sShakeCameraNormalVelocity) {
            MR::startSound(this, "SE_OJ_CHAL_FD_BRIDGE_END");
            MR::shakeCameraNormal();
        } else {
            if (MR::abs(mAngleSpeed) > ::sShakeCameraWeakVelocity) {
                MR::shakeCameraWeak();
            }
        }

        mAngleSpeed *= -cNum_control_f4;

        if (MR::abs(mAngleSpeed) < cNum_control_f5) {
            mAngleSpeed = 0.0f;

            if (MR::abs(mAngle) > cNum_control_f6) {
                mIsFallen = true;
            }
        }
    }

    if (MR::abs(mAngle) < cNum_control_f7 && MR::abs(mAngleSpeed) < cNum_control_f8) {
        mAngle = 0.0f;
        mAngleSpeed = 0.0f;
    }

    mAngleSpeed *= cNum_control_f9;
}

bool FallDownBridge::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (!mIsFallen && isNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvWait)) && (msg == ACTMES_BALL_DASH_WALL || msg == ACTMES_BALL_TOUCH_WALL)) {
        LiveActor* pHost = pSender->mHost;

        if (!MR::isBindedWall(pHost)) {
            return false;
        }

        Vec3f wallNormal(*MR::getWallNormal(pHost));
        MR::normalizeOrZero(&wallNormal);

        Pos3f base;
        base.set(getBaseMtx());

        Vec3f side;
        base.getXDir(side);

        f32 dot = side.dot(wallNormal);

        if (MR::abs(dot) < cNum_receiveOtherMsg_f1) {
            return false;
        }

        f32 push;

        if (msg == ACTMES_BALL_DASH_WALL) {
            push = cNum_receiveOtherMsg_f2;
            MR::startSound(this, "SE_OJ_CHAL_FD_BRIDGE_HIT");
        } else {
            push = cNum_receiveOtherMsg_f3;
        }

        if (dot < 0.0f) {
            push = -push;
        }

        mAngleSpeed += push;

        setNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvQuake));

        return true;
    }

    return false;
}

void FallDownBridge::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();

    Pos3f base;
    base.set(getBaseMtx());

    Pos3f toPivot;
    toPivot.identity();

    Vec3f pivot;

    if (mAngle >= 0.0f) {
        pivot.set< f32 >(0.0f, 0.0f, 0.0f);
    } else {
        pivot.set(mScale.x * cNum_calcAndSetBaseMtx_f1, 0.0f, 0.0f);
    }

    toPivot.setTrans(-pivot);

    Pos3f mtx;
    mtx.identity();
    mtx.makeRotate(Vec3f(0, 0, 1), MR::toRadian(mAngle));

    mtx.concat(mtx, toPivot);

    MR::addTransMtx(mtx, pivot);

    mtx.concat(base, mtx);

    MR::setBaseTRMtx(this, mtx);

    mtx.mult(::sBaseClipCentor, mClipCenter);
}

void FallDownBridge::exeWait() {
}

void FallDownBridge::exeQuake() {
    if (getNerveStep() > ::sQuakeTime) {
        setNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvWait));
    }
}
