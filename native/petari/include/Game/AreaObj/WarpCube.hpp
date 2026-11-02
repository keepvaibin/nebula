#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class ActorCameraInfo;
class JMapIdInfo;

class WarpCube : public AreaObj {
public:
    WarpCube(int formType, const char* pName) : AreaObj(formType, pName), mMapIdInfo() {
    }

    virtual ~WarpCube();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;

    JMapIdInfo* mMapIdInfo;
    ActorCameraInfo* mCameraInfo;
    char* mEventName;
    u16 mInvalidateTimer;
};

class WarpCubeMgr : public AreaObjMgr {
public:
    WarpCubeMgr(s32 formType, const char* pName) : AreaObjMgr(formType, pName), mWarpCube() {
    }

    virtual ~WarpCubeMgr() {
    }

    WarpCube* getPairCube(const AreaObj* pObj);
    void setInvalidateTimer(AreaObj* pAreaObj, u16 a2);
    void startEventCamera(const AreaObj* pAreaObj);
    void endEventCamera();

    const WarpCube* mWarpCube;
};
