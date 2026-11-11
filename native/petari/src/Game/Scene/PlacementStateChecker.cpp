#include "Game/Scene/PlacementStateChecker.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"

PlacementStateChecker::PlacementStateChecker(const char* pName) : NameObj(pName), mCurrentZoneId(-1) {
    lumacore::setClass(this, lumacore::kClassId< PlacementStateChecker >);
}

void PlacementStateChecker::setCurrentPlacementZoneId(int zoneID) {
    mCurrentZoneId = zoneID;
}

void PlacementStateChecker::clearCurrentPlacementZoneId() {
    mCurrentZoneId = -1;
}

int PlacementStateChecker::getCurrentPlacementZoneId() const {
    return mCurrentZoneId;
}

namespace MR {
    PlacementStateChecker* getPlacementStateChecker() {
        return MR::getSceneObj< PlacementStateChecker >(SceneObj_PlacementStateChecker);
    }
};  // namespace MR
