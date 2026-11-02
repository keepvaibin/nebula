#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class Coin;

class CoinGroup : public LiveActor {
public:
    CoinGroup(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();

    virtual void initCoinArray(const JMapInfoIter&) = 0;
    virtual void placementCoin() {
    }
    virtual const char* getCoinName() const {
        return "\203\122\203\103\203\223(\203\117\203\213\201\133\203\166\224\172\222\165)" /* コイン(グループ配置) */;
    }

    void killCoinAll();
    void appearCoinAll();
    void appearCoinFix();
    void appearCoinAllTimer();
    void setCoinTrans(s32, const TVec3f&);
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
