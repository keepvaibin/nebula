#pragma once

#include "Game/Util/Array.hpp"

class LayoutActor;
class LiveActor;
class NameObj;

class DemoSimpleCastHolder {
public:
    DemoSimpleCastHolder(s32, s32, s32);

    void registerActor(LiveActor*);
    void registerActor(LayoutActor*);
    void registerNameObj(NameObj*);
    void movementOnAllCasts();

private:
    MR::Vector< MR::AssignableArray< LiveActor* > > mLiveActors;
    MR::Vector< MR::AssignableArray< LayoutActor* > > mLayoutActors;
    MR::Vector< MR::AssignableArray< NameObj* > > mNameObjs;
};
