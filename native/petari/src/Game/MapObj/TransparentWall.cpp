#include "Game/MapObj/TransparentWall.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/InvisiblePolygonObj.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"

TransparentWall::TransparentWall(const char* pName) : InvisiblePolygonObj(pName) {
}

void TransparentWall::control() {
    if (!mIsPassable) {
        for (u32 i = 0; i < 10; i++) {
            if (mPassFlags & (1 << i) && MR::isPlayerElementMode(i)) {
                mIsPassable = true;
                break;
            }
        }

        if ((mPassFlags & 1024) && MR::isPlayerSkating()) {
            mIsPassable = true;
        }

        if ((mPassFlags & 2048) && !MR::isPlayerTeresaDisappear()) {
            mIsPassable = true;
        }

        if (mIsPassable) {
            MR::validateCollisionParts(this);
        }
    } else {
        bool r29 = true;
        for (u32 i = 0; i < 10; i++) {
            if (mPassFlags & (1 << i) && MR::isPlayerElementMode(i)) {
                r29 = false;
                break;
            }
        }

        if ((mPassFlags & 1024) && MR::isPlayerSkating()) {
            r29 = false;
        }

        if ((mPassFlags & 2048)) {
            r29 = MR::isPlayerTeresaDisappear();
        }

        if (r29) {
            MR::invalidateCollisionParts(this);
            mIsPassable = false;
        }
    }
}

TransparentWall::~TransparentWall() {
}

void TransparentWall::init(const TableIter& rrIter) {
    InvisiblePolygonObj::init(rrIter);
    MR::getPlacementArg4WithInit(rrIter, &mPassFlags);
    makeActorAppeared();
    mIsPassable = false;
    MR::invalidateCollisionParts(this);
    HitSensor* sensor = getSensor("body");
    sensor->setType(ATYPE_ICEJUMP_WALL);
}
