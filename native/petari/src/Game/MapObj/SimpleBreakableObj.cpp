
#include "Game/MapObj/SimpleBreakableObj.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/MapObj/StageEffectDataTable.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setStateBreak_f1, "setStateBreak.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearItem_f1, "appearItem.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearItem_f2, "appearItem.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appearItem_f3, "appearItem.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTryStartDemo_f1, "exeTryStartDemo.f1", 1000.0f);

namespace {
    NEBULA_TUNABLE(s32, sDefaultBreakOffsetFrame, 60);
    NEBULA_TUNABLE(s32, sDefaultItemNumCoin, 1);
    NEBULA_TUNABLE(s32, sDefaultItemNumStarPiece, 5);
    const char* const cBckNameBreak = "Break";
    const char* const cDemoName = "注目カメラ";
};  // namespace

namespace NrvSimpleBreakableObj {
    NEW_NERVE(HostTypeWait, SimpleBreakableObj, Wait);
    NEW_NERVE(HostTypeTryStartDemo, SimpleBreakableObj, TryStartDemo);
    NEW_NERVE(HostTypeDemo, SimpleBreakableObj, Demo);
};  // namespace NrvSimpleBreakableObj

SimpleBreakableObj::SimpleBreakableObj(const char* pName)
    : SimpleBreakableObjBase(pName), mCameraInfo(), mBreakOffsetFrame(::sDefaultBreakOffsetFrame), mSwitchDeadDelay(-1), mBreakFlags(1),
      mItemType(-1), mItemNum(-1), mKinokoOneUp(), mPlayRiddleSound(), mBreakEnded() {
}

void SimpleBreakableObj::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    MapObjActorUtil::setupInitInfoTypical(&info, mObjectName);
    MapObjActorUtil::setupInitInfoColorChangeArg0(&info, rIter);
    MapObjActorUtil::setupInitInfoTextureChangeArg1(&info, rIter);
    MapObjActorUtil::setupInitInfoShadowLengthArg2(&info, rIter);
    info.setupNerve(GET_NERVE(SimpleBreakableObj, HostTypeWait));
    initialize(rIter, info);

    const char* pObjectName = mObjectName;

    if (MR::isEqualString(pObjectName, "TeresaMansionHoleCover")) {
        mBreakFlags |= 4;
    } else if (MR::isEqualString(pObjectName, "KoopaStatue")) {
        mBreakFlags |= 0x48;
    } else if (MR::isEqualString(pObjectName, "IcicleRock")) {
        mBreakFlags |= 0x12;
    } else if (MR::isEqualString(pObjectName, "SandUpDownTowerBreakableWallA")) {
        mBreakFlags |= 2;
    } else if (MR::isEqualString(pObjectName, "SandUpDownTowerBreakableWallB")) {
        mBreakFlags |= 2;
    } else if (MR::isEqualString(pObjectName, "ConcreteBlock")) {
        mBreakFlags |= 0x180;
    } else if (MR::isEqualString(pObjectName, "SnowBlockA")) {
        mBreakFlags |= 0x20;
    }

    MR::getPlacementArg3NoInit(rIter, &mItemType);
    s32 itemNum = -1;
    MR::getPlacementArg6NoInit(rIter, &itemNum);

    switch (mItemType) {
    case -1:
        mItemNum = itemNum > 0 ? itemNum : ::sDefaultItemNumCoin;
        MR::declareCoin(this, mItemNum);
        break;
    case 1:
        mKinokoOneUp = MR::createKinokoOneUp();
        break;
    case 2:
        MR::declarePowerStar(this);
        break;
    case 3:
        mItemNum = itemNum > 0 ? itemNum : ::sDefaultItemNumStarPiece;
        MR::declareStarPiece(this, mItemNum);
        break;
    }

    MR::getPlacementArg7NoInit(rIter, &mSwitchDeadDelay);
    s32 playRiddleSound = -1;
    MR::getPlacementArg4NoInit(rIter, &playRiddleSound);
    mPlayRiddleSound = playRiddleSound == 1;

    if (MR::initActorCamera(this, rIter, &mCameraInfo)) {
        MR::getPlacementArg5NoInit(rIter, &mBreakOffsetFrame);
    } else {
        mCameraInfo = nullptr;
    }

    if (MR::isDemoCast(this, nullptr)) {
        MR::tryRegisterDemoActionFunctor(this, MR::Functor(this, &SimpleBreakableObj::setStateBreak), nullptr);
    }

    const char* pBckName = ::cBckNameBreak;

    if (MR::isExistBva(this, pBckName)) {
        MR::startBva(this, pBckName);
        MR::setBvaFrameAndStop(this, 0.0f);
    }
}

bool SimpleBreakableObj::receiveMsgPlayerAttack(u32 msg, HitSensor*, HitSensor*) {
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        return false;
    }

    if ((((mBreakFlags & 2U) == 2U && MR::isMsgPlayerSpinAttack(msg)) || ((mBreakFlags & 4U) == 4U && MR::isMsgPlayerHipDropFloor(msg)) ||
         ((mBreakFlags & 8U) == 8U && MR::isMsgExplosionAttack(msg)) || ((mBreakFlags & 0x20U) == 0x20U && MR::isMsgFireBallAttack(msg)) ||
         ((mBreakFlags & 0x10U) == 0x10U && MR::isMsgJetTurtleAttack(msg))) &&
        !isStateBreaking()) {
        setStateBreak();
        return true;
    }

    return false;
}

