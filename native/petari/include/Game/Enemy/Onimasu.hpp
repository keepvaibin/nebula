#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

/// @brief Tox Box
class Onimasu : public LiveActor {
public:
    /// @brief Creates a new `Onimasu`.
    /// @param pName A pointer to the null-terminated name of the object.
    Onimasu(const char* pName);

    /// @brief Destroys the `Onimasu`.
    virtual ~Onimasu();

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void calcAndSetBaseMtx();

    virtual void initFromRailPoint() = 0;
    virtual void incrementNextPoint() = 0;
    virtual void collectRailPointInfo() = 0;
    virtual s32 getNextPointNo() const = 0;
    virtual s32 getLastPointNo() const = 0;
    virtual const TVec3f& getLastPointNormal() const = 0;
    virtual const TVec3f& getNextPointNormal() const = 0;
    virtual void startMoveInner() {
    }
    virtual void updatePoseInner() {
    }

    void calcTargetPose();
    void updatePose();
    void land();
    bool calcTurnDirection(f32* pTurnDir) const;
    void updateStompVelocity();
    void calcGravityDir(TVec3f* pDir) const;
    f32 getGravityScalar() const;
    s32 getTimeToNextPoint() const;
    s32 getWaitTime() const;
    void emitEffectLand();

    void exeWaitForSwitchOn();
    void exeWait();
    void exeMove();
    void exeStamp();

    TPos3f _8C;
    TVec3f _BC;
    TQuat4f _C8;
    TQuat4f _D8;
    TQuat4f _E8;
    TVec3f _F8;
};

namespace OnimasuFunction {
    bool getPolygonOnRailPoint(TVec3f* pPosition, TVec3f* pNormal, const LiveActor* pActor, int pointNo);
};  // namespace OnimasuFunction
