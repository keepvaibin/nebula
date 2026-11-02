#pragma once

#include "Game/NameObj/NameObj.hpp"

class AutoEffectGroupHolder;
class JPAEmitterManager;
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

    virtual ~EffectSystem();
    virtual void init(const JMapInfoIter& rIter);

    ParticleEmitter* createEmitter(u16, u8, u8);
    void forceDeleteEmitter(ParticleEmitter*) const;
    void forceDeleteSingleEmitter(SingleEmitter*) const;
    void createSingleEmitter(SingleEmitter*, MultiEmitterCallBackBase*, MultiEmitterParticleCallBack*);
    void entry(ParticleResourceHolder*, u32, u32);

    JPAEmitterManager* mEmitterManager;
    ParticleEmitterHolder* mEmitterHolder;
    ParticleDrawExecutor* mDrawExec;
    ParticleCalcExecutor* mCalcExec;
    AutoEffectGroupHolder* mGroupHolder;
    bool _20;
};

namespace MR {
    EffectSystem* getEffectSystem();
};  // namespace MR
