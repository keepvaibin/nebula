#pragma once

#include "Game/Util/Table.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class PlacementId {
public:
    inline PlacementId() {
    }

    PlacementId(s32 id, s32 zoneId);
    // The zone is the one rIter was placed in.
    PlacementId(s32 id, const TableIter& rIter);

    void initialize(s32, const TableIter&);

    inline bool operator==(const PlacementId& rOther) const {
        bool ret = false;

        if (mId == rOther.mId && mZoneID == rOther.mZoneID) {
            ret = true;
        }

        return ret;
    }

    s32 mId;
    s32 mZoneID;
};

namespace MR {
    PlacementId createPlacementIdFromGroupId(const TableIter&);
    PlacementId createPlacementIdFromClippingGroupId(const TableIter&);
};  // namespace MR
