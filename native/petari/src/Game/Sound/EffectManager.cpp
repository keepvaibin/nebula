#include "Game/Sound/EffectManager.hpp"
#include "Game/Sound/SoundHandles.hpp"
#include "Game/Sound/SoundMemory.hpp"
#include "Game/Sound/StdSoundInfo.hpp"
#include "Game/System/NebulaArena.hpp"

void EffectCategory::managerCalc_() {
    mParams.calc();

    for (NebulaListLink< EffectSound >* i = mSeList.getFirst(); i != mSeList.getEnd(); i = i->getNext()) {
        i->getObject()->categoryCalc_();
    }

    sortByPriority_();
}

void EffectCategory::freeDeadSe_() {
    NebulaListLink< EffectSound >* i = mSeList.getFirst();
    while (i != mSeList.getEnd()) {
        NebulaListLink< EffectSound >* link_next = i->getNext();
        if (i->getObject()->isDead()) {
            mSeList.remove(i);
            delete i->getObject();
        }
        i = link_next;
    }
}

bool EffectCategory::acceptsNewSe_(u32 priority) const {
    int maxSe = getMaxSe();
    if (maxSe <= 0) {
        return true;
    }

    int playingNum = 0;
    for (NebulaListLink< EffectSound >* i = mSeList.getFirst(); i != mSeList.getEnd(); i = i->getNext()) {
        if (priority < i->getObject()->getProperPriority_()) {
            return true;
        }
        if (!i->getObject()->isStopping()) {
            playingNum++;
        }
    }

    return playingNum < maxSe;
}

// Stable, ascending by category priority. Linear when the order hasn't changed since the last frame.
void EffectCategory::sortByPriority_() {
    NebulaListLink< EffectSound >* pLink = mSeList.getFirst();
    if (pLink == mSeList.getEnd()) {
        return;
    }

    for (pLink = pLink->getNext(); pLink != mSeList.getEnd();) {
        NebulaListLink< EffectSound >* const pNext = pLink->getNext();
        const u32 priority = pLink->getObject()->getPriority_();
        NebulaListLink< EffectSound >* pPos = pLink->getPrev();

        if (priority < pPos->getObject()->getPriority_()) {
            do {
                pPos = pPos->getPrev();
            } while (pPos != nullptr && priority < pPos->getObject()->getPriority_());

            mSeList.insert(pPos == nullptr ? mSeList.getFirst() : pPos->getNext(), pLink);
        }

        pLink = pNext;
    }
}

void EffectCategory::stop(u32 fadeTime) {
    for (NebulaListLink< EffectSound >* i = mSeList.getFirst(); i != mSeList.getEnd(); i = i->getNext()) {
        i->getObject()->stop(fadeTime);
    }
}

void EffectCategory::stop() {
    for (NebulaListLink< EffectSound >* i = mSeList.getFirst(); i != mSeList.getEnd(); i = i->getNext()) {
        i->getObject()->stop();
    }
}

void EffectCategory::stopSoundID(SoundID id) {
    if (!id.isAnonymous()) {
        for (NebulaListLink< EffectSound >* i = mSeList.getFirst(); i != mSeList.getEnd(); i = i->getNext()) {
            if ((u32)i->getObject()->getID() == (u32)id) {
                i->getObject()->stop();
            }
        }
    }
}

void EffectCategory::managerMixOut_(const SoundMove& params, SoundActivity activity) {
    SoundParams mixParams;
    mixParams.combine(params.mParams, mParams.mParams);
    NebulaListLink< EffectSound >* i = mSeList.getFirst();

    // The leading sounds that aren't far away play, up to the active limit (0: no limit); the rest are muted.
    const bool isLimited = getMaxActiveSe() > 0;
    int activeLeft = getMaxActiveSe();
    while (i != mSeList.getEnd() && (!isLimited || activeLeft > 0) && !i->getObject()->isFarAway()) {
        i->getObject()->categoryMixOut_(true, mixParams, activity);
        i = i->getNext();
        activeLeft--;
    }
    for (; i != mSeList.getEnd(); i = i->getNext()) {
        i->getObject()->categoryMixOut_(false, mixParams, activity);
    }
}

EffectManager::EffectManager(bool setInstance) : SoundGlobal< EffectManager >(setInstance) {
    mAudience = nullptr;
    mSeqDataMgr = nullptr;
    mSeCalc = nullptr;
    mParams.init();
    mSoundActivity.init();
}

void EffectManager::setCategoryArrangement(const CategoryArrangement& arrangement) {
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].setMaxActiveSe(arrangement.mItems[i].mMaxActiveSe);
        mCategoryMgrs[i].setMaxInactiveSe(arrangement.mItems[i].mMaxInactiveSe);
    }
}

void EffectManager::stop() {
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].stop();
    }
}

void EffectManager::stop(u32 fadeTime) {
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].stop(fadeTime);
    }
}

void EffectManager::stopSoundID(SoundID id) {
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].stopSoundID(id);
    }
}

void EffectManager::initParams() {
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].getParams()->init();
    }
}

void EffectManager::setAudience(SoundAudience* audience) {
    mAudience = audience;
}

void EffectManager::setSeCalc(void (*pCalc)(EffectSound*)) {
    mSeCalc = pCalc;
}

void EffectManager::setSeqDataMgr(SeqSource* seqDataMgr) {
    mSeqDataMgr = seqDataMgr;
}

EffectSound* EffectManager::newSe_(int category, u32 priority) {
    if (category < 0) {
        category = 0;
    }

    EffectCategory* categoryMgr = &mCategoryMgrs[category];
    if (!categoryMgr->acceptsNewSe_(priority)) {
        return nullptr;
    }

    EffectSound* se = new (SoundMemory::arena(), 0) EffectSound(this, priority);
    if (se == nullptr) {
        return nullptr;
    }

    categoryMgr->appendSe_(se);
    return se;
}

void EffectManager::calc() {
    mParams.calc();
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].managerCalc_();
    }
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].freeDeadSe_();
    }
}

void EffectManager::mixOut() {
    for (int i = 0; i < cEffectCategoryNum; i++) {
        mCategoryMgrs[i].managerMixOut_(mParams, mSoundActivity);
    }
}

int EffectManager::getNumActiveSe() const {
    int num = 0;
    for (int i = 0; i < cEffectCategoryNum; i++) {
        num += mCategoryMgrs[i].getNumSe();
    }
    return num;
}

bool EffectManager::startSound(SoundID id, SoundHandle* handle, const Vec3f* posPtr) {
    if (handle != nullptr && handle->isSoundAttached()) {
        (*handle)->stop();
    }

    const StdSoundInfo* info = StdSoundInfo::getInstance();
    EffectSound* se = newSe_(id.getGroupID(), info->getPriority(id));
    if (se == nullptr) {
        return false;
    }

    SoundAudience* audiencePtr = mAudience;
    se->startID_(id, posPtr, audiencePtr);

    info->applySound(id, se);

    if (handle != nullptr) {
        se->attachHandle(handle);
    }

    return true;
}
