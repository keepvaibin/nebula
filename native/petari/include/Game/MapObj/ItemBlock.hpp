#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ItemBlock : public LiveActor {
public:
    ItemBlock(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void checkKind(const TableIter&);
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
    bool mCalcShadow;
    bool mNoCalcAnim;
};
