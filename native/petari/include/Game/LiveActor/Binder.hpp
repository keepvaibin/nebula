#pragma once

#include "Game/Map/HitInfo.hpp"
#include "Game/Util/TriangleFilter.hpp"

class CollisionPartsFilterBase;

class Binder {
public:
    // planeCapacity 0: a bind keeps every plane it meets in a buffer the binders share. Otherwise the binder keeps them in
    // a list of its own (room for planeCapacity to start, grown when a bind meets more), which getPlane reads.
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
    void moveAlongHittedPlanes(Vec3f*, Vec3f*, Vec3f*, const Vec3f&, const Vec3f&, bool*);
    u32 findBindedPos(Vec3f*, Vec3f*, bool*, bool, bool);
    bool moveWithCollisionParts(Vec3f*, Vec3f*);
    u32 storeCurrentHitInfo(bool);
    void obtainMomentFixReaction(Vec3f*, u32);
    void storeContactPlane();
    // The planes of the current bind: the binder's own list, or the shared one.
    HitInfo* planes();
    // planes() with room for `count`; it may move.
    HitInfo* planeRoom(u32 count);

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
    u32 mPlaneCapacity;  // the room of mPlane; 0: the shared list
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
