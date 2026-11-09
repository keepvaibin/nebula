#include "Game/Scene/MultiSceneEffectKeeper.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Effect/ParticleResourceHolder.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <nebula_geometry.hpp>

MultiSceneEffectKeeper::MultiSceneEffectKeeper(ModelManager* pModelManager, int extraEmitterNum, const char* pResName)
    : mEmitter(), mResName(pResName) {
    if (mResName == nullptr) {
        mResName = pModelManager->getModelResourceHolder()->getModelName();
    }

    s32 num = extraEmitterNum + MR::getParticleResourceHolder()->getAutoEffectNum(mResName);
    mEmitter.init(num);
}

void MultiSceneEffectKeeper::init(const MultiSceneActor* pActor, const EffectSystem* pSystem) {
    if (mResName == nullptr) {
        return;
    }

    MR::Effect::registerAutoEffectInfoGroup(this, pSystem, pActor, mResName);
}

void MultiSceneEffectKeeper::add(const char* pName, const Vec3f* pScale, const Vec3f* pRotation, const Vec3f* pTranslation,
                                 const char* pEmitterName) {
    MultiEmitter* pEmitter = new MultiEmitter(pName, pScale, pRotation, pTranslation, Vec3f(0.0f, 0.0f, 0.0f));
    registerEmitter(pEmitter, pEmitterName);
}

void MultiSceneEffectKeeper::add(const char* pName, MtxPtr pMtx, const char* pEmitterName) {
    MultiEmitter* pEmitter = new MultiEmitter(pName, pMtx, Vec3f(0.0f, 0.0f, 0.0f));
    registerEmitter(pEmitter, pEmitterName);
}

MultiEmitter* MultiSceneEffectKeeper::create(const char* pName, EffectSystem* pSystem) {
    MultiEmitter* pEmitter = find(pName);
    pEmitter->create(pSystem);
    return pEmitter;
}

void MultiSceneEffectKeeper::deleteAll() {
    for (auto& item : mEmitter) {
        item->deleteEmitter();
    }
}

void MultiSceneEffectKeeper::forceDeleteAll(EffectSystem* pSystem) {
    for (auto& item : mEmitter) {
        item->forceDelete(pSystem);
    }
}

void MultiSceneEffectKeeper::clear() {
    for (auto& item : mEmitter) {
        item->playCalcAndDeleteForeverEmitter();
    }
}

MultiEmitter* MultiSceneEffectKeeper::get(const char* pName) const {
    return find(pName);
}

MultiEmitter* MultiSceneEffectKeeper::find(const char* pName) const {
    if (mEmitter.size() == 0) {
        return nullptr;
    }

    const u32 hash = MR::getHashCode(pName);
    for (MultiEmitter* const* pEmitter = mEmitter.begin(); pEmitter != mEmitter.end(); pEmitter++) {
        if ((*pEmitter)->isEqualName(pName, hash)) {
            return *pEmitter;
        }
    }

    return nullptr;
}

void MultiSceneEffectKeeper::registerEmitter(MultiEmitter* pEmitter, const char* pEmitterName) {
    if (pEmitterName != nullptr) {
        pEmitter->setName(pEmitterName);
    }

    mEmitter.push_back(pEmitter);
}
