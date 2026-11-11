#include "Game/MapObj/ArrowSwitch.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
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

    NEBULA_TUNABLE(f32, sScale, 1.0f);
    NEBULA_TUNABLE(f32, sPunchSpeed, 6.0f);
    NEBULA_TUNABLE(s32, sRotYTargetNum, 4);
};  // namespace

namespace NrvArrowSwitch {
    NEW_NERVE(ArrowSwitchNrvWait, ArrowSwitch, Wait);
    NEW_NERVE(ArrowSwitchNrvRotate, ArrowSwitch, Rotate);
    NEW_NERVE(ArrowSwitchNrvLock, ArrowSwitch, Lock);
};  // namespace NrvArrowSwitch

ArrowSwitch::ArrowSwitch(const char* pName)
    : LiveActor(pName), mRotAngle(), mRotYTargetIndex(), mRotSpeed(), mSwitchType(-1), mLockAfterRotate(), mIsRotPlus(true), mIsPunch() {
    lumacore::setClass(this, lumacore::kClassId< ArrowSwitch >);
}

void ArrowSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mScale.set(::sScale);
    initModelManagerWithAnm("ArrowSwitch", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);

    Vec3f up;
    MR::calcUpVec(&up, this);
    mGravity.set(-up);

    MR::initShadowFromCSV(this, "Shadow");
    MR::onCalcShadow(this, nullptr);
    MR::onCalcGravity(this);

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
    initNerve(GET_NERVE(ArrowSwitch, ArrowSwitchNrvWait));
    MR::getPlacementArg0WithInit(rIter, &mSwitchType);
    MR::getPlacementArg1WithInit(rIter, &mLockAfterRotate);

    s32 shadowDropLength = -1;
    MR::getPlacementArg2WithInit(rIter, &shadowDropLength);

    if (shadowDropLength >= 0) {
        MR::setShadowDropLength(this, nullptr, shadowDropLength);
    }

    MR::needStageSwitchWriteA(this, rIter);

    if (!mLockAfterRotate) {
        MR::listenStageSwitchOnOffA(this, MR::Functor(this, &ArrowSwitch::listenOnSwitch), MR::Functor(this, &ArrowSwitch::listenOffSwitch));
    }

    makeActorAppeared();
}

void ArrowSwitch::control() {
}

void ArrowSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    Pos3f pos;
    pos.set(getBaseMtx());
    Pos3f rot;
    rot.identity();
    rot.setEulerY(MR::toRadian(mRotAngle));
    pos.concat(pos, rot);
    MR::setBaseTRMtx(this, pos);
}

void ArrowSwitch::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool ArrowSwitch::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgPlayerHitAll(msg)) {
        return requestPunch(pSender, pReceiver);
    }

    return false;
}

