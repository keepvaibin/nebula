#pragma once

#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>


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
    const Vec3f* getNormal(int) const;
    const Vec3f* getFaceNormal() const;
    const Vec3f* getEdgeNormal(int) const;
    const Vec3f* getPos(int) const;
    const Vec3f* calcAndGetNormal(int);
    const Vec3f* calcAndGetEdgeNormal(int);
    const Vec3f* calcAndGetPos(int);
    void calcForceMovePower(Vec3f*, const Vec3f&) const;
    TableIter getAttributes() const;
    Pos3f* getBaseMtx() const;
    Pos3f* getBaseInvMtx() const;
    Pos3f* getPrevBaseMtx() const;

    HitSensor* getSensor() const {
        return mSensor;
    }

    CollisionParts* mParts;
    u32 mIdx;
    HitSensor* mSensor;
    Vec3f mNormals[4];
    Vec3f mPos[3];
};

class HitInfo {
public:
    HitInfo();

    bool isCollisionAtFace() const;
    bool isCollisionAtEdge() const;
    bool isCollisionAtCorner() const;

    Triangle mParentTriangle;
    f32 mDepth;
    Vec3f mHitPos;
    Vec3f mNormalMove;
    u8 mHitPart;
};
