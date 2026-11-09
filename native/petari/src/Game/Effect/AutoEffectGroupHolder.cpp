#include "Game/Effect/AutoEffectGroupHolder.hpp"
#include "Game/Effect/AutoEffectGroup.hpp"
#include "Game/Util/StringUtil.hpp"

AutoEffectGroupHolder::AutoEffectGroupHolder() : mCount(), mIndex() {
}

AutoEffectGroup* AutoEffectGroupHolder::find(const char* pName) const {
    const u32 place = mIndex.find(pName);
    return place != MR::NameIndexOf< MR::NameKeyCase >::cNoValue ? mGroups.begin()[place] : nullptr;
}

void AutoEffectGroupHolder::add(AutoEffectGroup* pGroup) {
    const s32 index = mCount++;
    mGroups.at(index) = pGroup;
    mIndex.add(pGroup->getName(), static_cast< u32 >(index));
}

bool AutoEffectGroupHolder::isExist(const char* pName) const {
    return find(pName) != nullptr;
}

namespace MR {
    namespace Effect {
        bool createAndAddAutoEffectGroup(AutoEffectGroupHolder* pHolder, const char* pName) {
            if (pHolder->isExist(pName)) {
                return false;
            }

            AutoEffectGroup* pGroup = createAutoEffectGroup(pName);

            if (pGroup == nullptr) {
                return false;
            }

            pHolder->add(pGroup);
            return true;
        }

        void registerAutoEffectInfos(AutoEffectGroupHolder* pHolder, EffectKeeper* pKeeper, const LiveActor* pActor, const char* pName) {
            AutoEffectGroup* pGroup = pHolder->find(pName);

            if (pGroup != nullptr) {
                addAutoEffectsFromGroup(pGroup, pKeeper, pActor);
            }
        }

        void registerAutoEffectInfos(AutoEffectGroupHolder* pHolder, PaneEffectKeeper* pKeeper, const LayoutActor* pActor, const char* pName) {
            AutoEffectGroup* pGroup = pHolder->find(pName);

            if (pGroup != nullptr) {
                addAutoEffectsFromGroup(pGroup, pKeeper, pActor);
            }
        }

        void registerAutoEffectInfos(AutoEffectGroupHolder* pHolder, MultiSceneEffectKeeper* pKeeper, const MultiSceneActor* pActor,
                                     const char* pName) {
            AutoEffectGroup* pGroup = pHolder->find(pName);

            if (pGroup != nullptr) {
                addAutoEffectsFromGroup(pGroup, pKeeper, pActor);
            }
        }
    };  // namespace Effect
};  // namespace MR
