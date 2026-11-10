#include "Game/MapObj/TreasureBoxCracked.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/AirBubble.hpp"
#include "Game/MapObj/BenefitItemLifeUp.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/MapObj/BlueChip.hpp"
#include "Game/MapObj/Coin.hpp"
#include "Game/MapObj/YellowChip.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeItemBound_f1, "exeItemBound.f1", 720.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeOpenWait_1, "exeOpenWait.1", 10);

namespace NrvTreasureBoxCracked {
    NEW_NERVE(TreasureBoxCrackedNrvWait, TreasureBoxCracked, Wait);
    NEW_NERVE(TreasureBoxCrackedNrvOpen, TreasureBoxCracked, Open);
    NEW_NERVE(TreasureBoxCrackedNrvItemBound, TreasureBoxCracked, ItemBound);
    NEW_NERVE(TreasureBoxCrackedNrvOpenWait, TreasureBoxCracked, OpenWait);
    NEW_NERVE(TreasureBoxCrackedNrvAlwaysOpen, TreasureBoxCracked, AlwaysOpen);
};  // namespace NrvTreasureBoxCracked

namespace {
    Color8 sPointLightColor(0xFF, 0xFF, 0x96, 0xFF);
    NEBULA_TUNABLE_RECORD(Vec, sTopOpenStartOffset, {0.0f, -100.0f, -250.0f});
    NEBULA_TUNABLE_RECORD(Vec, sTopOpenEndOffset, {0.0f, 23.0f, -250.0f});
    NEBULA_TUNABLE_RECORD(Vec, sCoinOffset, {0.0f, 130.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sChipOffset, {0.0f, 170.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sKinokoOneUpOffset, {0.0f, 130.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sKinokoLifeUpOffset, {0.0f, 130.0f, 0.0f});
    NEBULA_TUNABLE_RECORD(Vec, sAirBubbleOffset, {0.0f, 220.0f, 0.0f});
    NEBULA_TUNABLE(f32, sFirstSpeed, 15.0f);
    NEBULA_TUNABLE(f32, sAccelRate, 1.2f);
    NEBULA_TUNABLE(f32, sBoundSpeedMax, 15.0f);
    NEBULA_TUNABLE(s32, sBoundTime, 20);
    NEBULA_TUNABLE(s32, sDefaultWaitForSwitchOn, 5);
    NEBULA_TUNABLE(f32, sPointLightBrightness, 0.99f);
    NEBULA_TUNABLE(f32, sStepForCollisionMove, 15.0f);
};  // namespace

TreasureBoxCracked::TreasureBoxCracked(const char* pName)
    : LiveActor(pName), mBoxCollider(), mBoxType(), mOpenCondition(), mItem(), mItemType(1), mItemVelocity(0.0f), mItemTargetPos(0.0f),
      mIsItemMoving(), mWaitForSwitchOn(::sDefaultWaitForSwitchOn), mPowerStarId(-1) {
    lumacore::setClass(this, lumacore::kClassId< TreasureBoxCracked >);
    mTopMtx.identity();
}

void TreasureBoxCracked::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchWriteA(this, rIter);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::getPlacementArg2NoInit(rIter, &mOpenCondition);
    const char* objectName;
    MR::getObjectName(&objectName, rIter);

    if (MR::isEqualSubString(objectName, "TreasureBoxCracked")) {
        mBoxType = 1;
    } else if (MR::isEqualSubString(objectName, "TreasureBoxGold")) {
        mBoxType = 2;
    } else {
        mBoxType = 0;
    }

    if (mBoxType == 0) {
        initModelManagerWithAnm("TreasureBox", nullptr, false);
    } else if (mBoxType == 1) {
        initModelManagerWithAnm("TreasureBoxCracked", nullptr, false);
    } else {
        initModelManagerWithAnm("TreasureBoxGold", nullptr, false);
    }

    MR::connectToSceneMapObjStrongLight(this);
    initHitSensor(1);
    MR::addHitSensor(this, "body", ATYPE_KILLER_TARGET_MAPOBJ, 8, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    mTopMtx.set(MR::getJointMtx(this, "Top"));
    MR::initCollisionParts(this, "Top", getSensor("body"), mTopMtx.toMtxPtr());
    mBoxCollider = MR::createCollisionPartsFromLiveActor(this, "Box", getSensor("body"), MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mBoxCollider);
    initEffectKeeper(0, nullptr);
    initSound(4, false);

    if (mBoxType == 2) {
        MR::initShadowFromCSV(this, "Shadow");
    }

    if (mOpenCondition == 2) {
        initNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvAlwaysOpen));
    } else {
        initNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvWait));
    }

    initItem(rIter);
    makeActorAppeared();
    MR::addToAttributeGroupSearchTurtle(this);
}

