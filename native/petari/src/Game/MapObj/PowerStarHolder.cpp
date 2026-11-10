#include "Game/System/NebulaMods.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/LumaCore.hpp"

PowerStarHolder::PowerStarHolder(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< PowerStarHolder >);
    mNumInfos = 0;
}

void PowerStarHolder::init(const TableIter&) {
}

void PowerStarHolder::registerPowerStar(PowerStar* pStar, int starNum) {
    PowerStarRequestInfo* info = new PowerStarRequestInfo();
    info->mStar = pStar;
    info->mStarNum = starNum;
    info->mIsAppear = false;
    mInfos.at(mNumInfos) = info;
    mNumInfos++;
}

void PowerStarHolder::requestAppearPowerStar(int starNum, const Vec3f* pPos, bool forceAppear) {
    NebulaMods::fire(NEBULA_MOD_EVENT_POWER_STAR_APPEAR, starNum);
    PowerStarRequestInfo* info = findPowerStarRequestInfo(starNum);
    // A star number no star is placed for (a scenario list longer than the stars) appears nothing.
    if (info == nullptr) {
        return;
    }
    info->mIsAppear = true;
    PowerStar* star = info->mStar;

    if (pPos != nullptr) {
        star->setDemoAppearPos(*pPos);
    }

    if (forceAppear) {
        star->mIsForceAppear = true;
        star->appear();
    } else {
        star->requestAppear();
    }
}

void PowerStarHolder::appearPowerStarWithoutDemo(int starNum) {
    PowerStarRequestInfo* info = findPowerStarRequestInfo(starNum);
    if (info == nullptr) {
        return;
    }
    info->mIsAppear = true;
    PowerStar* star = info->mStar;
    star->offAppearDemo();
    star->appear();
}

PowerStar* PowerStarHolder::getAppearedPowerStar(int starNum) const {
    for (s32 i = 0; i < mNumInfos; i++) {
        PowerStarRequestInfo* info = mInfos[i];

        if (info->mStarNum == starNum && info->mIsAppear) {
            return info->mStar;
        }
    }

    return nullptr;
}

PowerStarRequestInfo* PowerStarHolder::findPowerStarRequestInfo(int starNum) const {
    for (s32 i = 0; i < mNumInfos; i++) {
        PowerStarRequestInfo* info = mInfos[i];

        if (info->mStarNum == starNum) {
            return info;
        }
    }

    return nullptr;
}

namespace MR {
    void registerPowerStar(PowerStar* pStar, int starNum) {
        EventFunction::getPowerStarHolder()->registerPowerStar(pStar, starNum);
    }
};  // namespace MR

bool PowerStarFunction::isEndPowerStarAppearDemo(int starNum) {
    // A star that has not appeared has no demo to end.
    PowerStar* const pStar = EventFunction::getPowerStarHolder()->getAppearedPowerStar(starNum);
    return pStar != nullptr && pStar->isEndAppearDemo();
}

PowerStar* PowerStarFunction::findPowerStar(int starNum) {
    const PowerStarRequestInfo* const pInfo = EventFunction::getPowerStarHolder()->findPowerStarRequestInfo(starNum);
    return pInfo != nullptr ? pInfo->mStar : nullptr;
}
