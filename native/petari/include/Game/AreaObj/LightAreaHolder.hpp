#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class ZoneLightID;

class LightAreaHolder : public AreaObjMgr {
public:
    LightAreaHolder(s32 firstCapacity, const char* pName);

    virtual void initAfterPlacement() override;

    bool tryFindLightID(const Vec3f& rArea, ZoneLightID* pLightID) const;
    void sort();
};
