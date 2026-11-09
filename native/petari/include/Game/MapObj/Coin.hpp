#pragma once

#include "Game/LiveActor/FlashingCtrl.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/PartsModel.hpp"


class CoinHostInfo {
public:
    CoinHostInfo() {
        mHostActor = nullptr;
        mDeclared = 0;
        mAppeared = 0;
    }

    const NameObj* mHostActor;
    s32 mDeclared;
    s32 mAppeared;
};

class Coin : public LiveActor {
public:
    Coin(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initShadow(const TableIter&);
    void setShadowAndPoseModeFromPlacementIter(const TableIter&);
    void exeNonActive();
    void exeFix();
    void exeFixHide();
    void exeFixTimer();
    void exeMove();
    void exeControled();
    void exeSpinDrained();
    void exeHop();
    void appearFix();
    void appearControlPose();
    void appearNonActive();
    // A negative life or pickup delay takes the default.
    void appearFixTimer(s32 life, s32 cannotTime);
    void appearMove(const Vec3f& rPosition, const Vec3f& rVelocity, s32 life, s32 cannotTime);
    void appearHop(const Vec3f& rPosition, const Vec3f& rUp);
    bool requestActive();
    bool requestActiveWithGravity();
    bool requestDeactive();
    bool requestStartControl();
    bool requestEndControl();
    bool requestSpinDrain();
    bool requestShow();
    bool requestHide();
    void noticeGetCoin();
    void setLife(s32 life);
    void setHostInfo(CoinHostInfo* pInfo);
    void setCalcShadowMode();
    void setCannotTime(s32 time);
    void calcRebound();
    void attenuateVelocity();
    static bool isNeedBubble(const TableIter& rIter);

    CoinHostInfo* mHostInfo;
    FlashingCtrl* mFlashCtrl;
    PartsModel* mAirBubble;
    Vec3f mDropPosition;
    Vec3f mClippingRange;
    int mLife;
    // Steps after appearing before it can be picked up.
    s32 mCannotTime;
    bool mIsInWater;
    bool mIsCalcShadowAlways;
    bool mIsPoseFromRotation;
    bool mIsPurpleCoin;
    bool mIsNeedBubble;
};

namespace MR {
    NameObj* createDirectSetCoin(const char*);
    NameObj* createCoin(const NameObj*, const char*);
    NameObj* createDirectSetPurpleCoin(const char*);
    NameObj* createPurpleCoin(const NameObj*, const char*);
};  // namespace MR
