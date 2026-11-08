#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"

class AllLiveActorGroup : public LiveActorGroup {
public:
    AllLiveActorGroup();

    void startInitActorSystemInfo();
    void initActorSystemInfo(const TableIter&);
    void endInitActorSystemInfo();

    s32 mActorNum;
};

namespace MR {
    AllLiveActorGroup* getAllLiveActorGroup();
    void startInitLiveActorSystemInfo();
    void initLiveActorSystemInfo(const TableIter&);
    void endInitLiveActorSystemInfo();
};  // namespace MR
