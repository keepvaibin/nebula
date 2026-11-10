#include "Game/Boss/SkeletalFishBossRail.hpp"
#include "Game/Boss/SkeletalFishBossFunc.hpp"
#include "Game/Boss/SkeletalFishBossRailHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/System/LumaCore.hpp"

SkeletalFishBossRail::SkeletalFishBossRail(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SkeletalFishBossRail >);
    mRailId = -1;
    mFillUpFlags = nullptr;
    mNamePosIDs = nullptr;
}

void SkeletalFishBossRail::init(const TableIter& rIter) {
    MR::createSkeletalFishBossRailHolder();
    initRailRider(rIter);
    MR::getPlacementArg0WithInit(rIter, &mRailId);
    MR::getSkeletalFishBossRailHolder()->add(this);
    createFillUpFlag();
    createFillUpNamePosID();
    makeActorDead();
}

void SkeletalFishBossRail::initAfterPlacement() {
    linkFillUpNamePosID();
}

bool SkeletalFishBossRail::isFillUpPoint(s32 idx) const {
    return mFillUpFlags[idx];
}

s32 SkeletalFishBossRail::getFillUpNamePos(s32 idx) const {
    return mNamePosIDs[idx];
}

void SkeletalFishBossRail::createFillUpFlag() {
    mFillUpFlags = new bool[MR::getRailPointNum(this)];

    for (u32 i = 0; i < MR::getRailPointNum(this); i++) {
        s32 pointArg;
        MR::getRailPointArg0WithInit(this, i, &pointArg);

        if (pointArg == -1) {
            mFillUpFlags[i] = false;
        } else {
            mFillUpFlags[i] = true;
        }
    }
}

void SkeletalFishBossRail::createFillUpNamePosID() {
    mNamePosIDs = new s32[MR::getRailPointNum(this)];

    for (s32 i = 0; i < MR::getRailPointNum(this); i++) {
        mNamePosIDs[i] = -1;
    }
}

void SkeletalFishBossRail::linkFillUpNamePosID() {
    for (s32 i = 0; i < MR::getRailPointNum(this); i++) {
        Vec3f railPos;
        MR::calcRailPointPos(&railPos, this, i);
        mNamePosIDs[i] = SkeletalFishBossFunc::getNearestGuardPosID(railPos);
    }
}
