#include "Game/Map/LightZoneDataHolder.hpp"
#include "Game/AreaObj/LightArea.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Util.hpp"

ZoneLightID::ZoneLightID() {
    mZoneId = -1;
    mLightID = -1;
    mChangeSteps = 0;
}

void ZoneLightID::clear() {
    mZoneId = -1;
    mLightID = -1;
    mChangeSteps = 0;
}

bool ZoneLightID::isTargetArea(const LightArea* pLightArea) const {
    if (mZoneId == pLightArea->mPlacedZoneID && mLightID == pLightArea->mObjArg0) {
        return true;
    }

    return false;
}

bool ZoneLightID::isOutOfArea() const {
    return mLightID < 0;
}

LightZoneInfo::LightZoneInfo() {
    mAreaCount = 0;
    mAreaInfo = 0;
}

const char* LightZoneInfo::getAreaLightNameInZoneData(s32 idx) const {
    for (int i = 0; i < mAreaCount; i++) {
        if (mAreaInfo[i].mID == idx) {
            return mAreaInfo[i].mAreaLightName;
        }
    }

    return LightFunction::getDefaultAreaLightName();
}

LightZoneDataHolder::LightZoneDataHolder() {
    mCount = 0;
    mZoneInfo = 0;
}

void LightZoneDataHolder::initZoneData() {
    mCount = MR::getZoneNum();
    mZoneInfo = new LightZoneInfo[mCount];

    for (s32 i = 0; i < mCount; i++) {
        mZoneInfo[i].init(i);
    }
}

const char* LightZoneDataHolder::getAreaLightNameInZoneData(const ZoneLightID& zoneID) const {
    s32 zoneId = zoneID.mZoneId;

    if (zoneId < 0) {
        zoneId = 0;
    }

    return mZoneInfo[zoneId].getAreaLightNameInZoneData(zoneID.mLightID);
}

// Light -1 names the stage default, the same in every zone.
const char* LightZoneDataHolder::getDefaultStageAreaLightName() const {
    return mZoneInfo->getAreaLightNameInZoneData(-1);
}

void LightZoneInfo::init(s32 zoneID) {
    const char* zoneName = MR::getZoneNameFromZoneId(zoneID);
    Table* parser = 0;
    mAreaCount = LightFunction::createZoneDataParser(zoneName, &parser);
    mAreaInfo = new AreaInfo[mAreaCount];

    for (int i = 0; i < mAreaCount; i++) {
        AreaInfo* inf = &mAreaInfo[i];

        MR::getCsvDataS32(&inf->mID, parser, "LightID", i);
        MR::getCsvDataStr(&inf->mAreaLightName, parser, "AreaLightName", i);
    }
}
