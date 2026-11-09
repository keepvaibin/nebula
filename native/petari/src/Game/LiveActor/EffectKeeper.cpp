#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/EffectKeeper.hpp"
#include "Game/Effect/AutoEffectInfo.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Effect/SyncBckEffectChecker.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_module.h>
#include <algorithm>

namespace {
    const char* cAttributeEffectTag = "Attr";
};  // namespace

namespace {
    const char* getMultiEmitterName(const MultiEmitter* pEmitter) {
        return pEmitter->mAutoInfo->getName();
    }

    bool makeAttibuteEffectBaseName(MR::FormattedName& rDst, const char* pSrc) {
        const char* tag = strstr(pSrc, cAttributeEffectTag);

        if (tag == nullptr) {
            return false;
        }

        rDst.format("%.*s", static_cast< int >(tag - pSrc), pSrc);

        return true;
    }
};  // namespace

EffectKeeper::EffectKeeper(const char* pName, ResourceHolder* pResourceHolder, int extraEmitterNum, const char* pResourceName)
    : mName(pName), mResourceHolder(pResourceHolder), mResourceName(pResourceName), mEmitters(), mSyncBckChecker(), mBinder(), mFloorCode(),
      mEffectFloorCode(), mFloorCodeSteps(), mHasAttributeEffect(), mNoEffect() {
    if (mResourceName == nullptr && mResourceHolder != nullptr) {
        mResourceName = mResourceHolder->getModelName();
    }

    s32 autoNum = MR::Effect::getAutoEffectNum(mResourceName);
    // A mod's class whose model has no effects of its own has those of the class it is made like.
    if (autoNum == 0 && pName != nullptr) {
        const char* pLike = NebulaMods::objectLike(pName);
        if (pLike != pName) {
            const s32 likeNum = MR::Effect::getAutoEffectNum(pLike);
            if (likeNum != 0) {
                mResourceName = pLike;
                autoNum = likeNum;
            }
        }
    }

    const s32 capacity = autoNum + extraEmitterNum;
    mEmitters.init(capacity);
    mNames.reset(capacity);
}

void EffectKeeper::init(LiveActor* pActor) {
    if (mResourceName != nullptr) {
        MR::Effect::registerAutoEffectInfoGroup(this, pActor, mResourceName);
    }
}

namespace {
    s32 getFloorCodeBelow(MtxPtr pMtx, f32 distance) {
        Vec3f direction;
        Vec3f position;
        Vec3f intersection;
        Pos3f mtx;
        mtx.set(pMtx);
        mtx.getYDir(direction);
        direction.negate();
        MR::normalize(&direction);
        mtx.getTrans(position);
        position.add(-direction * distance);

        Triangle triangle;
        if (!MR::getFirstPolyOnLineToMap(&intersection, &triangle, position, direction)) {
            return -1;
        }

        return MR::getFloorCodeIndex(triangle.getAttributes());
    }
};  // namespace

void EffectKeeper::initAfterPlacementForAttributeEffect(MtxPtr pMtx) {
    if (mBinder != nullptr) {
        return;
    }

    checkExistenceAttributeEffect();
    if (mHasAttributeEffect && pMtx != nullptr) {
        s32 code = ::getFloorCodeBelow(pMtx, 100.0f);
        if (code >= 0) {
            mFloorCode = code;
            mEffectFloorCode = code;
        }
    }
}

void EffectKeeper::setBinder(const Binder* pBinder) {
    mBinder = pBinder;

    if (mBinder != nullptr) {
        checkExistenceAttributeEffect();
    }
}

void EffectKeeper::addEffect(const char* pEffectName, LiveActor* pActor) {
    MR::Effect::addAutoEffect(this, pActor, MR::Effect::createAutoEffect(pEffectName));
}

void EffectKeeper::registerEffect(const char* pName, const Vec3f* pTranslation, const Vec3f* pRotation, const Vec3f* pScale,
                                  const char* pUniqueName) {
    registMultiEmitter(new MultiEmitter(pName, pTranslation, pRotation, pScale, Vec3f(0.0f, 0.0f, 0.0f)), pName, pUniqueName);
}

void EffectKeeper::registerEffect(const char* pName, MtxPtr pMtx, const char* pUniqueName) {
    registMultiEmitter(new MultiEmitter(pName, pMtx, Vec3f(0.0f, 0.0f, 0.0f)), pName, pUniqueName);
}

void EffectKeeper::registerEffect(const char* pName, MtxPtr pMtx, const Vec3f* pScale, const char* pUniqueName) {
    registMultiEmitter(new MultiEmitter(pName, pMtx, pScale, Vec3f(0.0f, 0.0f, 0.0f)), pName, pUniqueName);
}

void EffectKeeper::registerEffectWithoutSRT(const char* pName, const char* pUniqueName) {
    registMultiEmitter(new MultiEmitter(pName), pName, pUniqueName);
}

