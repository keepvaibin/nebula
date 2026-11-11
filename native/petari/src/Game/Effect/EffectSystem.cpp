#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/AutoEffectGroupHolder.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/MultiEmitterCallBack.hpp"
#include "Game/Effect/MultiEmitterParticleCallBack.hpp"
#include "Game/Effect/ParticleCalcExecutor.hpp"
#include "Game/Effect/ParticleDrawExecutor.hpp"
#include "Game/Effect/ParticleEmitterHolder.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/Effect/SingleEmitter.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Particle/ParticleCore.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include "Game/Particle/EmitterManager.hpp"
#include "Game/System/LumaCore.hpp"

EffectSystem::EffectSystem(const char* pName, bool createAdaptors)
    : NameObj(pName), mEmitterManager(), mDrawExec(), mCalcExec(), mGroupHolder(new AutoEffectGroupHolder()) {
    lumacore::setClass(this, lumacore::kClassId< EffectSystem >);
    mDrawExec = new ParticleDrawExecutor(this, createAdaptors);
    mCalcExec = new ParticleCalcExecutor(this, createAdaptors);
}

void EffectSystem::init(const TableIter& /*rIter*/) {
}

ParticleEmitter* EffectSystem::createEmitter(u16 resourceId, u8 groupId, u8 resourceManagerId) {
    ParticleEmitter* pEmitter = mEmitterHolder->findAvailableParticleEmitter();
    if (pEmitter == nullptr) {
        return nullptr;
    }

    MR::Effect::createParticleEmitter(pEmitter, mEmitterManager, Vec3f(0.0f, 0.0f, 0.0f), resourceId, groupId, resourceManagerId);
    if (pEmitter->mEmitter != nullptr) {
        return pEmitter;
    }

    return nullptr;
}

void EffectSystem::forceDeleteEmitter(ParticleEmitter* pEmitter) const {
    if (pEmitter->mEmitter != nullptr) {
        SingleEmitter* pSingleEmitter = MR::Effect::getLinkSingleEmitter(pEmitter->mEmitter);

        if (pSingleEmitter != nullptr) {
            pSingleEmitter->mEmitter = nullptr;
        }

        mEmitterManager->forceDeleteEmitter(pEmitter->mEmitter);
        pEmitter->invalidate();
        pEmitter->mHolder->released(pEmitter);
    }
}

void EffectSystem::forceDeleteSingleEmitter(SingleEmitter* pSingleEmitter) const {
    ParticleEmitter* pEmitter = pSingleEmitter->mEmitter;

    if (pSingleEmitter->mEmitter != nullptr) {
        pSingleEmitter->mEmitter = nullptr;

        mEmitterManager->forceDeleteEmitter(pEmitter->mEmitter);
        pEmitter->invalidate();
        pEmitter->mHolder->released(pEmitter);
    }
}

void EffectSystem::createSingleEmitter(SingleEmitter* pSingleEmitter, MultiEmitterCallBackBase* pCallBack,
                                       MultiEmitterParticleCallBack* pParticleCallBack) {
    if (pSingleEmitter->isValid()) {
        if (!pSingleEmitter->isOneTime()) {
            return;
        }

        pSingleEmitter->unlink();
    }

    ParticleEmitter* pEmitter = createEmitter(pSingleEmitter->mResourceId, pSingleEmitter->mGroupId, 0);

    if (pEmitter != nullptr) {
        pSingleEmitter->link(pEmitter);

        if (pCallBack != nullptr) {
            pEmitter->mEmitter->mpEmtrCallBack = pCallBack;
        }

        if (pParticleCallBack != nullptr) {
            pEmitter->mEmitter->mpPtclCallBack = pParticleCallBack;
        }
    }
}

namespace MR {
    EffectSystem* getEffectSystem() {
        return getSceneObj< EffectSystem >(SceneObj_EffectSystem);
    }
};  // namespace MR

void EffectSystem::entry(ParticleResourceHolder* pResourceHolder, u32 emitterCount) {
    mEmitterManager = new EmitterManager(emitterCount, NebulaArena::current(), 9, 1);
    mEmitterManager->entryResourceManager(pResourceHolder->mResourceMgr, 0);
    startScene();
    mEmitterHolder = new ParticleEmitterHolder(this, emitterCount);
    pResourceHolder->swapTexture(MR::getScreenResTIMG(), "IndDummy");
}

void EffectSystem::startScene() {
    mEmitterManager->startScene(MR::getHashCode(MR::getCurrentStageName()));
}
