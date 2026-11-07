#pragma once

#include "Game/NameObj/NameObj.hpp"

class ClippingActorHolder;
class ClippingGroupHolder;
class ClippingJudge;
class LiveActor;
class LodCtrl;

class ClippingDirector : public NameObj {
public:
    ClippingDirector();

    virtual void movement();

    void endInitActorSystemInfo();
    void registerActor(LiveActor*);
    void initActorSystemInfo(LiveActor*, const TableIter&);
    void joinToGroupClipping(LiveActor*, const TableIter&, int);
    void entryLodCtrl(LodCtrl*, const TableIter&);

    ClippingJudge* mJudge;
    ClippingActorHolder* mActorHolder;
    ClippingGroupHolder* mGroupHolder;
};

namespace MR {
    ClippingDirector* getClippingDirector();
    void addToClippingTarget(LiveActor*);
    void removeFromClippingTarget(LiveActor*);
};  // namespace MR
