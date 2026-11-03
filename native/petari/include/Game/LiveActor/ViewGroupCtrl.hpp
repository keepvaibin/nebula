#pragma once


class AreaObjMgr;
class ClippingActorInfo;
class ClippingActorInfoList;
class JMapInfoIter;
class LodCtrl;

struct ViewGroupCtrlDataEntry {
    bool _0;
    bool _1;
    bool _2;
    bool _3;
    bool _4;
};

class ViewGroupCtrl {
public:
    ViewGroupCtrl();

    void initActorInfo(ClippingActorInfo*, s32);
    void initViewGroup(ClippingActorInfoList*);
    void endInitViewGroupTable();
    void entryLodCtrl(LodCtrl*, const JMapInfoIter&);
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
