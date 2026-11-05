#pragma once

#include <nebula_geometry.hpp>

class NameObj;

// Gravity vector calculation flags
#define GRAVITY_TYPE_NORMAL 1
#define GRAVITY_TYPE_SHADOW 2
#define GRAVITY_TYPE_MAGNET 4
#define GRAVITY_TYPE_MARIO_LAUNCHER 8

// Gravity power types
#define GRAVITY_POWER_LIGHT 0
#define GRAVITY_POWER_NORMAL 1
#define GRAVITY_POWER_HEAVY 2

class PlanetGravity {
public:
    PlanetGravity();

    virtual void updateMtx(const Pos3f& rMtx);

    virtual bool calcOwnGravityVector(Vec3f* pDest, f32* pScalar, const Vec3f& rPosition) const {
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

    f32 mRange;
    f32 mDistant;
    s32 mPriority;
    s32 mGravityId;     // read from the stage files but never used
    const void* mHost;  // host object pointer, can be any pointer
    u32 mGravityType;   // gravity type bits
    s32 mGravityPower;  // gravity power type
    bool mActivated;    // corresponding GlobalGravityObj has switch activated?
    bool mIsInverse;    // invert gravity vector?
    bool mValidFollower;
    bool mIsRegistered;  // is registered by PlanetGravityManager?
    bool mAppeared;      // corresponding GlobalGravityObj has appeared?
};
