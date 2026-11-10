#include "Game/MapObj/LavaProminenceTriple.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f2, "updateHitSensor.f2", 360.0f);

namespace {
    NEBULA_TUNABLE_LIST(f32, sNoSensorTable, , {
        0.0f, 59.765598f, 134.2f, 176.045f, 222.6f, 278.26199f, 360.0f,
    });

    NEBULA_TUNABLE(f32, sInitRotateVel, 100.0f);
    // Distance a head must pass beyond the view before its drop effect is deleted (it is made at the view's edge).
    NEBULA_TUNABLE(f32, sDropClipLeaveMargin, 150.0f);
    NEBULA_TUNABLE(f32, sToSensorRadius, 1900.0f);
    NEBULA_TUNABLE(f32, sEnvironmentRadius, 2100.0f);
};  // namespace

LavaProminenceTriple::LavaProminenceTriple(const char* pName)
    : LiveActor(pName), mBloomModel(), mBaseRot(0, 0, 0, 1), mCurrRot(0, 0, 0, 1), mSide(0, 0, 1), mFront(1, 0, 0), mRotateVel(::sInitRotateVel),
      mIsAppearDrop1(), mIsAppearDrop2(), mIsAppearDrop3(), mIsEnvironment(), mDefaultAppearTime() {
    mBloomMtx.identity();
}

void LavaProminenceTriple::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    Quat4f rot;
    rot.setEulerDegree(mRotation.x, mRotation.y, mRotation.z);
    rot.getZDir(mFront);
    rot.getXDir(mSide);
    mBaseRot = rot;

    MR::getPlacementArg0NoInit(rIter, &mRotateVel);
    MR::getPlacementArg1NoInit(rIter, &mDefaultAppearTime);
    mAppearTime = mDefaultAppearTime;

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
    }

    const char* objName = nullptr;
    MR::getObjectName(&objName, rIter);

    if (MR::isEqualString(objName, "LavaProminenceEnvironment")) {
        initModelManagerWithAnm("LavaProminenceEnvironment", nullptr, false);
        MR::startBtk(this, "LavaProminenceEnvironment");
        mIsEnvironment = true;
        mBloomModel = MR::createBloomModel(this, mBloomMtx);
        MR::startBtk(mBloomModel, "LavaProminenceEnvironmentBloom");
    } else {
        initModelManagerWithAnm("LavaProminenceTriple", nullptr, false);
        MR::startBtk(this, "LavaProminenceTriple");
        mIsEnvironment = false;
        mBloomModel = MR::createBloomModel(this, mBloomMtx);
    }

    initSound(4, false);
    MR::connectToSceneMapObj(this);

    if (!mIsEnvironment) {
        initHitSensor(1);
        MR::addHitSensorCallbackMapObj(this, "attack", 1, cNum_init_f1 * mScale.y);
    }

    initEffectKeeper(0, nullptr);
    MR::setGroupClipping(this, rIter, 16);

    if (mIsEnvironment) {
        MR::setClippingTypeSphere(this, ::sEnvironmentRadius * mScale.y);
    } else {
        MR::setClippingTypeSphere(this, ::sToSensorRadius * mScale.y);
    }

    if (MR::isValidSwitchAppear(this)) {
        MR::invalidateClipping(this);
    }

    makeActorAppeared();
}

void LavaProminenceTriple::appear() {
    LiveActor::appear();
    mIsAppearDrop1 = true;
    mAppearTime = mDefaultAppearTime;
    MR::emitEffect(this, "Drop1");

    if (!mIsEnvironment) {
        mIsAppearDrop2 = true;
        mIsAppearDrop3 = true;
        MR::emitEffect(this, "Drop2");
        MR::emitEffect(this, "Drop3");
    }
}

void LavaProminenceTriple::control() {
    if (mAppearTime > 0) {
        mAppearTime--;
        return;
    }

    mRotation.y += mRotateVel / cNum_control_f1;
    MR::repeatDegree(&mRotation.y);
    mCurrRot.setRotate(Vec3f(0, 1, 0), MR::toRadian(mRotation.y));
    mBloomModel->mPosition.set(mPosition);
    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_T_MOVE");
    updateEffectClipping();

    if (MR::isValidSwitchAppear(this)) {
        if (!MR::isOnSwitchAppear(this)) {
            kill();
        }
    }
}

void LavaProminenceTriple::calcAndSetBaseMtx() {
    Quat4f rot = mBaseRot;
    rot.mult(rot, mCurrRot);
    Pos3f mtx;
    mtx.zeroTrans();
    mtx.setQT(rot, mPosition);
    MR::setBaseTRMtx(this, mtx);
    mBloomMtx.set(mtx);
}

void LavaProminenceTriple::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    MR::sendMsgEnemyAttackFireStrong(pReceiver, pSender);
}
void LavaProminenceTriple::updateHitSensor(HitSensor* pSensor) {
    Vec3f diff = *MR::getPlayerCenterPos();
    diff -= mPosition;

    if (MR::isNearZero(diff)) {
        return;
    }

    f32 y = mSide.dot(diff);
    f32 x = mFront.dot(diff);
    f32 angle = MR::toDegree(MR::atan2(y, x));
    angle -= mRotation.y;
    MR::repeatDegree(&angle);

    const f32* sensor1 = &::sNoSensorTable[1];

    for (int i = 0; i < 3; i++) {
        s32 index = i * 2;
        s32 nextIndex = index + 1;
        f32 half = (::sNoSensorTable[index] + ::sNoSensorTable[nextIndex]) / cNum_updateHitSensor_f1;

        if (angle < half) {
            int j;

            if (i == 0) {
                angle += cNum_updateHitSensor_f2;
                j = 2;
            } else {
                j = i - 1;
            }

            angle = MR::clamp(angle, sensor1[static_cast< s32 >(j) * 2], sensor1[j * 2 + 1]);
            break;
        }

        if (i == 2) {
            angle = MR::clamp(angle, sensor1[index], sensor1[nextIndex]);
            break;
        }
    }

    angle += mRotation.y;
    MR::repeatDegree(&angle);

    Vec3f offset = mFront * MR::cosDegree(angle);
    offset += mSide * MR::sinDegree(angle);
    offset *= mScale.y * ::sToSensorRadius;
    pSensor->mPosition.set(*getPosition());
    pSensor->mPosition.add(offset);
}
void LavaProminenceTriple::updateEffectClipping() {
    updateEffectClippingIndividual(&mIsAppearDrop1, "Drop1", "Head1");

    if (!mIsEnvironment) {
        updateEffectClippingIndividual(&mIsAppearDrop2, "Drop2", "Head2");
        updateEffectClippingIndividual(&mIsAppearDrop3, "Drop3", "Head3");
    }
}

void LavaProminenceTriple::updateEffectClippingIndividual(bool* pIsAppear, const char* pEffectName, const char* pJointName) {
    Vec3f jointPos;
    MR::copyJointPos(this, pJointName, &jointPos);

    // The drop goes when the head is a margin outside the view and comes back at its edge: with a 1 unit sphere and one
    // test for both, the animated head crossing the edge deleted and made the effect again, restarting its particles.
    if (*pIsAppear) {
        if (MR::isJudgedToClipFrustum(jointPos, 1.0f + ::sDropClipLeaveMargin)) {
            MR::deleteEffect(this, pEffectName);
            *pIsAppear = false;
        }
    } else {
        if (!MR::isJudgedToClipFrustum(jointPos, 1.0f)) {
            MR::emitEffect(this, pEffectName);
            *pIsAppear = true;
        }
    }
}
