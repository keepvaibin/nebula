#pragma once

#include "Game/Util/JMapInfo.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JGeometry/TVec.hpp>

class CollisionParts;
class HitSensor;

class Triangle {
public:
    Triangle();

    void fillData(CollisionParts*, u32, HitSensor*);
    const char* getHostName() const;
    s32 getHostPlacementZoneID() const;
    bool isHostMoved() const;
    bool isValid() const;
    const TVec3f* getNormal(int) const;
    const TVec3f* getFaceNormal() const;
    const TVec3f* getEdgeNormal(int) const;
    const TVec3f* getPos(int) const;
    const TVec3f* calcAndGetNormal(int);
    const TVec3f* calcAndGetEdgeNormal(int);
    const TVec3f* calcAndGetPos(int);
    void calcForceMovePower(TVec3f*, const TVec3f&) const;
    JMapInfoIter getAttributes() const;
    TPos3f* getBaseMtx() const;
    TPos3f* getBaseInvMtx() const;
    TPos3f* getPrevBaseMtx() const;

    HitSensor* getSensor() const {
        return mSensor;
    }

    CollisionParts* mParts;
    u32 mIdx;
    HitSensor* mSensor;
    TVec3f mNormals[4];
    TVec3f mPos[3];
};

class HitInfo {
public:
    HitInfo();

    bool isCollisionAtFace() const;
    bool isCollisionAtEdge() const;
    bool isCollisionAtCorner() const;

    Triangle mParentTriangle;
    f32 _60;
    TVec3f mHitPos;
    TVec3f _70;
    TVec3f _7C;
    u8 _88;
};
