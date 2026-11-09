#pragma once

#include "Game/Sound/EffectSound.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include "Game/Sound/SeqSource.hpp"

class SoundAudience;
struct SoundParams;

// A category's sound limits: how many play at once, and how many more may wait silent (0 active: no limit).
struct CategoryArrangementItem {
    u8 mMaxActiveSe;
    u8 mMaxInactiveSe;
};

const int cEffectCategoryNum = 16;

struct CategoryArrangement {
    CategoryArrangementItem mItems[cEffectCategoryNum];
};

class EffectManager;

class EffectCategory {
public:
    void managerCalc_();
    void freeDeadSe_();
    bool acceptsNewSe_(u32 priority) const;
    void sortByPriority_();
    void stop(u32 fadeTime);
    void stop();
    void stopSoundID(SoundID id);
    void managerMixOut_(const SoundMove& params, SoundActivity activity);

    EffectCategory() {
        mParams.init();
        mMaxActiveSe = 0;
        mMaxInactiveSe = 0;
    }

    SoundMove* getParams() {
        return &mParams;
    }
    int getMaxSe() const {
        return mMaxActiveSe == 0 ? 0 : mMaxActiveSe + mMaxInactiveSe;
    }
    int getMaxInactiveSe() const {
        return mMaxInactiveSe;
    }
    int getMaxActiveSe() const {
        return mMaxActiveSe;
    }
    void setMaxActiveSe(int num) {
        mMaxActiveSe = num;
    }
    void setMaxInactiveSe(int num) {
        mMaxInactiveSe = num;
    }
    const NebulaList< EffectSound >* getSeList() const {
        return &mSeList;
    }
    int getNumSe() const {
        return mSeList.getNum();
    }
    void appendSe_(EffectSound* se) {
        mSeList.append(se);
    }

    SoundMove mParams;
    NebulaList< EffectSound > mSeList;
    int mMaxInactiveSe;
    int mMaxActiveSe;
};

class EffectManager : public SoundGlobal< EffectManager >, public SoundActivityOwner {
public:
    EffectManager(bool setInstance);
    void setCategoryArrangement(const CategoryArrangement& arrangement);
    void stop();
    void stop(u32 fadeTime);
    void stopSoundID(SoundID id);
    void initParams();
    void setAudience(SoundAudience* audience);
    // The game's per-frame step for each playing effect (its effect send and low-pass).
    void setSeCalc(void (*pCalc)(EffectSound*));
    void setSeqDataMgr(SeqSource* seqDataMgr);
    EffectSound* newSe_(int category, u32 priority);
    void calc();
    void mixOut();
    bool startSound(SoundID id, SoundHandle* handle, const Vec3f* posPtr);
    int getNumActiveSe() const;

    EffectCategory* getCategory(int categoryIndex) {
        return &mCategoryMgrs[categoryIndex];
    }
    SoundAudience* getAudience() {
        return mAudience;
    }
    SeqSource* getSeqDataMgr() {
        return mSeqDataMgr;
    }
    SoundMove* getParams() {
        return &mParams;
    }

    bool isActive() const {
        return getNumActiveSe() > 0;
    }

    SoundAudience* mAudience;
    SeqSource* mSeqDataMgr;
    void (*mSeCalc)(EffectSound*);
    EffectCategory mCategoryMgrs[cEffectCategoryNum];
    SoundMove mParams;
};
