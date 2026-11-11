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
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 250.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRotate_f1, "exeRotate.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRotate_f2, "exeRotate.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRotate_f3, "exeRotate.f3", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRotate_f4, "exeRotate.f4", 360.0f);

namespace {
    NEBULA_TUNABLE_LIST(f32, sRotYTargetList, , {0.0f, 90.0f, 180.0f, -90.0f});

    NEBULA_TUNABLE(f32, sPunchSpeed, 6.0f);
    NEBULA_TUNABLE(s32, sRotYTargetNum, 4);
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
        MR::addHitSensorMapObj(this, "body", 8, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, cNum_init_f3));
    } else {
        initHitSensor(2);
        MR::addHitSensorMapObj(this, "body", 8, cNum_init_f4, Vec3f(0.0f, cNum_init_f5, cNum_init_f6));
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
    mRotAngle = MR::repeat(mRotAngle, -cNum_exeRotate_f1, cNum_exeRotate_f2);
    f32 diff = MR::repeat(::sRotYTargetList[mRotYTargetIndex] - mRotAngle, -cNum_exeRotate_f3, cNum_exeRotate_f4);

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