void TreasureBoxCracked::exeWait() {
    if (MR::isFirstStep(this)) {
        if (mBoxType == 1) {
            MR::startBrk(this, "Wait");
            MR::emitEffect(this, "Light");
        } else if (mBoxType == 2) {
            MR::emitEffect(this, "Glow");
        }
    }

    if (mBoxType == 2) {
        MR::requestPointLight(this, mPosition, ::sPointLightColor, ::sPointLightBrightness, -1);
    }
}

void TreasureBoxCracked::exeOpen() {
    Vec3f up;
    MR::calcUpVec(&up, this);

    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Open");
        MR::emitEffect(this, "Open");

        if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
            MR::startSound(this, "SE_OJ_TREASURE_BOX_OPEN_W");
        } else {
            MR::startSound(this, "SE_OJ_TREASURE_BOX_OPEN");
        }

        if (mBoxType == 1) {
            MR::startBrk(this, "Open");
            MR::deleteEffect(this, "Light");
        } else if (mBoxType == 2) {
            MR::deleteEffect(this, "Glow");
        }

        if (mItemType == 0) {
            setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpenWait));
            return;
        }

        if (mItemType == 7) {
            setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpenWait));
            return;
        }

        mItem->makeActorAppeared();
        mIsItemMoving = MR::sendArbitraryMsg(ACTMES_ITEM_START_MOVE, mItem->getSensor(nullptr), getSensor("body"));

        if (!mIsItemMoving) {
            setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpenWait));
            return;
        }

        mItemVelocity.scale(::sFirstSpeed, up);
    }

    mItemVelocity.scaleAdd(::sAccelRate, up, mItemVelocity);
    mItem->mPosition.add(mItemVelocity);

    if (checkItemPos()) {
        mItem->mPosition.set(mItemTargetPos);
        mItemVelocity.zero();

        if (mItemType == 6) {
            setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpenWait));
        } else {
            setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvItemBound));
        }
    }
}

void TreasureBoxCracked::exeItemBound() {
    Vec3f up;
    MR::calcUpVec(&up, this);
    f32 amplitude = ::sBoundSpeedMax - (::sBoundSpeedMax / ::sBoundTime) * getNerveStep();
    f32 height = amplitude * MR::sinDegree((cNum_exeItemBound_f1 / ::sBoundTime) * getNerveStep());
    Vec3f position;
    position.scaleAdd(height, up, mItemTargetPos);
    mItem->mPosition.set(position);

    if (MR::isStep(this, ::sBoundTime)) {
        mItem->mPosition.set(mItemTargetPos);
        MR::sendArbitraryMsg(ACTMES_ITEM_END_MOVE, mItem->getSensor(nullptr), getSensor("body"));
        mIsItemMoving = false;
        setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpenWait));
    }
}

void TreasureBoxCracked::exeOpenWait() {
    if (mItemType == 0 && MR::isStep(this, mWaitForSwitchOn)) {
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }
    } else if (mItemType == 7 && MR::isStep(this, cStep_exeOpenWait_1)) {
        if (mPowerStarId == -1) {
            MR::requestAppearPowerStar(this, mPosition);
        } else {
            MR::requestAppearPowerStar(this, mPowerStarId, mPosition);
        }
    }

    if (MR::isBckStopped(this)) {
        MR::invalidateHitSensors(this);

        if (MR::isValidSwitchDead(this)) {
            MR::onSwitchDead(this);
        }
    }
}

