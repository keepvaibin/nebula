#pragma once

#include "Game/NameObj/NameObj.hpp"

class ActorLightCtrl;
struct AreaLightInfo;
class LightDataHolder;
class LightPointCtrl;
class LightZoneDataHolder;
class ResourceHolder;
class LightAreaHolder;

class LightDirector : public NameObj {
public:
    LightDirector();

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    void initData();
    void loadLightPlayer() const;
    void loadLightCoin() const;

    LightAreaHolder* mAreaHolder;
    LightDataHolder* mDataHolder;
    LightZoneDataHolder* mZoneDataHolder;
    AreaLightInfo* mDefaultAreaLight;
    const ActorLightCtrl* mPlayerLightCtrl;
    LightPointCtrl* mPointCtrl;
    ResourceHolder* mResourceHolder;
};
