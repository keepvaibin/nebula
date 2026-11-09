#include "Game/Effect/AutoEffectGroup.hpp"
#include "Game/Effect/AutoEffectInfo.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Util/Table.hpp"

AutoEffectGroup::AutoEffectGroup(const char* pName, int capacity) : mName(pName), mEffects(), mCount() {
    mEffects = new AutoEffectInfo*[capacity];
}

void AutoEffectGroup::add(const TableIter& rIter) {
    AutoEffectInfo* pInfo = new AutoEffectInfo;
    pInfo->init(rIter);
    const s32 index = mCount++;
    mEffects[index] = pInfo;
}

namespace MR {
    namespace Effect {
        void addAutoEffectsFromGroup(const AutoEffectGroup* pGroup, EffectKeeper* pKeeper, const LiveActor* pActor) {
            for (s32 i = 0; i < pGroup->mCount; i++) {
                addAutoEffect(pKeeper, pActor, pGroup->mEffects[i]);
            }
        }

        void addAutoEffectsFromGroup(const AutoEffectGroup* pGroup, PaneEffectKeeper* pKeeper, const LayoutActor* pActor) {
            for (s32 i = 0; i < pGroup->mCount; i++) {
                addAutoEffect(pKeeper, pActor, pGroup->mEffects[i]);
            }
        }

        void addAutoEffectsFromGroup(const AutoEffectGroup* pGroup, MultiSceneEffectKeeper* pKeeper, const MultiSceneActor* pActor) {
            for (s32 i = 0; i < pGroup->mCount; i++) {
                addAutoEffect(pKeeper, pActor, pGroup->mEffects[i]);
            }
        }

        AutoEffectGroup* createAutoEffectGroup(const char* pName) {
            TableIter iter = findTableElementNoCase(getAutoEffectListBinary(), "GroupName", pName, 0);

            if (!iter.isValid()) {
                return nullptr;
            }

            AutoEffectGroup* pGroup = new AutoEffectGroup(pName, getAutoEffectNum(pName));

            while (iter != getAutoEffectListBinary()->end()) {
                pGroup->add(iter);
                iter = findTableElementNoCase(getAutoEffectListBinary(), "GroupName", pName, iter.mIndex + 1);
            }

            return pGroup;
        }
    };  // namespace Effect
};  // namespace MR
