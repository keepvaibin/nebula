#pragma once

#include "Game/Util/Array.hpp"
#include "Game/NameObj/NameObj.hpp"

class ChipBase;
class ChipCounter;
class ChipGroup;
class ChipHolder;

class ChipHolder : public NameObj {
public:
    ChipHolder(const char* pName, s32 chipType);

    virtual void init(const TableIter&);

    void registerChipGroup(ChipGroup*);
    ChipGroup* findChipGroup(s32) const;

    ChipCounter* mChipCounter;
    MR::GrowTable< ChipGroup*, 64 > mChipGroups;
    s32 mNumChipGroups;
    s32 mChipType;  ///< Determines whether the chip is blue (`0`) or yellow (`1`).
};

namespace MR {
    void createChipHolder(s32 chipType);
    ChipHolder* getChipHolder(s32 chipType);
    void registerChipGroup(s32 chipType, ChipGroup* pChipGroup);
    void registerChip(s32 chipType, ChipBase*, s32 groupId);
    void noticeGetChip(s32 chipType, ChipBase*, s32 groupId);
    void showChipCounter(s32 chipType, s32 groupId);
    void hideChipCounter(s32 chipType, s32 groupId);
    void requestStartChipCompleteDemo(s32 chipType, s32 groupId);
    void noticeEndChipCompleteDemo(s32 chipType, s32 groupId);
    s32 getGotChipCount(s32 chipType, s32 groupId);
    void activateChipLayout();
    void deactivateChipLayout();
};  // namespace MR
