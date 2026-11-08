#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

/// @brief Tox Box
class Onimasu : public LiveActor {
public:
    Onimasu(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void calcAndSetBaseMtx();

    virtual void initFromRailPoint() = 0;
    virtual void incrementNextPoint() = 0;
    virtual void collectRailPointInfo() = 0;
    virtual s32 getNextPointNo() const = 0;
    virtual s32 getLastPointNo() const = 0;
    virtual const Vec3f& getLastPointNormal() const = 0;
    virtual const Vec3f& getNextPointNormal() const = 0;
    virtual void startMoveInner() {
    }
    virtual void updatePoseInner() {
    }

    void calcTargetPose();
    void updatePose();
    void land();
    bool calcTurnDirection(f32* pTurnDir) const;
    void updateStompVelocity();
    void calcGravityDir(Vec3f* pDir) const;
    f32 getGravityScalar() const;
    s32 getTimeToNextPoint() const;
    s32 getWaitTime() const;
    void emitEffectLand();

    void exeWaitForSwitchOn();
    void exeWait();
    void exeMove();
    void exeStamp();

    Pos3f mMoveEffectMtx;
    Vec3f mRotateAxis;
    Quat4f mStartQuat;
    Quat4f mTargetQuat;
    Quat4f mPoseQuat;
    Vec3f mClipCenter;
};

namespace OnimasuFunction {
    bool getPolygonOnRailPoint(Vec3f* pPosition, Vec3f* pNormal, const LiveActor* pActor, int pointNo);
};  // namespace OnimasuFunction
