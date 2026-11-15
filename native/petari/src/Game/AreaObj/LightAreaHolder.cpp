#include "Game/AreaObj/LightAreaHolder.hpp"
#include "Game/AreaObj/LightArea.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/Map/LightZoneDataHolder.hpp"
#include "Game/System/LumaCore.hpp"

LightAreaHolder::LightAreaHolder(s32 firstCapacity, const char* pName) : AreaObjMgr(firstCapacity, pName) {
    lumacore::setClass(this, lumacore::kClassId< LightAreaHolder >);
    LightFunction::registerLightAreaHolder(this);
}

namespace {
    // Leaving an area, or crossing into another, changes the light once the position has stayed out this many steps
    // in a row: an actor that sways over an edge (a platform, an animated joint, a camera spring) keeps one light.
    // Entering from no area, and a cleared id (a reset), change at once.
    const s32 cLeaveSteps = 8;
}  // namespace

bool LightAreaHolder::tryFindLightID(const Vec3f& rArea, ZoneLightID* pLightID) const {
    AreaObj* pArea = find_in(rArea);
    const LightArea* pLightArea = static_cast< LightArea* >(pArea);

    if (pArea == nullptr ? pLightID->isOutOfArea() : pLightID->isTargetArea(pLightArea)) {
        pLightID->mChangeSteps = 0;
        return false;
    }

    if (!pLightID->isOutOfArea() && ++pLightID->mChangeSteps < cLeaveSteps) {
        return false;
    }

    if (pArea == nullptr) {
        pLightID->clear();
    } else {
        pLightID->mZoneId = pLightArea->mPlacedZoneID;
        pLightID->mLightID = pLightArea->mObjArg0;
        pLightID->mChangeSteps = 0;
    }

    return true;
}

void LightAreaHolder::initAfterPlacement() {
    sort();
}

void LightAreaHolder::sort() {
    for (s32 i = 0; i < mArray.size() - 1; i++) {
        AreaObj* pOriginal = mArray[i];
        AreaObj* pSelected = pOriginal;
        s32 selected = i;

        for (s32 j = i + 1; j < mArray.size(); j++) {
            AreaObj* pCandidate = mArray[j];

            if (pSelected->mObjArg1 > pCandidate->mObjArg1) {
                selected = j;
                pSelected = pCandidate;
            }
        }

        if (i != selected) {
            mArray[i] = pSelected;
            mArray[selected] = pOriginal;
        }
    }
}
