#pragma once

#include "Game/Util/Table.hpp"

class PlacementLink {
public:
    PlacementLink(const TableIter&, bool);

    bool operator==(const PlacementLink& rOther) const {
        if (!isValid() || !rOther.isValid()) {
            return false;
        }

        return _0 == rOther._0 && _4 == rOther._4 && _8 == rOther._8;
    }

    void invalidate();
    bool isValid() const;
    void setLinkedInfo(const TableIter&);
    void setLinkInfo(const TableIter&);

    s32 _0;
    s32 _4;
    s32 _8;
};
