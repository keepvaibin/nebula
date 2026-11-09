#pragma once

#include "Game/NPC/NPCActor.hpp"

class ActorCameraInfo;
class BenefitItemObj;
class FullnessMeter;
class SpinDriverShootPath;

class TicoFat : public NPCActor {
public:
    TicoFat(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual void shootStarPiece();
    virtual void receiveStarPiece(s32);
    virtual void startEat();
    virtual bool enableAppear();
    virtual void initStarPieceSaveData(const TableIter&);
    virtual void addStarPieceSaveData(s32);
    virtual void disappear(bool);
    virtual void appearInformation() const;


    void setCameraParam();
    bool eventFunc(u32);
    f32 calcScale();
    void setScale(f32);
    void setMessage(s32);
    bool tryMetamorphosis();
    void emitScreenEffect();
    void updateScreenEffect();
    s32 getDanceSeTranspose() const;
    void startAbsorbSound();
    void updateGuidance();
    void updatePointing();
    void updateGoods();
    void exeReaction();
    void exePrep();
    void exeWait();
    void exePoint();
    void exeEat();
    void exeChem();
    void exeTest();
    void exeFullness();
    void exeMeta();
    void exeDemo();
    void exeFly();
    void exeWipeOut();
    void exeWipeIn();
    void exeInfo();
    void exeAfter();

    ActorCameraInfo* mCameraInfo;
    BenefitItemObj* mItem;
    FullnessMeter* mMeter;
    SpinDriverShootPath* mShootPath;
    TalkMessageCtrl* mHungryTalkCtrl;
    bool mIsCameraOn;
    bool mStartEat;
    s32 mNearStep;
    s32 mPointingStep;
    Pos3f mScreenEffectMtx;
    Pos3f mScreenLightMtx;
    s32 mItemType;
    s32 mStarPieceLeft;
    s32 mStarPieceNeed;
    s32 mCurrentFed;
    s32 mTicoSeedId;
    u32 mEatInterval;
    s32 mGuidanceId;
    f32 mFlyCoord;
    f32 mFlySpeed;
    s32 mEatSoundStep;
    s32 mAbsorbSoundStep;
};
