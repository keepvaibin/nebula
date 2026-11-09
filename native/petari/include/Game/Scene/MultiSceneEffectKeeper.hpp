#pragma once

#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class EffectSystem;
class ModelManager;
class MultiEmitter;
class MultiSceneActor;

class MultiSceneEffectKeeper {
public:
    MultiSceneEffectKeeper(ModelManager*, int extraEmitterNum, const char* pResName);

    void init(const MultiSceneActor*, const EffectSystem*);
    void add(const char*, const Vec3f*, const Vec3f*, const Vec3f*, const char*);
    void add(const char*, MtxPtr, const char*);
    MultiEmitter* create(const char*, EffectSystem*);
    void deleteAll();
    void forceDeleteAll(EffectSystem*);
    void clear();
    MultiEmitter* get(const char*) const;
    MultiEmitter* find(const char*) const;
    void registerEmitter(MultiEmitter*, const char*);

    MR::Vector< MR::AssignableArray< MultiEmitter* > > mEmitter;
    const char* mResName;
};
