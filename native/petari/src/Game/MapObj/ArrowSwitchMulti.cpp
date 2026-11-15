#include "Game/MapObj/ArrowSwitchMulti.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/ArrowSwitchMultiHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sRotYTargetList[] = {0.0f, 90.0f, 180.0f, -90.0f};

    const f32 sPunchSpeed = 6.0f;
    const s32 sRotYTargetNum = 4;
};  // namespace

namespace NrvArrowSwitchMulti {
    NEW_NERVE(ArrowSwitchMultiNrvWait, ArrowSwitchMulti, Wait);
    NEW_NERVE(ArrowSwitchMultiNrvRotate, ArrowSwitchMulti, Rotate);
};  // namespace NrvArrowSwitchMulti

ArrowSwitchTarget::ArrowSwitchTarget(const char* pName) : NameObj(pName), mIdInfo(), mStageSwitchCtrl(), mTargetIndex(-1) {
    lumacore::setClass(this, lumacore::kClassId< ArrowSwitchTarget >);
    MR::createArrowSwitchMultiHolder();
}

void ArrowSwitchTarget::init(const TableIter& rIter) {
    s32 switchId;
    MR::getPlacementArg0WithInit(rIter, &switchId);

    mIdInfo = new PlacementId(switchId, rIter);

    MR::getPlacementArg1WithInit(rIter, &mTargetIndex);
    mStageSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);
}

void ArrowSwitchTarget::initAfterPlacement() {
    MR::registerArrowSwitchTarget(this);
}

void ArrowSwitchTarget::onTarget() {
    if (mStageSwitchCtrl->isValidSwitchA()) {
        mStageSwitchCtrl->onSwitchA();
    }

    if (mStageSwitchCtrl->isValidSwitchB()) {
        mStageSwitchCtrl->offSwitchB();
    }
}

void ArrowSwitchTarget::offTarget() {
    if (mStageSwitchCtrl->isValidSwitchA()) {
        mStageSwitchCtrl->offSwitchA();
    }

    if (mStageSwitchCtrl->isValidSwitchB()) {
        mStageSwitchCtrl->onSwitchB();
    }
}

ArrowSwitchMulti::ArrowSwitchMulti(const char* pName)
    : LiveActor(pName), mIdInfo(), mTargetArray(), mRotAngle(), mRotSpeed(), mActiveTargetIndex(), mRotYTargetIndex(), mIsRotPlus(true) {
    lumacore::setClass(this, lumacore::kClassId< ArrowSwitchMulti >);
    MR::createArrowSwitchMultiHolder();
}

void ArrowSwitchMulti::registerTarget(ArrowSwitchTarget* pTarget) {
    // A level's own number: past the four it has room for is no target of it.
    if (pTarget->mTargetIndex >= 0 && pTarget->mTargetIndex < static_cast< s32 >(ARRAY_SIZE(mTargetArray))) {
        mTargetArray[pTarget->mTargetIndex] = pTarget;
    }
}

void ArrowSwitchMulti::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("ArrowSwitch", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    s32 switchId;
    MR::getPlacementArg0WithInit(rIter, &switchId);

    mIdInfo = new PlacementId(switchId, rIter);

    if (MR::isInAreaObj("PlaneModeCube", mPosition)) {
        initHitSensor(1);
        MR::addHitSensorMapObj(this, "body", 8, 100.0f, Vec3f(0.0f, 50.0f, 250.0f));
    } else {
        initHitSensor(2);
        MR::addHitSensorMapObj(this, "body", 8, 100.0f, Vec3f(0.0f, 50.0f, 250.0f));
        MR::addHitSensorMapObjMoveCollision(this, "collision", 8, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
        MR::initCollisionParts(this, "ArrowSwitch", getSensor("collision"), nullptr);
    }

    initSound(4, false);
    MR::registerArrowSwitchMulti(this);
    initNerve(GET_NERVE(ArrowSwitchMulti, ArrowSwitchMultiNrvWait));
    makeActorAppeared();
}

void ArrowSwitchMulti::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    Pos3f pos;
    pos.set(getBaseMtx());
    Pos3f rot;
    rot.identity();
    rot.setEulerY(MR::toRadian(mRotAngle));
    pos.concat(pos, rot);
    MR::setBaseTRMtx(this, pos);
}

bool ArrowSwitchMulti::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return MR::isMsgPlayerHitAll(msg) && requestPunch(pSender, pReceiver);
}

bool ArrowSwitchMulti::requestPunch(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (getSensor("body") != pReceiver) {
        return false;
    }

    if (!isNerve(GET_NERVE(ArrowSwitchMulti, ArrowSwitchMultiNrvWait))) {
        return false;
    }

    if (mIsRotPlus) {
        mRotYTargetIndex++;
        mRotSpeed = ::sPunchSpeed;
    } else {
        mRotYTargetIndex--;
        mRotSpeed = -::sPunchSpeed;
    }

    mRotYTargetIndex = (mRotYTargetIndex + ::sRotYTargetNum) % ::sRotYTargetNum;
    MR::invalidateClipping(this);
    setNerve(GET_NERVE(ArrowSwitchMulti, ArrowSwitchMultiNrvRotate));

    return true;
}

void ArrowSwitchMulti::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBtk(this, mRotYTargetIndex % 2 == 1 ? "On" : "Off");
        MR::validateClipping(this);
    }
}

void ArrowSwitchMulti::exeRotate() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ARROW_SWITCH_ON");
    }

    MR::startLevelSound(this, "SE_OJ_LV_ARROW_SWITCH_MOVE");
    mRotAngle += mRotSpeed;
    mRotAngle = MR::repeat(mRotAngle, -180.0f, 360.0f);
    f32 diff = MR::repeat(::sRotYTargetList[mRotYTargetIndex] - mRotAngle, -180.0f, 360.0f);

    if ((mIsRotPlus && diff < 0.0f) || (!mIsRotPlus && diff > 0.0f)) {
        mRotAngle = ::sRotYTargetList[mRotYTargetIndex];
        mRotSpeed = 0.0f;

        if (mTargetArray[mActiveTargetIndex] != nullptr) {
            mTargetArray[mActiveTargetIndex]->offTarget();
        }
        if (mTargetArray[mRotYTargetIndex] != nullptr) {
            mTargetArray[mRotYTargetIndex]->onTarget();
        }
        mActiveTargetIndex = mRotYTargetIndex;

        MR::startSystemSE("SE_SY_GRAVITY_SWITCHED");
        MR::startSound(this, "SE_OJ_ARROW_SWITCH_STOP");

        setNerve(GET_NERVE(ArrowSwitchMulti, ArrowSwitchMultiNrvWait));
    }
}
