#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/MultiEmitterCallBack.hpp"
#include "Game/Effect/MultiEmitterParticleCallBack.hpp"
#include "Game/Effect/SingleEmitter.hpp"
#include "Game/Effect/SyncBckEffectInfo.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/EmitterCore.hpp"
#include <cstring>


MultiEmitter::MultiEmitter(const char* pName, const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale, const Vec3f& rOffset)
    : mEmitters(), mCallBack(), mParticleCallBack(), mSyncBck(), mAutoInfo(), mLightAffect(), mHash(), mName(), mCreateOnUnclipped(), mForceDeleteOnClipped() {
    mCallBack = new MultiEmitterCallBack(this, pTranslation, pRotation, pScale, rOffset);
    mParticleCallBack = new MultiEmitterParticleCallBack();

    allocateEmitter(pName);
}

MultiEmitter::MultiEmitter(const char* pName, MtxPtr pMtx, const Vec3f& rOffset)
    : mEmitters(), mCallBack(), mParticleCallBack(), mSyncBck(), mAutoInfo(), mLightAffect(), mHash(), mName(), mCreateOnUnclipped(), mForceDeleteOnClipped() {
    mCallBack = new MultiEmitterCallBack(this, pMtx, rOffset);
    mParticleCallBack = new MultiEmitterParticleCallBack();

    allocateEmitter(pName);
}

MultiEmitter::MultiEmitter(const char* pName, MtxPtr pMtx, const Vec3f* pScale, const Vec3f& rOffset)
    : mEmitters(), mCallBack(), mParticleCallBack(), mSyncBck(), mAutoInfo(), mLightAffect(), mHash(), mName(), mCreateOnUnclipped(), mForceDeleteOnClipped() {
    mCallBack = new MultiEmitterCallBack(this, pMtx, pScale, rOffset);
    mParticleCallBack = new MultiEmitterParticleCallBack();

    allocateEmitter(pName);
}

MultiEmitter::MultiEmitter(const char* pName)
    : mEmitters(), mCallBack(), mParticleCallBack(), mSyncBck(), mAutoInfo(), mLightAffect(), mHash(), mName(), mCreateOnUnclipped(), mForceDeleteOnClipped() {
    mCallBack = new MultiEmitterCallBack(this, Vec3f(0.0f, 0.0f, 0.0f));
    mParticleCallBack = new MultiEmitterParticleCallBack();

    allocateEmitter(pName);
}

void MultiEmitter::createEmitter() {
    create(MR::getEffectSystem());
}

void MultiEmitter::createEmitterWithCallBack(MultiEmitterCallBackBase* pCallBackBase) {
    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        MR::getEffectSystem()->createSingleEmitter(pEmitter, pCallBackBase, nullptr);
    }

    for (auto& item : mChildren) {
        item->createEmitterWithCallBack(pCallBackBase);
    }
}

void MultiEmitter::deleteEmitter() {
    for (auto& item : mEmitters) {
        item.deleteEmitter();
    }
    for (auto& item : mChildren) {
        item->deleteEmitter();
    }
}

void MultiEmitter::forceDeleteEmitter() {
    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        MR::getEffectSystem()->forceDeleteSingleEmitter(pEmitter);
    }

    for (auto& item : mChildren) {
        item->forceDeleteEmitter();
    }
}

void MultiEmitter::forceDelete(EffectSystem* pSystem) {
    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        pSystem->forceDeleteSingleEmitter(pEmitter);
    }

    for (auto& item : mChildren) {
        item->forceDelete(pSystem);
    }
}

void MultiEmitter::deleteForeverEmitter() {
    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->isValid() && !pEmitter->isOneTime()) {
            pEmitter->deleteEmitter();
        }
    }

    for (auto& item : mChildren) {
        item->deleteForeverEmitter();
    }
}

void MultiEmitter::playCalcAndDeleteForeverEmitter() {
    playCalcEmitter(-1);
    deleteForeverEmitter();
}

bool MultiEmitter::isValid() const {
    for (const SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->isValid()) {
            return true;
        }
    }

    return false;
}

bool MultiEmitter::isExistOneTimeEmitter() const {
    for (const SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->isOneTime()) {
            return true;
        }
    }

    return false;
}

void MultiEmitter::setHostSRT(const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale) {
    mCallBack->setHostSRT(pTranslation, pRotation, pScale);
}

void MultiEmitter::setHostMtx(MtxPtr pMtx) {
    mCallBack->setHostMtx(pMtx);
}

void MultiEmitter::setOffset(const Vec3f& rOffset) {
    mCallBack->mOffset.set(rOffset);
}

void MultiEmitter::setBaseScale(f32 scale) {
    mCallBack->setBaseScale(scale);
}

void MultiEmitter::setName(const char* pName) {
    // A null name is the empty name.
    if (pName == nullptr) {
        pName = "";
    }
    mHash = MR::getHashCode(pName);
    const u32 size = static_cast< u32 >(strlen(pName)) + 1;
    char* pCopy = new (NebulaArena::findFromRoot(this), 0) char[size];
    memcpy(pCopy, pName, size);
    mName = pCopy;
}

bool MultiEmitter::isEqualName(u16 hash) const {
    return static_cast< u16 >(mHash) == hash;
}

bool MultiEmitter::isEqualName(const char* pName, u32 hash) const {
    return mName != nullptr && mHash == hash && MR::isEqualString(mName, pName);
}

ParticleEmitter* MultiEmitter::getParticleEmitter(int idx) const {
    const SingleEmitter* pEmitter = &mEmitters[idx];
    if (pEmitter->isValid()) {
        return pEmitter->mEmitter;
    }

    return nullptr;
}

