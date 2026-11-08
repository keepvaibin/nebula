#pragma once

#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class ClipAreaDrop;

class ClipAreaDropHolder : public DeriveActorGroup< ClipAreaDrop > {
public:
    ClipAreaDropHolder();
};

namespace MR {
    NameObj* createClipAreaDropHolder();
    ClipAreaDrop* getDeadClipAreaDrop();
    bool appearClipAreaDrop(const Vec3f&, f32);
};  // namespace MR
