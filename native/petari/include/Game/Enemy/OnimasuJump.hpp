#pragma once

#include "Game/Enemy/Onimasu.hpp"
#include "Game/Util/Array.hpp"

class OnimasuJump : public Onimasu {
public:
    OnimasuJump(const char* pName);

    virtual void initFromRailPoint() override;
    virtual void incrementNextPoint() override;
    virtual void collectRailPointInfo() override;
    virtual s32 getNextPointNo() const override;
    virtual s32 getLastPointNo() const override;
    virtual const Vec3f& getLastPointNormal() const override;
    virtual const Vec3f& getNextPointNormal() const override;
    virtual void startMoveInner() override;
    virtual void updatePoseInner() override;

    void calcJumpVelocity();

private:
    s32 mCurNormal;
    MR::AssignableArray< Vec3f > mNormals;
};
