#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"

PlacementId::PlacementId(s32 id, const TableIter& rIter) {
    initialize(id, rIter);
}

PlacementId::PlacementId(s32 id, s32 zoneId) {
    mId = id;
    mZoneID = zoneId;
}

void PlacementId::initialize(s32 id, const TableIter& rIter) {
    mId = id;
    mZoneID = MR::getPlacedZoneId(rIter);
}

namespace MR {
    PlacementId createPlacementIdFromGroupId(const TableIter& rIter) {
        s32 groupId = -1;
        MR::getPlacementGroupID(rIter, &groupId);
        return PlacementId(groupId, rIter);
    }

    PlacementId createPlacementIdFromClippingGroupId(const TableIter& rIter) {
        s32 groupId = -1;
        MR::getPlacementClippingGroupID(rIter, &groupId);
        return PlacementId(groupId, rIter);
    }
};  // namespace MR
