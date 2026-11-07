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

    bool isInArea(const Vec3f&, f32) const;

    bool mIsActive;
};

class ClipAreaCollisionFilter : public CollisionPartsFilterBase {
public:
    ClipAreaCollisionFilter(const Vec3f*, f32);

    virtual bool isInvalidParts(const CollisionParts*) const;

    const Vec3f* _04;
    f32 _08;
};

namespace MR {
    void createClipAreaHolder();
    bool isExistClipAreaHolder();
    void addClipArea(ClipArea*);
    bool isInClipArea(const Vec3f&, f32);
    bool isActiveClipArea();
    void activateClipArea();
    void deactivateClipArea();
    void setBinderExceptSensorType(LiveActor*, const Vec3f&, f32);
};  // namespace MR
