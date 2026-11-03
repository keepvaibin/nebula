#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ItemBlock : public LiveActor {
public:
    ItemBlock(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void calcAnim();
    virtual void appear();
    virtual void kill();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void checkKind(const JMapInfoIter&);
    void initBlock();
    bool tryStartJumpPunch();
    void exeStandby();
    void exeWait();
    void exeAppearItem();
    void exeAppearItemSplash();
    void decTimer();

    s32 mKind;
    s32 mTimer;
    s32 mItemCount;
    u8 _98;
    bool mCalcShadow;
    bool mNoCalcAnim;
};