void TreasureBoxCracked::exeAlwaysOpen() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::startBck(this, "Open");
        MR::setBckFrameAndStop(this, MR::getBckCtrl(this)->getEnd());
        Vec3f position;
        NebulaMtxMultVec(getBaseMtx(), &::sTopOpenEndOffset, &position);
        mTopMtx.setTrans(position);
    }
}

void TreasureBoxCracked::control() {
    if (MR::isBckPlaying(this, "Open")) {
        f32 frame = MR::getBckFrame(this);

        if (!(frame >= ::sStepForCollisionMove)) {
            f32 rate = frame / ::sStepForCollisionMove;
            Vec3f offset;
            MR::vecBlend(Vec3f(::sTopOpenStartOffset), Vec3f(::sTopOpenEndOffset), &offset, rate);
            Vec3f position;
            NebulaMtxMultVec(getBaseMtx(), &offset, &position);
            mTopMtx.setTrans(position);

            if (frame == 0.0f) {
                MR::resetAllCollisionMtx(this);
            }
        }
    }
}

void TreasureBoxCracked::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isHalfOpen() && MR::isSensorPlayerOrRide(pReceiver)) {
        Vec3f direction;
        direction.sub(pReceiver->mPosition, pSender->mPosition);
        MR::normalize(&direction);
        Vec3f front;
        MR::calcFrontVec(&front, this);

        if (front.dot(direction) < 0.0f && !MR::isPlayerDamaging()) {
            MR::sendMsgEnemyAttackFlipWeak(pReceiver, pSender);
        }
    }
}

bool TreasureBoxCracked::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpenWait))) {
        return false;
    }

    if (isNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvAlwaysOpen))) {
        return false;
    }

    if (MR::isMsgJetTurtleAttack(msg)) {
        if (mOpenCondition == 1 && pSender->mType != ATYPE_SPECIAL_WEAPON) {
            MR::startBck(this, "Shock");
            MR::startSound(this, "SE_OJ_TERAS_BOX_GOLD_REFUSE");
            MR::startSystemSE("SE_SY_FAILURE_1");
            return false;
        }

        setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpen));
        return true;
    }

    return false;
}

bool TreasureBoxCracked::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgExplosionAttack(msg) && isNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvWait))) {
        if (mOpenCondition == 1) {
            MR::startSound(this, "SE_OJ_TERAS_BOX_GOLD_REFUSE");
            return false;
        }

        setNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvOpen));
        return true;
    }

    return false;
}

