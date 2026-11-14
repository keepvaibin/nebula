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
#include "Game/System/LumaModLog.hpp"
#include <luma_modlog.h>
#include <nebula_format.h>
#include <nebula_module.h>
#include <cstdlib>
#include <cstring>

namespace {
    // The particle words of list-form effect names (a space in the name, or two digits at its end) that the resources
    // lack, each said once a session. Effects are made on the game thread and on a scene's loading thread, so the list
    // is pushed without a lock; two threads meeting one new word may both say it, which is harmless. File-scope and
    // constant-initialised (no function-local statics in the module); ::malloc, never a scene's arena, and kept until
    // the module unloads.
    struct MissingWord {
        MissingWord* mNext;
        u32 mHash;
        char mWord[1];
    };
    MissingWord* sMissingWords = nullptr;

    u32 hashOf(const char* pWord) {
        u32 hash = 2166136261u;  // FNV-1a
        for (const unsigned char* p = reinterpret_cast< const unsigned char* >(pWord); *p != 0; p++) {
            hash = (hash ^ *p) * 16777619u;
        }
        return hash;
    }

    bool saidBefore(const char* pWord, u32 hash) {
        for (const MissingWord* p = __atomic_load_n(&sMissingWords, __ATOMIC_ACQUIRE); p != nullptr; p = p->mNext) {
            if (p->mHash == hash && strcmp(p->mWord, pWord) == 0) {
                return true;
            }
        }
        return false;
    }

    // `pWord` names no particle: the game's log once; with a mod's prefix ("<id>:"), that mod's Content.MissingEffect
    // (references.md 6, code 623; one entry a word: the word's hash is its column, so two words are two entries); while
    // the harness runs, the fact `fx.particle_missing {"word", "mod"}`. Game data never does it; a mod's table can.
    void sayMissingParticle(const char* pWord) {
        const u32 hash = hashOf(pWord);
        if (saidBefore(pWord, hash)) {
            return;
        }
        const size_t length = strlen(pWord);
        MissingWord* const pNew = static_cast< MissingWord* >(::malloc(sizeof(MissingWord) + length));
        if (pNew == nullptr) {
            NEBULA_PANIC("effects: no memory to note the missing particle %s", pWord);
        }
        pNew->mHash = hash;
        memcpy(pNew->mWord, pWord, length + 1);
        MissingWord* pHead = __atomic_load_n(&sMissingWords, __ATOMIC_ACQUIRE);
        do {
            pNew->mNext = pHead;
        } while (!__atomic_compare_exchange_n(&sMissingWords, &pHead, pNew, true, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE));

        nebula_log("effects: no particle \"%s\" in the resources; its emitter is left out\n", pWord);
        const char* const pColon = strchr(pWord, ':');
        char mod[72];
        mod[0] = 0;
        if (pColon != nullptr && pColon != pWord && pColon - pWord < static_cast< ptrdiff_t >(sizeof(mod))) {
            memcpy(mod, pWord, static_cast< size_t >(pColon - pWord));
            mod[pColon - pWord] = 0;
            LumaModLogEntry entry = {};
            entry.size = sizeof(entry);
            entry.source = LUMA_MODLOG_CONTENT;
            entry.level = LUMA_MODLOG_ERROR;
            entry.code = 623;
            entry.mod = mod;
            entry.file = "";
            entry.column = hash;
            entry.names[0] = "found";
            entry.values[0] = pWord;
            entry.count = 1;
            luma::modlog::add(entry, "Content.MissingEffect");
        }
        if (luma::modlog::facts()) {
            char word[200];
            char modText[100];
            luma::modlog::jsonString(word, sizeof(word), pWord);
            luma::modlog::jsonString(modText, sizeof(modText), mod);
            char json[320];
            nebula_snprintf(json, sizeof(json), "{\"word\":%s,\"mod\":%s}", word, modText);
            luma::modlog::fact("fx.particle_missing", json);
        }
    }
}  // namespace


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
                // A word the resources lack keeps its slot (callers address emitters by their place in the list:
                // setGlobalRotation(.., idx)) with no resource, so it is never created.
                u16 index = SingleEmitter::cNoResource;
                if (!MR::Effect::isExistInResource(&index, name.c_str())) {
                    index = SingleEmitter::cNoResource;
                    if (name.c_str()[0] != 0) {  // two spaces together: an empty word, nothing to name
                        sayMissingParticle(name.c_str());
                    }
                }
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
