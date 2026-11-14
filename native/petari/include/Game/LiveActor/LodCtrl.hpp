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
    // The archive pArchive's level ("Middle", "Low") is in the store: a game archive's in ObjectData, a mod's new model's
    // ("<id>:<path>") beside it.
    static bool isExistLevel(const char* pArchive, const char* pLevel);
    // The LodCtrl of the actor in a view group (only those can draw a far model), or null.
    static const LodCtrl* findOf(const LiveActor* pActor);
};
