#pragma once

#include "Game/Util/Array.hpp"

class LayoutActor;
class LiveActor;
class NameObj;

class DemoSimpleCastHolder {
public:
    // Starting capacities: the lists grow past them.
    DemoSimpleCastHolder(s32 liveActorCapacity, s32 layoutActorCapacity, s32 nameObjCapacity);

    void registerActor(LiveActor*);
    void registerActor(LayoutActor*);
    void registerNameObj(NameObj*);
    void movementOnAllCasts();

private:
    MR::Vector< MR::AssignableArray< LiveActor* > > mLiveActors;
    MR::Vector< MR::AssignableArray< LayoutActor* > > mLayoutActors;
    MR::Vector< MR::AssignableArray< NameObj* > > mNameObjs;
};
