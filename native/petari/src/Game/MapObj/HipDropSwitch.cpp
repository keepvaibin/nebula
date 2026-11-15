#include "Game/MapObj/HipDropSwitch.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SpringValue.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sOnSomethingAcc = 0;
};  // namespace

namespace NrvHipDropSwitch {
    NEW_NERVE(HipDropSwitchNrvOff, HipDropSwitch, Off);
    NEW_NERVE(HipDropSwitchNrvSwitchDown, HipDropSwitch, SwitchDown);
    NEW_NERVE(HipDropSwitchNrvOn, HipDropSwitch, On);
};  // namespace NrvHipDropSwitch

HipDropSwitch::HipDropSwitch(const char* pName) : LiveActor(pName), mConnector(), mSpringModel(), mIsHipDropped(), mWasOnPlayer(), mIsOnPlayer() {
    lumacore::setClass(this, lumacore::kClassId< HipDropSwitch >);
    mSpringValue = new SpringValue();
    mConnector = new MapObjConnector(this);
    mSpringValue->setParam(0.0f, 0.0f, 0.2f, 0.90f, 0.0f);
}

inline void HipDropSwitch::initStarPointerTarget() {
    Vec3f targetOffs;
    targetOffs.set(0.0f);
    MR::initStarPointerTarget(this, 150.0f, targetOffs);
}

void HipDropSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("HipDropSwitch", nullptr, false);
    MR::connectToSceneMapObjDecorationStrongLight(this);
    MR::initLightCtrl(this);
    initSound(4, false);
    initHitSensor(2);
    MR::addHitSensorMapObj(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorMapObj(this, "hit", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    initStarPointerTarget();
    MR::initCollisionParts(this, "HipDropSwitch", getSensor("body"), nullptr);
    mSpringModel = MR::createCollisionPartsFromLiveActor(this, "Move", getSensor("hit"), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mSpringModel);
    initNerve(GET_NERVE(HipDropSwitch, HipDropSwitchNrvOff));
    MR::needStageSwitchWriteA(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void HipDropSwitch::initAfterPlacement() {
    mConnector->attachToUnder();
}

void HipDropSwitch::control() {
    mIsHipDropped = false;
    mWasOnPlayer = mIsOnPlayer;
    mIsOnPlayer = false;
}

void HipDropSwitch::calcAnim() {
    LiveActor::calcAnim();
    Pos3f mtx;
    MtxPtr jointMtx = MR::getJointMtx(this, "Move");
    mtx.set(jointMtx);
    f32 springValue = mSpringValue->mSpringValue;

    if (!MR::isNearZero(springValue)) {
        Vec3f trans, upVec;
        mtx.getTrans(trans);
        MR::calcUpVec(&upVec, this);
        trans += upVec * springValue;
        mtx.setTrans(trans);
        NebulaMtxCopy(mtx, jointMtx);
    }

    mSpringModel->setMtx(mtx);
}

void HipDropSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mConnector->connect();
}

bool HipDropSwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgStarPieceAttack(msg)) {
        mIsOnPlayer = true;
        return true;
    } else if (MR::isMsgPlayerHipDropFloor(msg) && mSpringModel->mHitSensor == pReceiver && !mIsHipDropped) {
        mIsHipDropped = true;
        return true;
    }

    return false;
}

bool HipDropSwitch::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isMsgFloorTouch(msg) && mSpringModel->mHitSensor == pReceiver) {
        mIsOnPlayer = true;
        return !mWasOnPlayer;
    }

    return false;
}

bool HipDropSwitch::trySwitchDown() {
    if (mIsHipDropped) {
        setNerve(GET_NERVE(HipDropSwitch, HipDropSwitchNrvSwitchDown));
        return true;
    }

    return false;
}

bool HipDropSwitch::tryOn() {
    if (MR::isGreaterStep(this, ::sOnSomethingAcc) && MR::isBckStopped(this)) {
        setNerve(GET_NERVE(HipDropSwitch, HipDropSwitchNrvOn));
        return true;
    }

    return false;
}

void HipDropSwitch::exeOff() {
    if (MR::isFirstStep(this)) {
        mIsHipDropped = false;
        MR::validateClipping(this);
        MR::offSwitchA(this);
        MR::startBck(this, "Wait");
        MR::startBrk(this, "Off");
        mSpringValue->reset();
    }

    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        mIsOnPlayer = true;
    }

    if (!mWasOnPlayer && mIsOnPlayer) {
        mSpringValue->mVelocity += -10.0f;
        MR::startSound(this, "SE_OJ_PNC_KINOKO_BOUND");
    }

    mSpringValue->update();

    if (trySwitchDown()) {
        mSpringValue->reset();
    }
}

void HipDropSwitch::exeSwitchDown() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
    }

    if (MR::isStep(this, ::sOnSomethingAcc)) {
        MR::startBck(this, "On");
        MR::startBrk(this, "On");
    }

    if (tryOn()) {
        return;
    }
}

void HipDropSwitch::exeOn() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
        MR::onSwitchA(this);
        MR::startSound(this, "SE_OJ_HIPDROP_SWITCH_ON");
        MR::shakeCameraNormal();
    }
}