void TreasureBoxCracked::initItem(const TableIter& rIter) {
    const char* objectName;
    MR::getObjectName(&objectName, rIter);

    if (MR::isEqualSubString(objectName, "Empty")) {
        mItemType = 0;
        MR::getPlacementArg0NoInit(rIter, &mWaitForSwitchOn);
        return;
    }

    if (MR::isEqualSubString(objectName, "Coin")) {
        mItemType = 1;
    } else if (MR::isEqualSubString(objectName, "YellowChip")) {
        mItemType = 2;
    } else if (MR::isEqualSubString(objectName, "BlueChip")) {
        mItemType = 3;
    } else if (MR::isEqualSubString(objectName, "KinokoOneUp")) {
        mItemType = 4;
    } else if (MR::isEqualSubString(objectName, "KinokoLifeUp")) {
        mItemType = 5;
    } else if (MR::isEqualSubString(objectName, "AirBubble")) {
        mItemType = 6;
    } else if (MR::isEqualSubString(objectName, "PowerStar")) {
        mItemType = 7;
        MR::getPlacementArg3WithInit(rIter, &mPowerStarId);

        if (mPowerStarId != -1) {
            MR::declarePowerStar(this, mPowerStarId);
        } else {
            MR::declarePowerStar(this);
        }

        return;
    }

    // A mod may have a box hold something else of the kinds a box holds.
    static const char* const cItemTypeNames[] = {nullptr, "Coin", "YellowChip", "BlueChip", "KinokoOneUp", "KinokoLifeUp", "AirBubble"};
    if (mItemType >= 1 && mItemType <= 6) {
        const char* pHeld = NebulaMods::objectBecomes(NEBULA_OBJECTS_TREASURE_BOX, cItemTypeNames[mItemType]);
        for (s32 i = 1; i <= 6; i++) {
            if (pHeld != nullptr && MR::isEqualString(pHeld, cItemTypeNames[i])) {
                mItemType = i;
                break;
            }
        }
    }

    if (mItemType == 1) {
        mItem = static_cast< LiveActor* >(MR::createCoin(this, "コイン"));
    } else if (mItemType == 2) {
        mItem = initYellowChip(rIter);
    } else if (mItemType == 3) {
        mItem = initBlueChip(rIter);
    } else if (mItemType == 4) {
        mItem = initKinokoOneUp(rIter);
    } else if (mItemType == 5) {
        mItem = new BenefitItemLifeUp("ライフＵＰキノコ");
    } else if (mItemType == 6) {
        mItem = new AirBubble("空気泡");
    }

    initItemTrans();
    mItem->initWithoutIter();

    if (mItemType != 6) {
        MR::invalidateShadow(mItem, nullptr);
    }

    mItem->makeActorDead();
}

void TreasureBoxCracked::initItemTrans() {
    const Vec* offset = nullptr;

    if (mItemType == 1) {
        offset = &::sCoinOffset;
    } else if (mItemType == 2) {
        offset = &::sChipOffset;
    } else if (mItemType == 3) {
        offset = &::sChipOffset;
    } else if (mItemType == 4) {
        offset = &::sKinokoOneUpOffset;
    } else if (mItemType == 5) {
        offset = &::sKinokoLifeUpOffset;
    } else if (mItemType == 6) {
        offset = &::sAirBubbleOffset;
    }

    NebulaMtxMultVec(getBaseMtx(), offset, &mItemTargetPos);
    mItem->mPosition.set(mPosition);
    mItem->mRotation.set(mRotation);
}

LiveActor* TreasureBoxCracked::initYellowChip(const TableIter& rIter) {
    s32 groupId;
    MR::getPlacementArg0WithInit(rIter, &groupId);
    YellowChip* chip = new YellowChip("イエローチップ");
    chip->setHost(this);
    chip->setGroupID(groupId);
    return chip;
}

LiveActor* TreasureBoxCracked::initBlueChip(const TableIter& rIter) {
    s32 groupId;
    MR::getPlacementArg0WithInit(rIter, &groupId);
    BlueChip* chip = new BlueChip("ブルーチップ");
    chip->setHost(this);
    chip->setGroupID(groupId);
    return chip;
}

LiveActor* TreasureBoxCracked::initKinokoOneUp(const TableIter& /*rIter*/) {
    return new BenefitItemOneUp("１ＵＰキノコ");
}

bool TreasureBoxCracked::checkItemPos() {
    f32 distance = mPosition.distance(mItem->mPosition);
    f32 targetDistance = mPosition.distance(mItemTargetPos);
    Vec3f up;
    MR::calcUpVec(&up, this);

    if (mItemVelocity.dot(up) >= 0.0f) {
        return distance >= targetDistance;
    } else {
        return distance <= targetDistance;
    }
}

bool TreasureBoxCracked::isHalfOpen() const {
    if (isNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvWait))) {
        return false;
    }

    if (isNerve(GET_NERVE(TreasureBoxCracked, TreasureBoxCrackedNrvAlwaysOpen))) {
        return false;
    }

    return !MR::isBckStopped(this);
}
