#include "Game/MapObj/TransparentWall.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/InvisiblePolygonObj.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

TransparentWall::TransparentWall(const char* pName) : InvisiblePolygonObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< TransparentWall >);
}

void TransparentWall::control() {
    if (!mIsSolid) {
        for (u32 i = 0; i < 10; i++) {
            if (mSolidForms & (1 << i) && MR::isPlayerElementMode(i)) {
                mIsSolid = true;
                break;
            }
        }

        if ((mSolidForms & 1024) && MR::isPlayerSkating()) {
            mIsSolid = true;
        }

        if ((mSolidForms & 2048) && !MR::isPlayerTeresaDisappear()) {
            mIsSolid = true;
        }

        if (mIsSolid) {
            MR::validateCollisionParts(this);
        }
    } else {
        bool isNoFormLeft = true;
        for (u32 i = 0; i < 10; i++) {
            if (mSolidForms & (1 << i) && MR::isPlayerElementMode(i)) {
                isNoFormLeft = false;
                break;
            }
        }

        if ((mSolidForms & 1024) && MR::isPlayerSkating()) {
            isNoFormLeft = false;
        }

        if ((mSolidForms & 2048)) {
            isNoFormLeft = MR::isPlayerTeresaDisappear();
        }

        if (isNoFormLeft) {
            MR::invalidateCollisionParts(this);
            mIsSolid = false;
        }
    }
}

void TransparentWall::init(const TableIter& rIter) {
    InvisiblePolygonObj::init(rIter);
    MR::getPlacementArg4WithInit(rIter, &mSolidForms);
    makeActorAppeared();
    mIsSolid = false;
    MR::invalidateCollisionParts(this);
    HitSensor* sensor = getSensor("body");
    sensor->setType(ATYPE_ICEJUMP_WALL);
}
