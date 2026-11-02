#pragma once

#include "Game/NameObj/NameObj.hpp"

class RailRider;

class CameraRailHolder : public NameObj {
public:
    struct RailSet {
    public:
        s32 mRailId;
        RailRider* mRailRider;
    };

    class ZoneRail {
    public:
        ZoneRail() : mNumRails(), mRails() {
        }

        u32 mNumRails;
        RailSet* mRails;
    };

    CameraRailHolder(const char*);

    RailRider* getRider(s32 zoneId, s32 railId) const;
    s32 find(s32 zoneId, s32 railId) const;

    ZoneRail* mRails;
};
