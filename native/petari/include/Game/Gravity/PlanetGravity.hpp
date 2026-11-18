#pragma once

#include <nebula_geometry.hpp>

class NameObj;

// PlanetGravity::mGravityType bits; a gravity query names the types it takes.
enum GravityType {
    GravityType_Normal = 1,
    GravityType_Shadow = 2,
    GravityType_Magnet = 4,
    GravityType_MarioLauncher = 8,
};

// PlanetGravity::mGravityPower.
enum GravityPower {
    GravityPower_Light = 0,
    GravityPower_Normal = 1,
    GravityPower_Heavy = 2,
};

class PlanetGravity {
public:
    PlanetGravity();

    virtual void updateMtx(const Pos3f& rMtx);

    virtual bool calcOwnGravityVector(Vec3f* /*pDest*/, f32* /*pScalar*/, const Vec3f& /*rPosition*/) const {
        return false;
    }

    bool calcGravity(Vec3f* pDest, const Vec3f& rPosition) const;
    bool calcGravityFromMassPosition(Vec3f* pDirection, f32* pScalar, const Vec3f& rPosition, const Vec3f& rMassPosition) const;

    bool isInRangeSquare(f32 radius) const;
    bool isInRangeDistance(f32 radius) const;

    f32 getDistant() const {
        return mDistant;
    }

    void setPriority(s32 priority);
    void updateIdentityMtx();

#if defined(NEBULA_PLAIN_PATHS)
    // A sphere outside of which calcOwnGravityVector gives nothing, false when there is none (P-139). It reads the shape
    // as updateMtx left it, and the setters' values the shape's own test reads directly.
    virtual bool calcBound(Vec3f* /*pCenter*/, f32* /*pRadius*/) const {
        return false;
    }
    // Makes the bound again: after every updateMtx (updateIdentityMtx, GravityFollower) and every setter whose value
    // calcOwnGravityVector reads directly.
    void refreshBound();
    // False only when rPosition is outside the bound, where calcGravity gives nothing. Makes the bound again first when
    // mRange or mDistant changed since (the placement sets them after the first updateMtx may have run).
    bool isInBound(const Vec3f& rPosition) {
        if (mBoundRange != mRange || mBoundDistant != mDistant) {
            refreshBound();
        }
        // Written so a NaN anywhere keeps the gravity.
        return mBoundRadiusSquared < 0.0f || !((rPosition - mBoundCenter).squared() > mBoundRadiusSquared);
    }
    // The room isInRangeDistance leaves past a shape (never below 0), false when the range is unlimited.
    bool rangeRoom(f32* pRoom) const {
        if (mRange < 0.0f) {
            return false;
        }

        const f32 room = mRange + mDistant;
        *pRoom = room > 0.0f ? room : 0.0f;
        return true;
    }
#endif

    f32 mRange;
    f32 mDistant;
    s32 mPriority;
    const void* mHost;  // the gravity itself, or the actor it follows
    u32 mGravityType;   // gravity type bits
    s32 mGravityPower;  // gravity power type
    bool mActivated;    // corresponding GlobalGravityObj has switch activated?
    bool mIsInverse;    // invert gravity vector?
    bool mValidFollower;
    bool mIsRegistered;  // is registered by PlanetGravityManager?
    bool mAppeared;      // corresponding GlobalGravityObj has appeared?
#if defined(NEBULA_PLAIN_PATHS)
    Vec3f mBoundCenter;
    f32 mBoundRadiusSquared;  // below 0: no bound
    f32 mBoundRange;          // mRange and mDistant when the bound was made
    f32 mBoundDistant;
#endif
};
