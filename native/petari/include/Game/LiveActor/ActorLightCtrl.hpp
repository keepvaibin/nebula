#pragma once

#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightZoneDataHolder.hpp"

class LiveActor;
class DrawBuffer;

class ActorLightCtrl {
public:
    ActorLightCtrl(const LiveActor*);

    void init(int, bool);
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
    s32 _4;
    DrawBuffer* _8;
    u8 _C;
    AreaLightInfo* mAreaLightInf;
    ZoneLightID mLightID;
    const ActorLightInfo* _1C;
    ActorLightInfo mLightInfo;
    s32 mInterpolate;
    s32 _54;
};
