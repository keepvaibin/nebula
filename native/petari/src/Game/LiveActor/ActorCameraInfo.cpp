#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"

ActorCameraInfo::ActorCameraInfo(const TableIter& rIter) : mCameraSetID(-1), mZoneID() {
    s32 cameraSetID = -1;

    if (MR::getPlacementCameraSetID(rIter, &cameraSetID)) {
        mCameraSetID = cameraSetID;
    }

    mZoneID = MR::getPlacedZoneId(rIter);
}

ActorCameraInfo::ActorCameraInfo(s32 cameraSetID, s32 zoneID) : mCameraSetID(cameraSetID), mZoneID(zoneID) {
}