bool SimpleBreakableObj::receiveMsgEnemyAttack(u32 msg, HitSensor*, HitSensor*) {
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        return false;
    }

    if ((((mBreakFlags & 8U) == 8U && MR::isMsgExplosionAttack(msg)) || ((mBreakFlags & 0x40U) == 0x40U && MR::isMsgEnemyAttack(msg))) &&
        !isStateBreaking()) {
        setStateBreak();
        return true;
    }

    return false;
}

bool SimpleBreakableObj::receiveOtherMsg(u32 msg, HitSensor*, HitSensor*) {
    if ((((mBreakFlags & 0x80U) == 0x80U && MR::isMsgBallDashWall(msg)) || ((mBreakFlags & 0x100U) == 0x100U && MR::isMsgBallDashGround(msg))) &&
        !isStateBreaking()) {
        setStateBreak();
        return true;
    }

    return false;
}

void SimpleBreakableObj::initCaseUseSwitchA(const MapObjActorInitInfo&) {
}

void SimpleBreakableObj::initCaseUseSwitchB(const MapObjActorInitInfo&) {
    MR::listenStageSwitchOnB(this, MR::Functor(this, &SimpleBreakableObj::setStateBreak));
}

void SimpleBreakableObj::control() {
    MapObjActor::control();

    if (mBreakEnded) {
        return;
    }

    if (MapObjActorUtil::isBreakStopped(this) || (MR::isHiddenModel(this) && !isStateBreaking())) {
        endBreak();

        if (!MR::isExistBva(this, ::cBckNameBreak)) {
            kill();
        }
    }

    if (isStateBreaking() && MR::isValidSwitchDead(this)) {
        if (mSwitchDeadDelay != -1) {
            if (mSwitchDeadDelay == 0) {
                MR::onSwitchDead(this);
            }

            mSwitchDeadDelay--;
        }
    }
}

void SimpleBreakableObj::setStateBreak() {
    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        return;
    }

    if (mCameraInfo != nullptr) {
        setNerve(GET_NERVE(SimpleBreakableObj, HostTypeTryStartDemo));
        return;
    }

    MR::startRumbleWithShakeCameraWeak(this, "中", "中", cNum_setStateBreak_f1, FLOAT_MAX);
    MR::invalidateCollisionParts(this);
    MR::stopScene(5);
    startBreak();
}

void SimpleBreakableObj::startBreak() {
    if (mPlayRiddleSound && MR::StageEffect::isRiddleSeTypeStop(mObjectName)) {
        MR::startSystemSE("SE_SY_READ_RIDDLE_S");
    }

    appearItem();
    bool isBreakStarted = MapObjActorUtil::tryStartBreak(this);

    if (mSwitchDeadDelay == -1 && MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    if (!isBreakStarted) {
        if (mCameraInfo != nullptr) {
            MR::hideModel(this);
            return;
        }

        kill();
    }
}

void SimpleBreakableObj::endBreak() {
    if (mCameraInfo != nullptr) {
        MR::endActorCamera(this, mCameraInfo, false);
        MR::endDemo(this, ::cDemoName);
    }

    if (mModelObj != nullptr) {
        MR::validateClipping(mModelObj);
        mModelObj->kill();
    }

    mBreakEnded = true;
}

bool SimpleBreakableObj::isStateBreaking() const {
    if (mModelObj == nullptr) {
        return false;
    }

    if (MR::isDead(mModelObj)) {
        return false;
    }

    return MR::isBckPlaying(mModelObj, ::cBckNameBreak);
}

void SimpleBreakableObj::appearItem() {
    switch (mItemType) {
    case -1:
        MR::appearCoinPop(this, mPosition, 1);
        return;
    case 1:
        MR::appearKinokoOneUpPop(mKinokoOneUp, getBaseMtx(), cNum_appearItem_f1);
        return;
    case 2:
        MR::requestAppearPowerStar(this, mPosition);
        return;
    case 3:
        MR::appearStarPiece(this, mPosition, mItemNum, cNum_appearItem_f2, cNum_appearItem_f3, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        return;
    }
}

void SimpleBreakableObj::exeTryStartDemo() {
    if (MR::tryStartDemoWithoutCinemaFrame(this, ::cDemoName)) {
        MR::startActorCameraTargetSelf(this, mCameraInfo);
        MR::startRumbleWithShakeCameraWeak(this, "中", "中", cNum_exeTryStartDemo_f1, FLOAT_MAX);
        MR::invalidateCollisionParts(this);
        setNerve(GET_NERVE(SimpleBreakableObj, HostTypeDemo));
    }
}

void SimpleBreakableObj::exeDemo() {
    if (MR::isStep(this, MR::getActorCameraFrames(this, mCameraInfo))) {
        MR::endActorCamera(this, mCameraInfo, false);
        MR::endDemo(this, ::cDemoName);
        mCameraInfo = nullptr;
        return;
    }

    if (MR::isStep(this, mBreakOffsetFrame)) {
        startBreak();
    }
}
