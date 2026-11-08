#pragma once

#include "Game/Enemy/Onimasu.hpp"
#include "Game/Util/Array.hpp"

class OnimasuJump : public Onimasu {
public:
    OnimasuJump(const char* pName);

    virtual void initFromRailPoint();
    virtual void incrementNextPoint();
    virtual void collectRailPointInfo();
    virtual s32 getNextPointNo() const;
    virtual s32 getLastPointNo() const;
    virtual const Vec3f& getLastPointNormal() const;
    virtual const Vec3f& getNextPointNormal() const;
    virtual void startMoveInner();
    virtual void updatePoseInner();

    void calcJumpVelocity();

private:
    s32 mCurNormal;
    MR::AssignableArray< Vec3f > mNormals;
};
