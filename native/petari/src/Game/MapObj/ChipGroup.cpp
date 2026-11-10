#include "Game/MapObj/ChipGroup.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/ChipBase.hpp"
#include "Game/MapObj/ChipHolder.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateUIRange_f1, "updateUIRange.f1", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateUIRange_f2, "updateUIRange.f2", 2.0f);

ChipGroup::ChipGroup(const char* pName, s32 chipType) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< ChipGroup >);
    mSwitchCtrl = nullptr;
    mGotCount = 0;
    mTotalCount = 0;
    mStageSwitchArg = -1;
    mChipType = chipType;
    mFlashFrame = -1;
    mCounterTimer = -1;
    mUIRange = -1.0f;
    mUIRangeFixed = -1.0f;
    mIsDeactivated = 0;
    mAlreadyDoneFlag = -1;
    mIsListeningAppear = false;
    mIsCounterVisible = false;
}

void ChipGroup::updateUIRange() {
    if (mUIRangeFixed < 0.0f) {
        Box3f range;

        for (s32 i = 0; i < mTotalCount; i++) {
            Box3f currBound;
            Vec3f pos = mChips[i].mChip->mPosition;

            Vec3f center = pos;
            Vec3f pad(cNum_updateUIRange_f1);
            currBound.set(center - pad, center + pad);

            if (i == 0) {
                range.i.set(currBound.i);
                range.f.set(currBound.f);
            } else {
                range.extend(currBound.i, currBound.f);
            }
        }

        range.getCenter(&mUIPos);

        Vec3f diff;
        diff.sub(range.f, range.i);
        mUIRange = diff.length() / cNum_updateUIRange_f2;
    } else {
        mUIRange = mUIRangeFixed;
    }
}

void ChipGroup::movement() {
    if (!mIsDeactivated) {
        if (mIsCounterVisible) {
            if (mUIPos.distance(*MR::getPlayerPos()) < mUIRange) {
                MR::showChipCounter(mChipType, mStageSwitchArg);
            } else {
                MR::hideChipCounter(mChipType, mStageSwitchArg);
            }
        }

        if (mCounterTimer >= 0) {
            mCounterTimer--;

            if (mCounterTimer < 0) {
                MR::hideChipCounter(mChipType, mStageSwitchArg);
                mCounterTimer = -1;
                mIsCounterVisible = false;
            }
        }
    }
}

void ChipGroup::registerChip(ChipBase* pChip) {
    if (mIsListeningAppear) {
        pChip->makeActorDead();
    }

    if (mIsDeactivated) {
        pChip->deactive();
    }

    mChips.at(mTotalCount).mChip = pChip;
    mTotalCount++;
    updateUIRange();
}

void ChipGroup::noticeEndCompleteDemo() {
    mSwitchCtrl->onSwitchA();
}

s32 ChipGroup::getGotCount() const {
    return mGotCount;
}

bool ChipGroup::isComplete() const {
    for (s32 i = 0; i < mTotalCount; i++) {
        if (!mChips[i].mIsGotten) {
            return false;
        }
    }

    return true;
}

BlueChipGroup::BlueChipGroup(const char* pName) : ChipGroup(pName, ChipBase::Type_Blue) {
    lumacore::setClass(this, lumacore::kClassId< BlueChipGroup >);
}

YellowChipGroup::YellowChipGroup(const char* pName) : ChipGroup(pName, ChipBase::Type_Yellow) {
    lumacore::setClass(this, lumacore::kClassId< YellowChipGroup >);
}

void ChipGroup::init(const TableIter& rIter) {
    MR::getPlacementTrans(rIter, &mUIPos);
    MR::getPlacementArg0WithInit(rIter, &mStageSwitchArg);
    MR::getPlacementArg1WithInit(rIter, &mFlashFrame);
    mUIRangeFixed = -1.0f;
    MR::getPlacementArg2NoInit(rIter, &mUIRangeFixed);

    s32 useDoneFlag = 0;
    MR::getPlacementArg3NoInit(rIter, &useDoneFlag);

    if (useDoneFlag == 1) {
        mIsDeactivated = 0;
        mAlreadyDoneFlag = MR::setupAlreadyDoneFlag("チップ集め済み", rIter, &mIsDeactivated);
    } else {
        mIsDeactivated = 0;
        mAlreadyDoneFlag = -1;
    }

    MR::createChipHolder(mChipType);
    MR::registerChipGroup(mChipType, this);
    mSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);

    if (mSwitchCtrl->isValidSwitchAppear()) {
        MR::listenNameObjStageSwitchOnAppear(this, mSwitchCtrl, MR::Functor(this, &ChipGroup::receiveAppearRequest));
        mIsListeningAppear = true;
    } else {
        mIsListeningAppear = false;
    }

    MR::connectToSceneMapObjMovement(this);
}

void ChipGroup::noticeGet(ChipBase* pChip) {
    if (mIsDeactivated) {
        return;
    }

    MR::showChipCounter(mChipType, mStageSwitchArg);

    if (!mIsListeningAppear || mFlashFrame == -1) {
        mIsCounterVisible = true;
    }

    for (s32 i = 0; i < mTotalCount; i++) {
        bool isMatch = false;

        if (pChip && mChips[i].mChip == pChip) {
            isMatch = true;
        }

        if (isMatch) {
            mChips[i].mIsGotten = true;
            mGotCount++;

            if (isComplete()) {
                MR::requestStartChipCompleteDemo(mChipType, mStageSwitchArg);
                MR::requestMovementOn(pChip);

                if (mAlreadyDoneFlag >= 0) {
                    MR::updateAlreadyDoneFlag(mAlreadyDoneFlag, 1);
                }

                if (mChipType == 0) {
                    MR::startSystemSE("SE_OJ_BLUECHIP_COMPLETE", mGotCount);
                } else if (mChipType == 1) {
                    MR::startSystemSE("SE_OJ_YELLOWCHIP_COMPLETE", mGotCount);
                }

                mCounterTimer = -1;
                mIsCounterVisible = false;
                return;
            }

            return;
        }
    }
}

void ChipGroup::receiveAppearRequest() {
    if (mIsDeactivated) {
        return;
    }

    for (s32 i = 0; i < mTotalCount; i++) {
        if (mFlashFrame >= 0) {
            mChips[i].mChip->appearFlashing(mFlashFrame);
        } else {
            mChips[i].mChip->appearWait();
        }
    }

    MR::showChipCounter(mChipType, mStageSwitchArg);

    if (mFlashFrame >= 0) {
        mCounterTimer = mFlashFrame;
        mIsCounterVisible = false;
    } else {
        mCounterTimer = -1;
        mIsCounterVisible = true;
    }

    if (mChipType == 0) {
        MR::startSystemSE("SE_OJ_BLUECHIP_APPEAR");
    } else if (mChipType == 1) {
        MR::startSystemSE("SE_OJ_YELLOWCHIP_APPEAR");
    }
}
