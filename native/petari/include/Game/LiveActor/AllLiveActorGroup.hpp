#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"

class AllLiveActorGroup : public LiveActorGroup {
public:
    AllLiveActorGroup();
    virtual ~AllLiveActorGroup() override;

    void startInitActorSystemInfo();
    void initActorSystemInfo(const TableIter&);
    void endInitActorSystemInfo();

    // The actors that took a star pointer target, in that order; the scene query of the pointer walks them.
    void registerPointerTarget(LiveActor* pActor);

    s32 getPointerTargetNum() const {
        return mPointerTargetNum;
    }

    LiveActor* getPointerTarget(s32 index) const {
        return mPointerTargets[index];
    }

    s32 mActorNum;

private:
    s32 mPointerTargetNumMax;
    s32 mPointerTargetNum;
    LiveActor** mPointerTargets;
};

namespace MR {
    AllLiveActorGroup* getAllLiveActorGroup();
    void startInitLiveActorSystemInfo();
    void initLiveActorSystemInfo(const TableIter&);
    void endInitLiveActorSystemInfo();
};  // namespace MR
