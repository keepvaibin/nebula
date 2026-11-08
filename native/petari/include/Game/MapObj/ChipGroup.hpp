#pragma once

#include "Game/Util/Array.hpp"
#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class ChipBase;
class StageSwitchCtrl;

class ChipGroup : public NameObj {
public:
    ChipGroup(const char*, s32);

    struct ChipEntry {
        ChipBase* mChip;
        bool mIsGotten;
    };

    virtual void init(const TableIter&);
    virtual void movement();

    void updateUIRange();
    void registerChip(ChipBase*);
    void noticeEndCompleteDemo();
    s32 getGotCount() const;
    bool isComplete() const;
    void noticeGet(ChipBase*);
    void receiveAppearRequest();

    MR::GrowTable< ChipEntry, 5 > mChips;
    StageSwitchCtrl* mSwitchCtrl;
    Vec3f mUIPos;
    s32 mGotCount;
    s32 mTotalCount;
    s32 mStageSwitchArg;
    s32 mChipType;
    s32 mFlashFrame;
    s32 mCounterTimer;
    f32 mUIRange;
    f32 mUIRangeFixed;
    u32 mIsDeactivated;
    s32 mAlreadyDoneFlag;
    u8 mIsListeningAppear;
    u8 mIsCounterVisible;
};

class BlueChipGroup : public ChipGroup {
public:
    BlueChipGroup(const char*);

};

class YellowChipGroup : public ChipGroup {
public:
    YellowChipGroup(const char*);

};
