#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SkeletalFishBossRail : public LiveActor {
public:
    SkeletalFishBossRail(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;

    bool isFillUpPoint(s32) const;
    s32 getFillUpNamePos(s32) const;
    void createFillUpFlag();
    void createFillUpNamePosID();
    void linkFillUpNamePosID();

    s32 mRailId;
    bool* mFillUpFlags;
    s32* mNamePosIDs;
};