#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class RunawayRabbit;
class RunawayTico;

class RunawayRabbitCollect : public LiveActor {
public:
    /// @brief Creates a new `RunawayRabbitCollect`.
    /// @param pName A pointer to the null-terminated name of the object.
    RunawayRabbitCollect(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();

    s32 calcCompleteRabbitCount() const;
    void linkMsgCtrl();
    void noticeAppearRabbit(RunawayRabbit* pRabbit);
    void noticeCaughtRabbit(RunawayRabbit* pRabbit);
    void appearTico(RunawayTico* pTico, const TVec3f& rPos);

    void exeWait();
    void exeActive();

private:
    ActorCameraInfo* mCameraInfo;
    RunawayRabbit** mRabbit;
    s32 mRabbitNum;
    RunawayTico** mTico;
    s32 mTicoNum;
    u32 mLinkedRabbitCount;
    s32 mCaughtRabbitCount;
    s32 mCompleteRabbitCount;
    s32 mBgmState;
    bool mIsAppearTico[3];
};
