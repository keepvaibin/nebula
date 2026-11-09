#pragma once

#include "Game/Screen/LayoutActor.hpp"

class CollectCounter;

class ChipCounter : public LayoutActor {
public:
    ChipCounter(const char*, s32);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void setCount(s32);
    void requestShow(s32 groupId, s32 count);
    void requestHide(s32 groupId);
    void requestComplete(s32 groupId);
    void requestActive();
    void requestDeactive();
    bool tryEndFrameIn();
    bool tryEndFrameOut();
    bool tryEndComplete();

    void exeHide();
    void exeFrameIn();
    void exeShow();
    void exeFrameOut();
    void exeTryDemo();
    void exeComplete();
    void exeCompleteOut();

    inline bool isHidden();
    inline bool isComplete();

    CollectCounter* mCollectCounter;
    s32 mCount;
    s32 mType;
    s32 mGroupId;
    f32 mShowRate;
};
