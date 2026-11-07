#pragma once

#include "Game/Map/LightDataHolder.hpp"
#include "Game/Map/LightZoneDataHolder.hpp"

class Table;
class LightAreaHolder;
struct PointLightInfo;
class ResourceHolder;
class ActorLightCtrl;

class LightFunction {
public:
    static void initLightRegisterAll();
    static void initLightData();
    static ResourceHolder* loadLightArchive();
    static s32 createLightDataParser(Table**);
    static s32 createZoneDataParser(const char*, Table**);
    static void getAreaLightLightData(Table*, int, AreaLightInfo*);
    static const char* getDefaultAreaLightName();
    static s32 getDefaultStepInterpolate();
    static void registerPlayerLightCtrl(const ActorLightCtrl*);
    static void registerLightAreaHolder(LightAreaHolder*);
    static bool tryFindNewAreaLightID(const Vec3f&, ZoneLightID*);
    static AreaLightInfo* getAreaLightInfo(const ZoneLightID&);
    static void blendActorLightInfo(ActorLightInfo*, const ActorLightInfo&, const ActorLightInfo&, f32);
    static void loadActorLightInfo(const ActorLightInfo*);
    static void loadLightInfoCoin(const LightInfoCoin*);
    static void loadPointLightInfo(const PointLightInfo*);
    static void loadAllLightWhite();
    static void calcLightWorldPos(Vec3f*, const LightInfo&);
};