bool ArrowSwitch::receiveMsgPush(HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

bool ArrowSwitch::requestPunch(HitSensor* pSender, HitSensor* pReceiver) {
    if (getSensor("body") != pReceiver) {
        return false;
    }

    if (!isNerve(GET_NERVE(ArrowSwitch, ArrowSwitchNrvWait))) {
        return false;
    }

    Vec3f side;
    MR::calcSideVec(&side, this);

    Vec3f sensorDir;
    MR::calcSensorDirection(&sensorDir, pSender, pReceiver);

    if (isPlusLimit()) {
        mIsRotPlus = false;
    } else if (isMinusLimit()) {
        mIsRotPlus = true;
    } else if (side.dot(sensorDir) > 0.0f) {
        mIsRotPlus = true;
    } else {
        mIsRotPlus = false;
    }

    mRotYTargetIndex += mIsRotPlus ? getOneStep() : -getOneStep();
    mRotYTargetIndex = (mRotYTargetIndex + ::sRotYTargetNum) % ::sRotYTargetNum;
    mRotSpeed = mIsRotPlus ? ::sPunchSpeed : -::sPunchSpeed;
    mIsPunch = true;

    MR::invalidateClipping(this);
    setNerve(GET_NERVE(ArrowSwitch, ArrowSwitchNrvRotate));

    return true;
}

void ArrowSwitch::listenOnSwitch() {
    if (mRotYTargetIndex != Direction_Up) {
        return;
    }

    switch (mSwitchType) {
    case -1:
    case SwitchType_HalfPlus:
        mIsRotPlus = true;
        mRotSpeed = ::sPunchSpeed;
        mRotYTargetIndex = Direction_Down;
        break;
    case SwitchType_HalfMinus:
        mIsRotPlus = false;
        mRotSpeed = -::sPunchSpeed;
        mRotYTargetIndex = Direction_Down;
        break;
    case SwitchType_QuarterPlus:
        mIsRotPlus = true;
        mRotSpeed = ::sPunchSpeed;
        mRotYTargetIndex = Direction_Right;
        break;
    case SwitchType_QuarterMinus:
        mIsRotPlus = false;
        mRotSpeed = -::sPunchSpeed;
        mRotYTargetIndex = Direction_Left;
        break;
    default:
        return;
    }

    MR::invalidateClipping(this);
    setNerve(GET_NERVE(ArrowSwitch, ArrowSwitchNrvRotate));
}

void ArrowSwitch::listenOffSwitch() {
    if (mRotYTargetIndex == Direction_Up) {
        return;
    }

    mRotYTargetIndex = Direction_Up;

    switch (mSwitchType) {
    case -1:
    case SwitchType_QuarterPlus:
    case SwitchType_HalfPlus:
        mIsRotPlus = false;
        mRotSpeed = -::sPunchSpeed;
        break;
    case SwitchType_QuarterMinus:
    case SwitchType_HalfMinus:
        mIsRotPlus = true;
        mRotSpeed = ::sPunchSpeed;
        break;
    default:
        return;
    }

    MR::invalidateClipping(this);
    setNerve(GET_NERVE(ArrowSwitch, ArrowSwitchNrvRotate));
}

void ArrowSwitch::exeWait() {
    if (MR::isFirstStep(this)) {
        if (mRotYTargetIndex == Direction_Up) {
            MR::startBtk(this, "Off");
        } else {
            MR::startBtk(this, "On");
        }

        MR::validateClipping(this);
        MR::onCalcShadow(this, nullptr);
    }
}

void ArrowSwitch::exeRotate() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ARROW_SWITCH_ON");
        MR::onCalcShadow(this, nullptr);

        if (mLockAfterRotate) {
            MR::invalidateHitSensors(this);

            if (MR::isExistCollisionParts(this)) {
                MR::invalidateCollisionParts(this);
            }
        }
    }

    MR::startLevelSound(this, "SE_OJ_LV_ARROW_SWITCH_MOVE");
    mRotAngle += mRotSpeed;
    mRotAngle = MR::repeat(mRotAngle, -cNum_exeRotate_f1, cNum_exeRotate_f2);
    f32 diff = MR::repeat(::sRotYTargetList[mRotYTargetIndex] - mRotAngle, -cNum_exeRotate_f3, cNum_exeRotate_f4);

    if ((mIsRotPlus && diff < 0.0f) || (!mIsRotPlus && diff > 0.0f)) {
        mRotAngle = ::sRotYTargetList[mRotYTargetIndex];
        mRotSpeed = 0.0f;

        if (MR::isValidSwitchA(this) && mIsPunch) {
            if (mRotYTargetIndex != 0) {
                MR::onSwitchA(this);
            } else {
                MR::offSwitchA(this);
            }

            MR::startSystemSE("SE_SY_GRAVITY_SWITCHED");
        }

        MR::startSound(this, "SE_OJ_ARROW_SWITCH_STOP");

        mIsPunch = false;

        if (mLockAfterRotate) {
            setNerve(GET_NERVE(ArrowSwitch, ArrowSwitchNrvLock));
        } else {
            setNerve(GET_NERVE(ArrowSwitch, ArrowSwitchNrvWait));
        }
    }
}

void ArrowSwitch::exeLock() {
    if (MR::isFirstStep(this)) {
        if (mRotYTargetIndex == Direction_Up) {
            MR::startBtk(this, "Off");
        } else {
            MR::startBtk(this, "On");
        }

        MR::validateClipping(this);
        MR::onCalcShadow(this, nullptr);
    }
}

bool ArrowSwitch::isPlusLimit() const {
    switch (mSwitchType) {
    case SwitchType_HalfPlus:
        return mRotYTargetIndex == Direction_Down;
    case SwitchType_QuarterPlus:
        return mRotYTargetIndex == Direction_Right;
    case SwitchType_QuarterMinus:
        return mRotYTargetIndex == Direction_Up;
    case SwitchType_HalfMinus:
        return mRotYTargetIndex == Direction_Up;
    default:
        return false;
    }
}

bool ArrowSwitch::isMinusLimit() const {
    switch (mSwitchType) {
    case SwitchType_HalfPlus:
        return mRotYTargetIndex == Direction_Up;
    case SwitchType_QuarterPlus:
        return mRotYTargetIndex == Direction_Up;
    case SwitchType_QuarterMinus:
        return mRotYTargetIndex == Direction_Left;
    case SwitchType_HalfMinus:
        return mRotYTargetIndex == Direction_Down;
    default:
        return false;
    }
}

s32 ArrowSwitch::getOneStep() const {
    switch (mSwitchType) {
    case -1:
    case SwitchType_HalfPlus:
    case SwitchType_HalfMinus:
        return 2;
    case SwitchType_QuarterPlus:
    case SwitchType_QuarterMinus:
        return 1;
    default:
        return 0;
    }
}