void MultiEmitter::create(EffectSystem* pSystem) {
    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        pSystem->createSingleEmitter(pEmitter, mCallBack, mParticleCallBack);
    }

    for (MultiEmitter** pEmitter = mChildren.begin(); pEmitter != mChildren.end(); pEmitter++) {
        (*pEmitter)->create(pSystem);
    }
}

void MultiEmitter::scanParticleEmitter(EffectSystem* pSystem) {
    for (auto& item : mEmitters) {
        item.scanParticleEmitter(pSystem);
    }
}

void MultiEmitter::forceFollowOn() {
    mCallBack->forceFollowOn();
}

void MultiEmitter::forceFollowOff() {
    mCallBack->forceFollowOff();
}

void MultiEmitter::forceScaleOn() {
    mCallBack->forceScaleOn();
}

void MultiEmitter::initSyncBck(XanimePlayer* pPlayer, const char* pName, s32 capacity, f32 startFrame) {
    mSyncBck = new SyncBckEffectInfo(pPlayer, pName, capacity, 0.0f, -1.0f, false);
    mSyncBck->mStartFrame = startFrame;
}

void MultiEmitter::onDeleteSyncBck(f32 endFrame) {
    if (mSyncBck != nullptr) {
        mSyncBck->mEndFrame = endFrame;
    }
}

void MultiEmitter::addSyncBck(const XanimePlayer* pPlayer, const char* pName) {
    if (mSyncBck != nullptr) {
        mSyncBck->addBck(pPlayer, pName);
    }
}

void MultiEmitter::setContinueBckEnd(bool continueAnimEnd) {
    if (mSyncBck != nullptr) {
        mSyncBck->mContinueAnimEnd = continueAnimEnd;
    }
}

void MultiEmitter::onCreateSyncClipping() {
    mCreateOnUnclipped = true;
}

void MultiEmitter::onForceDeleteSyncClipping() {
    mForceDeleteOnClipped = true;
}

void MultiEmitter::stopEmitterOnClipped() {
    if (mForceDeleteOnClipped) {
        forceDeleteEmitter();
    } else if (mSyncBck != nullptr) {
        stopCalcEmitter(-1);
        stopDrawParticle(-1);
    }

    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->isValid() && !pEmitter->isOneTime()) {
            pEmitter->mEmitter->mEmitter->setStatus(EmitterStatus_StopCalc);
            pEmitter->mEmitter->mEmitter->setStatus(EmitterStatus_StopDraw);
        }
    }
}

void MultiEmitter::playEmitterOffClipped() {
    if (mCreateOnUnclipped) {
        create(MR::getEffectSystem());
    } else if (mSyncBck != nullptr) {
        playCalcEmitter(-1);
        playDrawParticle(-1);
    }

    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->isValid() && !pEmitter->isOneTime()) {
            pEmitter->mEmitter->mEmitter->clearStatus(EmitterStatus_StopCalc);
            pEmitter->mEmitter->mEmitter->clearStatus(EmitterStatus_StopDraw);
        }
    }
}

void MultiEmitter::setDrawOrder(s32 index) {
    for (auto& item : mEmitters) {
        item.setGroupID(static_cast< u8 >(index));
    }
}

void MultiEmitter::addChildEmitter(MultiEmitter* pChild) {
    mChildren.push_back(pChild);
}

namespace {
    inline s16 toS16(f32 deg) {
        return (s32)(DEGREE_TO_S16 * deg);
    }
};  // namespace

void MultiEmitter::setGlobalRotationDegree(const Vec3f& rRotation, s32 idx) {
    setGlobalRotation(Vec3s(toS16(rRotation.x), toS16(rRotation.y), toS16(rRotation.z)), idx);
}

void MultiEmitter::allocateEmitter(const char* pName) {
    // A null name is the empty name.
    if (pName == nullptr) {
        pName = "";
    }
    MR::InlineVector< u16, 32 > indices;
    bool numbered = !MR::hasStringSpace(pName) && !MR::isDigitStringTail(pName, 2);
    if (numbered) {
        u16 index = 0;
        while (MR::Effect::isExistInResource(&index, pName, indices.size())) {
            indices.push_back(index);
        }
    } else {
        u32 length = strlen(pName);
        u32 start = 0;
        for (u32 i = 0; i <= length; i++) {
            if (pName[i] == ' ' || pName[i] == '\0') {
                const MR::FormattedName name("%.*s", static_cast< int >(i - start), pName + start);
                u16 index;
                MR::Effect::isExistInResource(&index, name.c_str());
                indices.push_back(index);
                start = i + 1;
            }
        }
    }

    mEmitters.init(indices.size());
    for (s32 i = 0; i < indices.size(); i++) {
        mEmitters[i].init(indices[i]);
    }

    if (numbered) {
        setName(pName);
    }
}

SingleEmitter* MultiEmitter::getValidEmitter(s32 idx) {
    SingleEmitter* pEmitter = &mEmitters[idx];
    if (pEmitter->isValid()) {
        return pEmitter;
    }

    return nullptr;
}

void MultiEmitter::createOneTimeEmitter() {
    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (pEmitter->isOneTime()) {
            MR::getEffectSystem()->createSingleEmitter(pEmitter, mCallBack, nullptr);
        }
    }
}

void MultiEmitter::createForeverEmitter() {
    for (SingleEmitter* pEmitter = mEmitters.begin(); pEmitter != mEmitters.end(); pEmitter++) {
        if (!pEmitter->isOneTime()) {
            MR::getEffectSystem()->createSingleEmitter(pEmitter, mCallBack, nullptr);
        }
    }
}

void SingleEmitter::setGroupID(u8 groupId) {
    mGroupId = groupId;
}
