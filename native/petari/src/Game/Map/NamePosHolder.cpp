#include "Game/Map/NamePosHolder.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlacementLink.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

NamePosHolder::NamePosHolder() : NameObj("位置テーブル保持"), mPosNum(), mInfos(), mIndex(), mNextSame() {
    lumacore::setClass(this, lumacore::kClassId< NamePosHolder >);
    mPosNum = MR::getGeneralPosNum();
    mInfos = new NamePosInfo[mPosNum];
    mNextSame = new s32[mPosNum > 0 ? mPosNum : 1];
    mIndex.reset(static_cast< u32 >(mPosNum));

    // Entries of one name chain in table order. pLastSame[first]: the latest entry of the name whose first entry is first.
    s32* pLastSame = new s32[mPosNum > 0 ? mPosNum : 1];
    for (s32 i = 0; i < mPosNum; i++) {
        NamePosInfo* curInf = &mInfos[i];
        curInf->mName = "";
        curInf->mLinkInfo = nullptr;
        curInf->mRegisteredObj = nullptr;
        MR::getGeneralPosData(&curInf->mName, &curInf->mPosition, &curInf->mRotation, &curInf->mLinkInfo, i);

        mNextSame[i] = -1;
        if (mIndex.add(curInf->mName, static_cast< u32 >(i))) {
            pLastSame[i] = i;
        } else {
            const s32 first = static_cast< s32 >(mIndex.find(curInf->mName));
            // A row with no name is not chained.
            if (first < 0) {
                pLastSame[i] = i;
                continue;
            }
            mNextSame[pLastSame[first]] = i;
            pLastSame[first] = i;
        }
    }
    delete[] pLastSame;
}

NamePosInfo::NamePosInfo() {
}

bool NamePosHolder::tryRegisterLinkObj(const NameObj* pObj, const TableIter& rIter) {
    PlacementLink info = PlacementLink(rIter, true);

    for (s32 i = 0; i < mPosNum; i++) {
        NamePosInfo* currInfo = &mInfos[i];
        if (*currInfo->mLinkInfo == info) {
            currInfo->mRegisteredObj = pObj;
            return true;
        }
    }

    return false;
}

// The first entry of that name, in table order, that pObj may use.
bool NamePosHolder::find(const NameObj* pObj, const char* pName, Vec3f* pPos, Vec3f* pRot) const {
    const u32 first = mIndex.find(pName);
    if (first == MR::NameIndex::cNoValue) {
        return false;
    }

    for (s32 idx = static_cast< s32 >(first); idx >= 0; idx = mNextSame[idx]) {
        NamePosInfo* pInfo = &mInfos[idx];

        if (pObj != nullptr && pInfo->mLinkInfo->isValid() && (pObj != pInfo->mRegisteredObj)) {
            continue;
        }

        pPos->set(pInfo->mPosition);

        if (pRot != nullptr) {
            pRot->set(pInfo->mRotation);
        }

        return true;
    }

    return false;
}

namespace MR {
    NamePosHolder* getNamePosHolder() {
        return MR::getSceneObj< NamePosHolder >(SceneObj_NamePosHolder);
    }
};  // namespace MR
