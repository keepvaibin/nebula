#pragma once

#include "Game/Map/HitInfo.hpp"
#include "Game/Util/TriangleFilter.hpp"

class CollisionPartsFilterBase;

class Binder {
public:
    Binder(MtxPtr, const TVec3f*, const TVec3f*, f32, f32, u32);

    void setTriangleFilter(TriangleFilterBase*);
    void setCollisionPartsFilter(CollisionPartsFilterBase*);

    void setExCollisionParts(CollisionParts* pParts) {
        mExCollisionParts = pParts;
        if (pParts == nullptr) {
            _1EC._2 = false;
        } else {
            _1EC._2 = true;
        }
    }

    void clear();
    const HitInfo* getPlane(int) const;
    u32 copyPlaneArrayAndSortingSensor(HitInfo**, u32);
    static bool compSensor(const HitInfo*, const HitInfo*);
    const TVec3f bind(const TVec3f&);
    void moveAlongHittedPlanes(TVec3f*, TVec3f*, TVec3f*, const TVec3f&, const TVec3f&, HitInfo*, u32, bool*);
    u32 findBindedPos(TVec3f*, TVec3f*, bool*, HitInfo*, u32, bool, bool);
    bool moveWithCollisionParts(TVec3f*, TVec3f*);
    u32 storeCurrentHitInfo(HitInfo*, u32, bool);
    void obtainMomentFixReaction(HitInfo*, u32, TVec3f*, u32);
    void storeContactPlane(HitInfo*, u32);

    inline bool isBindedGround() const {
        return 0.0f <= _C8;
    }

    inline bool isBindedWall() const {
        return 0.0f <= _158;
    }

    inline bool isBindedRoof() const {
        return 0.0f <= _1E8;
    }

    TriangleFilterBase* mTriangleFilter;
    CollisionPartsFilterBase* mCollisionPartsFilter;
    CollisionParts* mExCollisionParts;
    MtxPtr _C;
    const TVec3f* _10;
    const TVec3f* _14;
    f32 mRadius;
    f32 mOffsetY;
    const TVec3f* mOffsetVec;
    u32 _24;
    u32 mPlaneNum;
    HitInfo* mPlane;
    TVec3f mFixReactionVector;
    HitInfo mGroundInfo;
    f32 _C8;
    HitInfo mWallInfo;
    f32 _158;
    HitInfo mRoofInfo;
    f32 _1E8;
    struct {
        // MR:: utilities mask this as a byte (0x80 is _0): fields from the least significant bit up, so each has the
        // bit those masks name.
        u8 : 2;
        u8 _5 : 1;
        u8 _4 : 1;
        u8 _3 : 1;
        u8 _2 : 1;
        u8 _1 : 1;
        u8 _0 : 1;
    } _1EC;
};
