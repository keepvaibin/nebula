#pragma once


class AreaObjMgr;
class ClippingActorInfo;
class ClippingActorInfoList;
class TableIter;
class LodCtrl;

// This frame's view cubes of one group around the player.
struct ViewGroupCtrlDataEntry {
    void clear() {
        mIsPlayerInside = false;
        mIsNear = false;
        mIsFar = false;
        mIsHidden = false;
    }

    bool mIsPlayerInside;  // in a cube that does not hide the group
    bool mIsNear;          // models at their near level
    bool mIsFar;           // models at their far level
    bool mIsHidden;
    s32 mInsideHold = 0;  // steps mIsPlayerInside is kept after the player leaves the cubes; survives clear()
};

class ViewGroupCtrl {
public:
    ViewGroupCtrl();

    void initActorInfo(ClippingActorInfo*, s32);
    void initViewGroup(ClippingActorInfoList*);
    // An actor made once the table stands (a plugin's spawn) is in no view group, as an actor without one.
    void initMadeDuringPlay(ClippingActorInfo*);
    void endInitViewGroupTable();
    void entryLodCtrl(LodCtrl*, const TableIter&);
    void update();
    void startInitViewGroupTable();

    AreaObjMgr* mViewCubeMgr;
    s32 mMaxViewGroupId;
    ViewGroupCtrlDataEntry* mViewGroupData;
    s32 mViewCtrlCount;
    LodCtrl** mLodCtrls;
    s32 mLodCtrlCapacity;
    bool mPlanetsFromStart;
};
