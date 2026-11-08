#include "Game/System/AlreadyDoneFlagInGalaxy.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include <algorithm>

AlreadyDoneInfo::AlreadyDoneInfo() {
    clear();
}

void AlreadyDoneInfo::clear() {
    mNameHash = 0;
    mIsDone = false;
    mZoneId = -1;
    mLinkId = -1;
}

void AlreadyDoneInfo::init(const char* pInfo, s32 zoneId, s32 linkId) {
    mNameHash = MR::getHashCode(pInfo);
    mIsDone = false;
    mZoneId = zoneId;
    mLinkId = linkId;
}

bool AlreadyDoneInfo::isEqual(const AlreadyDoneInfo& otherInfo) const {
    return otherInfo.mNameHash == mNameHash && otherInfo.mZoneId == mZoneId && otherInfo.mLinkId == mLinkId;
}

void AlreadyDoneInfo::set(bool isDone) {
    mIsDone = isDone;
}

AlreadyDoneFlagInGalaxy::AlreadyDoneFlagInGalaxy(int numInfos) : mDoneInfos() {
    mDoneInfos.init(numInfos);
}

void AlreadyDoneFlagInGalaxy::clear() {
    mDoneInfos.clear();
}

// The index of the flag of pName at rIter's zone and link, added if new; *pIsDone receives its value.
u32 AlreadyDoneFlagInGalaxy::setupFlag(const char* pName, const TableIter& rIter, u32* pIsDone) {
    u32 result;

    s32 linkID = -1;
    MR::getPlacementLinkID(rIter, &linkID);
    s32 zoneID = MR::getPlacedZoneId(rIter);

    AlreadyDoneInfo info;
    info.init(pName, zoneID, linkID);

    AlreadyDoneInfo* it = std::find(mDoneInfos.begin(), mDoneInfos.end(), info);

    if (it != mDoneInfos.end()) {
        result = it - mDoneInfos.begin();
        *pIsDone = it->mIsDone;
    } else {
        result = mDoneInfos.size();
        mDoneInfos.push_back(info);
        *pIsDone = 0;
    }

    return result;
}

void AlreadyDoneFlagInGalaxy::updateValue(int index, u32 isDone) {
    mDoneInfos[index].set(isDone != 0);
}
