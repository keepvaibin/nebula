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
    const s32 sQuakeTime = 80;
    const f32 sShakeCameraNormalVelocity = 1.0f;
    const f32 sShakeCameraWeakVelocity = 0.5f;
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

    MR::setClippingTypeSphere(this, 2600.0f, &mClipCenter);

    initNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvWait));

    appear();
}

void FallDownBridge::control() {
    f32 val = MR::sin(MR::toRadian(MR::abs(mAngle) - 9.090277f));

    if (mAngle < 0.0f) {
        val = -val;
    }

    mAngleSpeed += val * 0.06f;
    mAngle += mAngleSpeed;

    if (!mIsFallen && MR::abs(mAngleSpeed) >= 0.06f) {
        MR::startLevelSound(this, "SE_OJ_LV_CHAL_FD_BRIDGE_FL");
    }

    bool ret = false;

    if (mAngle > mMaxAngle) {
        mAngle = mMaxAngle;
        ret = true;
    }

    if (mAngle < mMinAngle) {
        mAngle = mMinAngle;
        ret = true;
    }

    if (ret) {
        if (MR::abs(mAngleSpeed) > ::sShakeCameraNormalVelocity) {
            MR::startSound(this, "SE_OJ_CHAL_FD_BRIDGE_END");
            MR::shakeCameraNormal();
        } else {
            if (MR::abs(mAngleSpeed) > ::sShakeCameraWeakVelocity) {
                MR::shakeCameraWeak();
            }
        }

        mAngleSpeed *= -0.5f;

        if (MR::abs(mAngleSpeed) < 0.06f) {
            mAngleSpeed = 0.0f;

            if (MR::abs(mAngle) > 9.090277f) {
                mIsFallen = true;
            }
        }
    }

    if (MR::abs(mAngle) < 0.1f && MR::abs(mAngleSpeed) < 0.06f) {
        mAngle = 0.0f;
        mAngleSpeed = 0.0f;
    }

    mAngleSpeed *= 0.99f;
}

bool FallDownBridge::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (!mIsFallen && isNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvWait)) && (msg == ACTMES_BALL_DASH_WALL || msg == ACTMES_BALL_TOUCH_WALL)) {
        LiveActor* pHost = pSender->mHost;

        if (!MR::isBindedWall(pHost)) {
            return false;
        }

        Vec3f vec6C(*MR::getWallNormal(pHost));
        MR::normalizeOrZero(&vec6C);

        Pos3f mtx60;
        mtx60.set(getBaseMtx());

        Vec3f vec78;
        mtx60.getXDir(vec78);

        f32 dot = vec78.dot(vec6C);

        if (MR::abs(dot) < 0.1f) {
            return false;
        }

        f32 val;

        if (msg == ACTMES_BALL_DASH_WALL) {
            val = 0.45f;
            MR::startSound(this, "SE_OJ_CHAL_FD_BRIDGE_HIT");
        } else {
            val = 0.3f;
        }

        if (dot < 0.0f) {
            val = -val;
        }

        mAngleSpeed += val;

        setNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvQuake));

        return true;
    }

    return false;
}

void FallDownBridge::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();

    Pos3f mtx58;
    mtx58.set(getBaseMtx());

    Pos3f mtx88;
    mtx88.identity();

    Vec3f vecC4;

    if (mAngle >= 0.0f) {
        vecC4.set< f32 >(0.0f, 0.0f, 0.0f);
    } else {
        vecC4.set(mScale.x * 400.0f, 0.0f, 0.0f);
    }

    mtx88.setTrans(-vecC4);

    Pos3f baseMtx;
    baseMtx.identity();
    baseMtx.makeRotate(Vec3f(0, 0, 1), MR::toRadian(mAngle));

    baseMtx.concat(baseMtx, mtx88);

    MR::addTransMtx(baseMtx, vecC4);

    baseMtx.concat(mtx58, baseMtx);

    MR::setBaseTRMtx(this, baseMtx);

    baseMtx.mult(::sBaseClipCentor, mClipCenter);
}

void FallDownBridge::exeWait() {
}

void FallDownBridge::exeQuake() {
    if (getNerveStep() > ::sQuakeTime) {
        setNerve(GET_NERVE(FallDownBridge, FallDownBridgeNrvWait));
    }
}