void EffectKeeper::registerSyncBckEffect(XanimePlayer* pPlayer, const char* pEffectName, const char* pBckName, s32 capacity, f32 startFrame,
                                         f32 endFrame) {
    if (mSyncBckChecker == nullptr) {
        mSyncBckChecker = new SyncBckEffectChecker(pPlayer);
    }

    MultiEmitter* emitter = getEmitter(pEffectName);
    if (emitter == nullptr) {
        return;
    }

    emitter->initSyncBck(pPlayer, pBckName, capacity, startFrame);
    emitter->onDeleteSyncBck(endFrame);
}

MultiEmitter* EffectKeeper::createEmitter(const char* pEffectName) {
    if (LiveActor::sClipScope == LiveActor::ClipScope_Run) {
        return getEmitterOrNone(pEffectName);  // the actor is seen throughout: its effects stay as they are
    }

    MultiEmitter* emitter = getEmitterOrNone(pEffectName);

    emitter->createEmitter();

    return emitter;
}

void EffectKeeper::deleteEmitter(const char* pEffectName) {
    if (LiveActor::sClipScope == LiveActor::ClipScope_Run) {
        return;
    }

    if (MultiEmitter* pEmitter = getEmitter(pEffectName)) {
        pEmitter->deleteEmitter();
    }
}

void EffectKeeper::forceDeleteEmitter(const char* pEffectName) {
    if (LiveActor::sClipScope == LiveActor::ClipScope_Run) {
        return;
    }

    if (MultiEmitter* pEmitter = getEmitter(pEffectName)) {
        pEmitter->forceDeleteEmitter();
    }
}

void EffectKeeper::deleteEmitterAll() {
    if (LiveActor::sClipScope == LiveActor::ClipScope_Run) {
        return;
    }

    for (auto& item : mEmitters) {
        item->deleteEmitter();
    }
}

void EffectKeeper::forceDeleteEmitterAll() {
    if (LiveActor::sClipScope == LiveActor::ClipScope_Run) {
        return;
    }

    for (auto& item : mEmitters) {
        item->forceDeleteEmitter();
    }
}

MultiEmitter* EffectKeeper::getEmitter(const char* pEffectName) const {
    MR::FormattedName buf;

    if (mHasAttributeEffect && !MR::isEqualSubString(pEffectName, ::cAttributeEffectTag) && isTypeAttributeEffect(pEffectName)) {
        const char* attributeName = MR::Effect::getEffectAttributeName(mFloorCode);
        buf.format("%s%s%s", pEffectName, ::cAttributeEffectTag, attributeName);

        if (!isRegisteredEmitter(buf.c_str())) {
            buf.format("%s%s%s", pEffectName, ::cAttributeEffectTag, "Default");
        }

        pEffectName = buf.c_str();
    }

    const u32 index = mNames.find(pEffectName);
    return index == MR::NameIndex::cNoValue ? nullptr : mEmitters[index];
}

MultiEmitter* EffectKeeper::getEmitterOrNone(const char* pEffectName) const {
    MultiEmitter* pEmitter = getEmitter(pEffectName);
    if (pEmitter != nullptr) {
        return pEmitter;
    }

    // What an effect the actor has none of is (nothing shows, nothing is deleted). It lives as long as the keeper: made in
    // the keeper's arena, whichever is current when the code first names the effect.
    if (mNoEffect == nullptr) {
        NebulaArena* const pArena = NebulaArena::findFromRoot(const_cast< EffectKeeper* >(this));
        const NebulaArena::CurrentScope scope(pArena != nullptr ? pArena : NebulaArena::current());
        mNoEffect = new MultiEmitter("");
    }

    return mNoEffect;
}

MultiEmitter* EffectKeeper::getEmitter(s32 index) const {
    return mEmitters[index];
}

void EffectKeeper::changeEffectName(const char* pEffectName, const char* pNewName) {
    const bool renamed = mNames.rename(pEffectName, pNewName);
    if (!renamed) {
        nebula_log("effect %s of %s: not renamed to %s (absent, or the name is held)\n", pEffectName, mName != nullptr ? mName : "", pNewName);
    }
}

void EffectKeeper::update() {
    updateSyncBckEffect();
    updateAttributeEffect();
}

void EffectKeeper::updateSyncBckEffect() {
    if (mSyncBckChecker == nullptr) {
        return;
    }

    mSyncBckChecker->updateBefore();

    for (MultiEmitter* const* it = mEmitters.begin(); it != mEmitters.end(); it++) {
        syncEffectBck(*it);
    }

    mSyncBckChecker->updateAfter();
}

