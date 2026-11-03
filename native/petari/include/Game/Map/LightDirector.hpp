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

    virtual void init(const JMapInfoIter&);
    virtual void movement();

    void initData();
    void loadLightPlayer() const;
    void loadLightCoin() const;

    LightAreaHolder* _C;
    LightDataHolder* mDataHolder;
    LightZoneDataHolder* mZoneDataHolder;
    AreaLightInfo* mDefaultAreaLight;
    const ActorLightCtrl* _1C;
    LightPointCtrl* mPointCtrl;
    ResourceHolder* mResourceHolder;
};
