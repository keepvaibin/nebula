#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ModelObj;

class HipDropRock : public LiveActor {
public:
    /// @brief Creates a new `HipDropRock`.
    /// @param pName A pointer to the null-terminated name of the object.
    HipDropRock(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeBreak();
    void exeWreck();

    void initBreakModel();
    void initItem(const JMapInfoIter&);
    void appearItem();

    ModelObj* mBreakModel;
    TMtx34f mBreakModelMtx;
    s32 mItemType;
    s32 mStarPieceNum;
    bool mObjArg7;
};
