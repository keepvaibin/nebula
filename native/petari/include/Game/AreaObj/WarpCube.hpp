#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class ActorCameraInfo;
class PlacementId;

class WarpCube : public AreaObj {
public:
    WarpCube(int formType, const char* pName) : AreaObj(formType, pName), mMapIdInfo() {
    }

    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void draw() const;

    PlacementId* mMapIdInfo;
    ActorCameraInfo* mCameraInfo;
    char* mEventName;
    u16 mInvalidateTimer;
};

class WarpCubeMgr : public AreaObjMgr {
public:
    WarpCubeMgr(s32 firstCapacity, const char* pName) : AreaObjMgr(firstCapacity, pName), mWarpCube() {
    }

    virtual ~WarpCubeMgr() {
    }

    WarpCube* getPairCube(const AreaObj* pObj);
    void setInvalidateTimer(AreaObj* pAreaObj, u16 timer);
    void startEventCamera(const AreaObj* pAreaObj);
    void endEventCamera();

    const WarpCube* mWarpCube;
};
