#pragma once

#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>

class DrawBuffer;
class LiveActor;

namespace MR {
    void loadLight(s32);
    void loadLightPlayer();
    void initActorLightInfoLightType(LiveActor*, s32);
    void initActorLightInfoDrawBuffer(LiveActor*, DrawBuffer*);
    void requestPointLight(const LiveActor*, Vec3f, Color8, f32, s32);
};  // namespace MR
