#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"

PlacementId::PlacementId(s32 a1, const TableIter& rIter) {
    mZoneID = -1;
    initialize(a1, rIter);
}

PlacementId::PlacementId(s32 a1, s32 zoneId) {
    _0 = a1;
    mZoneID = zoneId;
}

void PlacementId::initialize(s32 a1, const TableIter& rIter) {
    _0 = a1;
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
