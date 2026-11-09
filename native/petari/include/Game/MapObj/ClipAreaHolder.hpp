#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Util/CollisionPartsFilter.hpp"
#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class ClipArea;

class ClipAreaHolder : public DeriveActorGroup< ClipArea > {
public:
    ClipAreaHolder(const char*);

    virtual void init(const TableIter& rIter);

    bool isInArea(const Vec3f& rPos, f32 radius) const;

    bool mIsActive;
};

class ClipAreaCollisionFilter : public CollisionPartsFilterBase {
public:
    ClipAreaCollisionFilter(const Vec3f* pClipCenter, f32 clipRadius);

    virtual bool isInvalidParts(const CollisionParts*) const override;

    const Vec3f* mClipCenter;
    f32 mClipRadius;
};

namespace MR {
    void createClipAreaHolder();
    bool isExistClipAreaHolder();
    void addClipArea(ClipArea*);
    bool isInClipArea(const Vec3f& rPos, f32 radius);
    bool isActiveClipArea();
    void activateClipArea();
    void deactivateClipArea();
    void setBinderExceptSensorType(LiveActor*, const Vec3f* pClipCenter, f32 clipRadius);
};  // namespace MR