void EffectKeeper::syncEffectBck(MultiEmitter* pEmitter) {
    SyncBckEffectInfo* pInfo = pEmitter->mSyncBck;

    if (pInfo == 0) {
        return;
    }

    // Forever emitters run while a registered animation plays; one-time emitters start at its frame.
    const bool isForeverDue = mSyncBckChecker->isCreate(pInfo, false);
    const bool isOneTimeDue = isForeverDue && mSyncBckChecker->isCreate(pInfo, true);

    if (isOneTimeDue || isForeverDue) {
        MultiEmitter* emitter = pEmitter;
        if (MR::isEqualSubString(::getMultiEmitterName(pEmitter), ::cAttributeEffectTag)) {
            MR::FormattedName buf;
            ::makeAttibuteEffectBaseName(buf, ::getMultiEmitterName(pEmitter));

            emitter = getEmitter(buf.c_str());
        }

        if (isOneTimeDue) {
            emitter->createOneTimeEmitter();
        }

        if (isForeverDue) {
            emitter->createForeverEmitter();
        }
    }

    if (mSyncBckChecker->isDelete(pInfo)) {
        pEmitter->deleteEmitter();
    }
}

void EffectKeeper::updateAttributeEffect() {
    if (!mHasAttributeEffect) {
        return;
    }

    // The forever effects follow the ground's code once it has held cFloorCodeHoldSteps steps: a seam between two floors,
    // or a ground ray that alternates between them, keeps one effect instead of re-making it every step.
    const s32 cFloorCodeHoldSteps = 4;
    if (mFloorCode == mEffectFloorCode) {
        mFloorCodeSteps = 0;
    } else if (++mFloorCodeSteps >= cFloorCodeHoldSteps) {
        mEffectFloorCode = mFloorCode;
        mFloorCodeSteps = 0;
        for (MultiEmitter* const* it = mEmitters.begin(); it != mEmitters.end(); it++) {
            if (!(*it)->isValid()) {
                continue;
            }

            if ((*it)->isExistOneTimeEmitter()) {
                continue;
            }

            if (!MR::isEqualSubString(::getMultiEmitterName(*it), ::cAttributeEffectTag)) {
                continue;
            }

            MR::FormattedName buf;
            ::makeAttibuteEffectBaseName(buf, ::getMultiEmitterName(*it));

            // Two codes with one effect (both the default, say): the running one stays.
            MultiEmitter* pNext = getEmitter(buf.c_str());
            if (pNext == *it) {
                continue;
            }

            (*it)->deleteForeverEmitter();
            pNext->createEmitter();
        }
    }

    updateFloorCode();
}

void EffectKeeper::stopEmitterOnClipped() {
    for (auto& item : mEmitters) {
        item->stopEmitterOnClipped();
    }
}

void EffectKeeper::playEmitterOffClipped() {
    for (auto& item : mEmitters) {
        item->playEmitterOffClipped();
    }
}

void EffectKeeper::clear() {
    for (auto& item : mEmitters) {
        item->playCalcAndDeleteForeverEmitter();
    }
}

void EffectKeeper::changeBck() {
    if (mSyncBckChecker != nullptr) {
        mSyncBckChecker->reset();
    }
}

void EffectKeeper::onDraw() {
    for (auto& item : mEmitters) {
        item->playDrawParticle(-1);
    }
}

void EffectKeeper::offDraw() {
    for (auto& item : mEmitters) {
        item->stopDrawParticle(-1);
    }
}

bool EffectKeeper::isRegisteredEmitter(const char* pEffectName) const {
    return mNames.find(pEffectName) != MR::NameIndex::cNoValue;
}

void EffectKeeper::checkExistenceAttributeEffect() {
    for (MultiEmitter* const* it = mEmitters.begin(); it != mEmitters.end(); it++) {
        if (MR::isEqualSubString(::getMultiEmitterName(*it), ::cAttributeEffectTag)) {
            mHasAttributeEffect = true;
            break;
        }
    }
}

void EffectKeeper::registMultiEmitter(MultiEmitter* pEmitter, const char* pName, const char* pUniqueName) {
    // A later emitter of a name already held stays in mEmitters (it is updated and drawn) but isn't found by name.
    mNames.add(pUniqueName != nullptr ? pUniqueName : pName, mEmitters.size());
    mEmitters.push_back(pEmitter);
}

namespace {
    bool isBinded(const Binder* pBinder) {
        if (pBinder == nullptr) {
            return false;
        }

        return pBinder->isBindedGround() || pBinder->isBindedWall() || pBinder->isBindedRoof();
    }
};  // namespace

void EffectKeeper::updateFloorCode() {
    if (mBinder == nullptr) {
        return;
    }

    if (::isBinded(mBinder)) {
        updateFloorCode(&mBinder->mGroundInfo.mParentTriangle);
    }
}

void EffectKeeper::updateFloorCode(const Triangle* pTriangle) {
    if (pTriangle == nullptr) {
        return;
    }

    mFloorCode = MR::getFloorCodeIndex(pTriangle->getAttributes());
}

bool EffectKeeper::isTypeAttributeEffect(const char* pEffectName) const {
    MR::FormattedName buf("%s%s", pEffectName, ::cAttributeEffectTag);

    for (MultiEmitter* const* it = mEmitters.begin(); it != mEmitters.end(); it++) {
        if ((*it)->mAutoInfo == nullptr) {
            continue;
        }

        if (MR::isEqualSubString((*it)->mAutoInfo->getName(), buf.c_str())) {
            return true;
        }
    }

    return false;
}
