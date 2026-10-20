#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <JSystem/JGeometry/THex.hpp>

class ClippingJudge : public NameObj {
public:
    ClippingJudge(const char*);

    virtual ~ClippingJudge();
    virtual void init(const JMapInfoIter&);

    virtual void movement();

    bool isJudgedToClipFrustum(const TVec3f&, f32) const;
    bool isJudgedToClipFrustum(const TVec3f&, f32, s32) const;
    void calcViewingVolume(THex3f*, f32);
#ifdef TARGET_PC
    // Nebula, ray-traced shadows (NEBULA_OPTION_RT_SHADOW_CASTERS): whether a ball at `rCenter`, swept along the stage's
    // key light as far as its clip level draws (`index`, as isJudgedToClipFrustum), can reach that level's frustum.
    bool isShadowCasterInView(const TVec3f& rCenter, f32 radius, s32 index) const;
    // Whether ray-traced light keeps clipped light sources drawn this frame.
    bool isLightSourcesWanted() const;
    // Whether the ball of a light source's reach around `rCenter` (light_reach in nebula_game_api.h) meets the view.
    bool isLightInView(const TVec3f& rCenter, f32 reach) const;
    // The traced sun's direction of travel this frame, else the key light's (none: the option is off or the stage has
    // neither).
    void updateShadowCasters();
#endif

    /* 0x0C */ THex3f mFrustum;
    /* 0x06C */ THex3f mClipFrustums[0x8];
    /* 0x36C */ f32 mClipDistances[0x8];
};

namespace MR {
    ClippingJudge* getClippingJudge();
};  // namespace MR
