#pragma once

#include "Game/Map/HitInfo.hpp"
#include "Game/Util/TriangleFilter.hpp"

class CollisionPartsFilterBase;

class Binder {
public:
    // planeCapacity 0: a bind keeps up to 32 planes in a buffer the binders share.
    Binder(MtxPtr pHostMtx, const Vec3f* pHostPos, const Vec3f* pGravity, f32 radius, f32 offsetY, u32 planeCapacity);

    void setTriangleFilter(TriangleFilterBase*);
    void setCollisionPartsFilter(CollisionPartsFilterBase*);

    void setExCollisionParts(CollisionParts* pParts) {
        mExCollisionParts = pParts;
        mIsExCollisionValid = pParts != nullptr;
    }

    void clear();
    const HitInfo* getPlane(int) const;
    u32 copyPlaneArrayAndSortingSensor(HitInfo**, u32);
    static bool compSensor(const HitInfo*, const HitInfo*);
    const Vec3f bind(const Vec3f&);
    void moveAlongHittedPlanes(Vec3f*, Vec3f*, Vec3f*, const Vec3f&, const Vec3f&, HitInfo*, u32, bool*);
    u32 findBindedPos(Vec3f*, Vec3f*, bool*, HitInfo*, u32, bool, bool);
    bool moveWithCollisionParts(Vec3f*, Vec3f*);
    u32 storeCurrentHitInfo(HitInfo*, u32, bool);
    void obtainMomentFixReaction(HitInfo*, u32, Vec3f*, u32);
    void storeContactPlane(HitInfo*, u32);

    inline bool isBindedGround() const {
        return 0.0f <= mGroundDepth;
    }

    inline bool isBindedWall() const {
        return 0.0f <= mWallDepth;
    }

    inline bool isBindedRoof() const {
        return 0.0f <= mRoofDepth;
    }

    TriangleFilterBase* mTriangleFilter;
    CollisionPartsFilterBase* mCollisionPartsFilter;
    CollisionParts* mExCollisionParts;
    MtxPtr mHostMtx;
    const Vec3f* mHostPos;
    const Vec3f* mGravity;
    f32 mRadius;
    f32 mOffsetY;
    const Vec3f* mOffsetVec;
    u32 mPlaneCapacity;
    u32 mPlaneNum;
    HitInfo* mPlane;
    Vec3f mFixReactionVector;
    // The deepest plane of each kind the last bind touched; below 0: none.
    HitInfo mGroundInfo;
    f32 mGroundDepth;
    HitInfo mWallInfo;
    f32 mWallDepth;
    HitInfo mRoofInfo;
    f32 mRoofDepth;
    bool mIsMoveWithCollision;   // carried by the collision parts it stands on
    bool mIsMovingReaction;      // strike checks against moving collision react to its movement
    bool mIsExCollisionValid;    // mExCollisionParts takes part in binds
    bool mIsSkipFirstCheck;      // no strike check before the first step
    bool mIsOffsetInHostFrame;   // mOffsetVec is in the host matrix's frame
    bool mIsStopAtFirstHit;      // for the next bind only
};
