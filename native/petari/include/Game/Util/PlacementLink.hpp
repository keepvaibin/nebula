#pragma once

#include "Game/Util/Table.hpp"

class PlacementLink {
public:
    PlacementLink(const TableIter&, bool);

    bool operator==(const PlacementLink& rOther) const {
        if (!isValid() || !rOther.isValid()) {
            return false;
        }

        return mLinkId == rOther.mLinkId && mZoneId == rOther.mZoneId && mInfoType == rOther.mInfoType;
    }

    void invalidate();
    bool isValid() const;
    void setLinkedInfo(const TableIter&);
    void setLinkInfo(const TableIter&);

    s32 mLinkId;
    s32 mZoneId;
    s32 mInfoType;
};
