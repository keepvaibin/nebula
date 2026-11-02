#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AirBubble : public LiveActor {
public:
    /// @brief Creates a new `AirBubble`.
    /// @param pName A pointer to the null-terminated name of the object.
    AirBubble(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void appearMove(const TVec3f&, s32);
    void exeWait();
    void exeMove();
    void exeBreak();
    void exeKillWait();
    bool canSpinGet() const;

    TVec3f _8C;
    u8 _98[0xC];
    TVec3f _A4;
    f32 _B0;
    u8 _B4[0xC];
    f32 mRailMoveSpeed;
    s32 mLife;
    bool mIsFollowRail;
    bool _C9;
};
