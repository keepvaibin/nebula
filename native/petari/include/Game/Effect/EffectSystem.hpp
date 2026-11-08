#pragma once

#include "Game/NameObj/NameObj.hpp"

class AutoEffectGroupHolder;
class EmitterManager;
class MultiEmitterCallBackBase;
class MultiEmitterParticleCallBack;
class ParticleCalcExecutor;
class ParticleDrawExecutor;
class ParticleEmitter;
class ParticleEmitterHolder;
class ParticleResourceHolder;
class SingleEmitter;

class EffectSystem : public NameObj {
public:
    EffectSystem(const char*, bool);

    virtual void init(const TableIter& rIter);

    ParticleEmitter* createEmitter(u16, u8, u8);
    void forceDeleteEmitter(ParticleEmitter*) const;
    void forceDeleteSingleEmitter(SingleEmitter*) const;
    void createSingleEmitter(SingleEmitter*, MultiEmitterCallBackBase*, MultiEmitterParticleCallBack*);
    void entry(ParticleResourceHolder*, u32 emitterCount);
    // The emitters' seeds start over from the current stage, as the scene's random does.
    void startScene();

    EmitterManager* mEmitterManager;
    ParticleEmitterHolder* mEmitterHolder;
    ParticleDrawExecutor* mDrawExec;
    ParticleCalcExecutor* mCalcExec;
    AutoEffectGroupHolder* mGroupHolder;
};

namespace MR {
    EffectSystem* getEffectSystem();
};  // namespace MR
