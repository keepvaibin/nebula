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
    const void* mHost;  // the gravity itself, or the actor it follows
    u32 mGravityType;   // gravity type bits
    s32 mGravityPower;  // gravity power type
    bool mActivated;    // corresponding GlobalGravityObj has switch activated?
    bool mIsInverse;    // invert gravity vector?
    bool mValidFollower;
    bool mIsRegistered;  // is registered by PlanetGravityManager?
    bool mAppeared;      // corresponding GlobalGravityObj has appeared?
};
