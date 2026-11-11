#include "Game/Boss/SkeletalFishBossRailHolder.hpp"
#include "Game/Boss/SkeletalFishBossRail.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"

SkeletalFishBossRailHolder::SkeletalFishBossRailHolder(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< SkeletalFishBossRailHolder >);
    // The place getByID answers with when no rail was added.
    mRails[0] = nullptr;
}

void SkeletalFishBossRailHolder::add(SkeletalFishBossRail* pRail) {
    mRails.push_back(pRail);
}

SkeletalFishBossRail* SkeletalFishBossRailHolder::getInOrder(s32 idx) const {
    return mRails[idx];
}

SkeletalFishBossRail* SkeletalFishBossRailHolder::getByID(s32 id) const {
    for (s32 i = 0; i < mRails.size(); i++) {
        SkeletalFishBossRail* rail = getInOrder(i);

        if (id == rail->mRailId) {
            return rail;
        }
    }

    return mRails[0];
}

namespace MR {
    SkeletalFishBossRailHolder* getSkeletalFishBossRailHolder() {
        return MR::getSceneObj< SkeletalFishBossRailHolder >(SceneObj_SkeletalFishBossRailHolder);
    }

    void createSkeletalFishBossRailHolder() {
        MR::createSceneObj(SceneObj_SkeletalFishBossRailHolder);
    }
};  // namespace MR
