#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

class LightArea;

class ZoneLightID {
public:
    ZoneLightID();

    void clear();
    bool isTargetArea(const LightArea*) const;
    bool isOutOfArea() const;

    s32 mZoneId;
    s32 mLightID;
    // The steps in a row the position has been outside the area named (in none, or in another): LightAreaHolder.
    s32 mChangeSteps;
};

// I am assuming they called this "AreaInfo" because the debug map
// tells me there was a function contained in LightZoneInfo called "getAreaInfo"
struct AreaInfo {
    s32 mID;
    const char* mAreaLightName;
};

class LightZoneInfo {
public:
    LightZoneInfo();

    void init(s32);

    const char* getAreaLightNameInZoneData(s32) const;

    s32 mAreaCount;
    AreaInfo* mAreaInfo;
};

class LightZoneDataHolder {
public:
    LightZoneDataHolder();

    void initZoneData();
    const char* getAreaLightNameInZoneData(const ZoneLightID&) const;
    const char* getDefaultStageAreaLightName() const;

    s32 mCount;
    LightZoneInfo* mZoneInfo;
};