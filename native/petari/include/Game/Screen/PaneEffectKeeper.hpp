#pragma once

#include "Game/Util/Array.hpp"

class LayoutActor;
class LayoutManager;
class EffectSystem;
class MultiEmitter;

class PaneEffectKeeper {
public:
    PaneEffectKeeper(LayoutActor*, const LayoutManager*, int, const char*);

    void init(const LayoutActor*, const EffectSystem*);
    void add(const char*, const char*, const char*);
    MultiEmitter* createEmitter(const char*);
    void deleteEmitter(const char*);
    void forceDeleteEmitter(const char*);
    void deleteEmitterAll();
    void forceDeleteEmitterAll();
    void clear();
    MultiEmitter* getEmitter(const char*) const;
    void changeAnim();
    void registerEffect(MultiEmitter*, const char*);
    MultiEmitter* find(const char*) const;

    LayoutActor* mHost;
    const char* mName;
    MR::Vector< MR::AssignableArray< MultiEmitter* > > mEmitters;
};
