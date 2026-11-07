#pragma once

#include "Game/Sound/EffectSound.hpp"
#include "Game/Sound/Sound.hpp"
#include "Game/Sound/SoundGlobal.hpp"
#include "Game/Sound/SeqSource.hpp"

class SoundAudience;
struct SoundParams;

struct CategoryArrangementItem {
    struct {
        u8 mMaxActiveSe;
        u8 mMaxInactiveSe;
    };
};

struct CategoryArrangement {
    CategoryArrangementItem mItems[16];
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
    const JSUList< EffectSound >* getSeList() const {
        return &mSeList;
    }
    int getNumSe() const {
        return mSeList.getNumLinks();
    }
    void appendSe_(EffectSound* se) {
        mSeList.append(se);
    }

    SoundMove mParams;
    JSUList< EffectSound > mSeList;
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
    void setStrategyMgr(SoundStrategyMgr< EffectSound >* soundStrategyMgr);
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

    static const int NUM_CATEGORIES = 16;

    SoundAudience* mAudience;
    SeqSource* mSeqDataMgr;
    SoundStrategyMgr< EffectSound >* mStrategyMgr;
    EffectCategory mCategoryMgrs[NUM_CATEGORIES];
    SoundMove mParams;
};
