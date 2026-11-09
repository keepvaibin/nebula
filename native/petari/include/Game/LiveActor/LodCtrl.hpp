#pragma once

#include "Game/Util/Table.hpp"

class LiveActor;
class ModelObj;

// What the level's view cubes ask of an actor where the player stands: hidden, its near model or its far model.
class LodCtrl {
public:
    LodCtrl(LiveActor*, const TableIter&);

    void appear();
    void kill();
    void validate();
    void invalidate();
    void update();
    void setFarClipping(f32);
    void setClippingTypeSphereContainsModelBoundingBox(f32);
    void invalidateClipping();
    void showNear();
    void setViewCtrlPtr(const bool* pNear, const bool* pFar, const bool* pHidden);
    void createFarModel(int drawBufferType, int movementType, int calcAnimType);

    LiveActor* mActor;
    ModelObj* mFarModel;
    bool mFollowsView;
    const bool* mNear;
    const bool* mFar;
    const bool* mHidden;
    s32 mViewGroupID;
};

class LodCtrlFunction {
public:
    static bool isExistLodLowModel(const char*);
};
