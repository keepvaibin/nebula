#pragma once

#include "Game/Util/JMapInfo.hpp"

class LiveActor;

// Whether the level's view cubes hide an actor where the player stands. Every actor has one model: the middle and low
// models and their switch distances are gone (see look-changes.tsv).
class LodCtrl {
public:
    LodCtrl(LiveActor*, const JMapInfoIter&);

    void appear();
    void kill();
    void validate();
    void invalidate();
    void update();
    void setFarClipping(f32);
    void setClippingTypeSphereContainsModelBoundingBox(f32);
    void invalidateClipping();
    void setHiddenPtr(const bool*);

    LiveActor* mActor;
    bool mFollowsView;
    const bool* mHidden;
    s16 mViewGroupID;
};

class LodCtrlFunction {
public:
    static bool isExistLodLowModel(const char*);
};
