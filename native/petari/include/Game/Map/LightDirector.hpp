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
    /// @brief Creates a new `LightDirector`.
    LightDirector();

    virtual void init(const TableIter&);
    virtual void movement();

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
