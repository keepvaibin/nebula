#pragma once

#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightZoneDataHolder.hpp"

class LiveActor;
class DrawBuffer;

class ActorLightCtrl {
public:
    ActorLightCtrl(const LiveActor*);

    // lightType below 0: the actor's light info table chooses.
    void init(int lightType, bool);
    void update(bool);
    void loadLight() const;
    void reset();
    bool isSameLight(const ActorLightCtrl*) const;
    const ActorLightInfo* getActorLight() const;
    void initActorLightInfo();
    void tryFindNewAreaLight(bool);
    void updateLightBlend();
    void resetCurrentLightInfo();
    const ActorLightInfo* getTargetActorLight(const AreaLightInfo*) const;

    const LiveActor* mActor;
    s32 mLightType;  // 0 player, 1 strong, 2 weak, 3 planet
    DrawBuffer* mDrawBuffer;
    bool mHasFixedType;  // given at init: no light info table, not sorted by light in its draw buffer
    AreaLightInfo* mAreaLightInf;
    ZoneLightID mLightID;
    const ActorLightInfo* mBlendFrom;  // while blending into a new area's light: &mBlendStart
    ActorLightInfo mBlendStart;        // the light shown when the blend began
    ActorLightInfo mLightInfo;
    s32 mInterpolate;
    s32 mBlendStep;
};
