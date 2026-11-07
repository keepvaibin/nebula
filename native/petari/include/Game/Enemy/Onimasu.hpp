#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

/// @brief Tox Box
class Onimasu : public LiveActor {
public:
    /// @brief Creates a new `Onimasu`.
    /// @param pName A pointer to the null-terminated name of the object.
    Onimasu(const char* pName);

    /// @brief Destroys the `Onimasu`.
    virtual ~Onimasu();

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

    Pos3f _8C;
    Vec3f _BC;
    Quat4f _C8;
    Quat4f _D8;
    Quat4f _E8;
    Vec3f _F8;
};

namespace OnimasuFunction {
    bool getPolygonOnRailPoint(Vec3f* pPosition, Vec3f* pNormal, const LiveActor* pActor, int pointNo);
};  // namespace OnimasuFunction
