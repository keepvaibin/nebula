#include "Game/Boss/BossKameckMoveRail.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/System/LumaCore.hpp"

BossKameckMoveRail::BossKameckMoveRail(const char* pName) : LiveActor(pName), mRailNo(), mStoppablePointNum() {
    lumacore::setClass(this, lumacore::kClassId< BossKameckMoveRail >);
}

void BossKameckMoveRail::init(const TableIter& rIter) {
    MR::getPlacementArg0WithInit(rIter, &mRailNo);
    initRailRider(rIter);
    makeActorDead();
    mStoppablePointNum = 0;
    s32 pointNum = MR::getRailPointNum(this);
    for (s32 i = 0; i < pointNum; i++) {
        s32 stoppable = -1;
        MR::getRailPointArg0WithInit(this, i, &stoppable);

        if (stoppable != 0) {
            mStoppablePointNum++;
        }
    }
}

void BossKameckMoveRail::calcMovePosition(Vec3f* pPos, s32 idx) const {
    MR::calcRailPointPos(pPos, this, idx);
}

s32 BossKameckMoveRail::calcRandomIndex(s32 idx) const {
    s32 pointNum = MR::getRailPointNum(this);
    s32 last = pointNum - 1;

    if (idx < 0) {
        last = pointNum;
    }

    if (last <= 0) {
        return 0;
    }

    s32 rand = MR::getRandom(0, last);
    if (idx >= 0 && rand >= idx) {
        rand++;
    }

    return rand;
}

s32 BossKameckMoveRail::calcRandomStoppableIndex(s32 idx) const {
    s32 last = mStoppablePointNum - 1;

    if (idx < 0) {
        last = mStoppablePointNum;
    }

    // The rand-th stoppable point, skipping idx.
    s32 rand = (last <= 0) ? 0 : MR::getRandom(0, last);
    s32 pointNum = MR::getRailPointNum(this);
    s32 stoppableSeen = 0;

    for (s32 i = 0; i < pointNum; i++) {
        if (i == idx) {
            continue;
        }

        s32 pointArg = -1;
        MR::getRailPointArg0WithInit(this, i, &pointArg);

        if (pointArg == 0) {
            continue;
        }

        if (stoppableSeen == rand) {
            return i;
        }

        stoppableSeen++;
    }

    return 0;
}
