#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Util/CollisionPartsFilter.hpp"
#include <JSystem/JGeometry/TVec.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class ClipArea;

class ClipAreaHolder : public DeriveActorGroup< ClipArea > {
public:
    ClipAreaHolder(const char*);

    virtual void init(const JMapInfoIter& rIter);

    bool isInArea(const TVec3f&, f32) const;

    bool mIsActive;
};

class ClipAreaCollisionFilter : public CollisionPartsFilterBase {
public:
    ClipAreaCollisionFilter(const TVec3f*, f32);

    virtual bool isInvalidParts(const CollisionParts*) const;

    const TVec3f* _04;
    f32 _08;
};

namespace MR {
    void createClipAreaHolder();
    bool isExistClipAreaHolder();
    void addClipArea(ClipArea*);
    bool isInClipArea(const TVec3f&, f32);
    bool isActiveClipArea();
    void activateClipArea();
    void deactivateClipArea();
    void setBinderExceptSensorType(LiveActor*, const TVec3f&, f32);
};  // namespace MR
