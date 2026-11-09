#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ModelObj;

class HipDropRock : public LiveActor {
public:
    HipDropRock(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeBreak();
    void exeWreck();

    void initBreakModel();
    void initItem(const TableIter&);
    void appearItem();

    ModelObj* mBreakModel;
    Mtx34f mBreakModelMtx;
    s32 mItemType;
    s32 mStarPieceNum;
    bool mObjArg7;
};
