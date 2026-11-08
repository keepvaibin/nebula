#pragma once

#include "Game/Enemy/Onimasu.hpp"
#include "Game/Util/Array.hpp"

class OnimasuPivot : public Onimasu {
public:
    /// @brief Creates a new `OnimasuPivot`.
    /// @param pName A pointer to the null-terminated name of the object.
    OnimasuPivot(const char* pName);

    virtual void initFromRailPoint();
    virtual void incrementNextPoint();
    virtual void collectRailPointInfo();
    virtual s32 getNextPointNo() const;
    virtual s32 getLastPointNo() const;
    virtual const Vec3f& getLastPointNormal() const;
    virtual const Vec3f& getNextPointNormal() const;
    virtual void startMoveInner();
    virtual void updatePoseInner();

    const Vec3f getLastPointPos() const;
    const Vec3f getPivotPointPos() const;
    const Vec3f getNextPointPos() const;

    s32 mCurNormal;
    MR::AssignableArray< Vec3f > mNormals;
    Quat4f mLastPointQuat;
    Quat4f mNextPointQuat;
};
