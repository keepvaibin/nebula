#pragma once


class AutoEffectInfo;
class EffectKeeper;
class JMapInfoIter;
class LayoutActor;
class LiveActor;
class MultiSceneActor;
class MultiSceneEffectKeeper;
class PaneEffectKeeper;

class AutoEffectGroup {
public:
    AutoEffectGroup(const char* pName, int capacity);
    void add(const JMapInfoIter& rIter);

    const char* getName() const {
        return mName;
    }

    const char* mName;
    AutoEffectInfo** mEffects;
    s32 mCapacity;
    s32 mCount;
};

namespace MR {
    namespace Effect {
        void addAutoEffectsFromGroup(const AutoEffectGroup* pGroup, EffectKeeper* pKeeper, const LiveActor* pActor);
        void addAutoEffectsFromGroup(const AutoEffectGroup* pGroup, PaneEffectKeeper* pKeeper, const LayoutActor* pActor);
        void addAutoEffectsFromGroup(const AutoEffectGroup* pGroup, MultiSceneEffectKeeper* pKeeper, const MultiSceneActor* pActor);
        AutoEffectGroup* createAutoEffectGroup(const char* pName);
    };  // namespace Effect
};  // namespace MR
