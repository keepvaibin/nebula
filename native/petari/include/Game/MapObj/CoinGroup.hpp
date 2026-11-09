#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class Coin;

class CoinGroup : public LiveActor {
public:
    CoinGroup(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;

    virtual void initCoinArray(const TableIter&) = 0;
    virtual void placementCoin() {
    }
    virtual const char* getCoinName() const {
        return "コイン(グループ配置)";
    }

    void killCoinAll();
    void appearCoinAll();
    void appearCoinFix();
    void appearCoinAllTimer();
    void setCoinTrans(s32, const Vec3f&);
    void exeAppear();
    void exeTryStartDemo();
    void exeDemoAppear();
    void exeKill();

    Coin** mCoinArray;
    ActorCameraInfo* mCameraInfo;
    u32 mCoinCount;
    s32 mTimeLimit;
    bool mIsPurpleCoinGroup;
};
