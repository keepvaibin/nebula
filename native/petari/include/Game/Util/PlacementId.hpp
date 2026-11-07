#pragma once

#include "Game/Util/Table.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class PlacementId {
public:
    inline PlacementId() {
    }

    PlacementId(s32, s32);
    PlacementId(s32, const TableIter&);

    void initialize(s32, const TableIter&);

    void operator=(const PlacementId& rhs) {
        _0 = rhs._0;
        mZoneID = rhs.mZoneID;
    }

    inline bool operator==(const PlacementId& rOther) const {
        bool ret = false;

        if (_0 == rOther._0 && mZoneID == rOther.mZoneID) {
            ret = true;
        }

        return ret;
    }

    s32 _0;
    s32 mZoneID;
};

namespace MR {
    PlacementId createPlacementIdFromGroupId(const TableIter&);
    PlacementId createPlacementIdFromClippingGroupId(const TableIter&);
};  // namespace MR
