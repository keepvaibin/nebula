#pragma once

#include "Game/Enemy/Onimasu.hpp"
#include "Game/Util/Array.hpp"

class OnimasuPivot : public Onimasu {
public:
    OnimasuPivot(const char* pName);

    virtual void initFromRailPoint() override;
    virtual void incrementNextPoint() override;
    virtual void collectRailPointInfo() override;
    virtual s32 getNextPointNo() const override;
    virtual s32 getLastPointNo() const override;
    virtual const Vec3f& getLastPointNormal() const override;
    virtual const Vec3f& getNextPointNormal() const override;
    virtual void startMoveInner() override;
    virtual void updatePoseInner() override;

    const Vec3f getLastPointPos() const;
    const Vec3f getPivotPointPos() const;
    const Vec3f getNextPointPos() const;

    s32 mCurNormal;
    MR::AssignableArray< Vec3f > mNormals;
    Quat4f mLastPointQuat;
    Quat4f mNextPointQuat;
};
