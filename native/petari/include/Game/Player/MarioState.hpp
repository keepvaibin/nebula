#pragma once

#include "Game/Player/MarioModule.hpp"

enum MarioStatus {
    MarioStatus_None,
    MarioStatus_Wall,
    MarioStatus_Damage,
    MarioStatus_Faint,
    MarioStatus_Blown,
    MarioStatus_Hang,
    MarioStatus_Swim,
    MarioStatus_Slider,
    MarioStatus_FireDamage,
    MarioStatus_FireDance,
    MarioStatus_FireRun,
    MarioStatus_Paralyze,
    MarioStatus_AbyssDamage,
    MarioStatus_Freeze,
    MarioStatus_Stun,
    MarioStatus_Crush,
    MarioStatus_Step,
    MarioStatus_Magic,
    MarioStatus_FpView,
    MarioStatus_RecoveryOrWarp,
    MarioStatus_Recovery = MarioStatus_RecoveryOrWarp,
    MarioStatus_Warp = MarioStatus_RecoveryOrWarp,
    MarioStatus_Flip,
    MarioStatus_SideStep,
    MarioStatus_Stick,
    MarioStatus_Rabbit,
    MarioStatus_Foo,
    MarioStatus_Flow,
    MarioStatus_Sukekiyo,
    MarioStatus_Bury,
    MarioStatus_Wait,
    MarioStatus_Climb,
    MarioStatus_Bump,
    MarioStatus_Skate,
    MarioStatus_FrontStep,
    MarioStatus_Teresa,
    MarioStatus_Talk,
    MarioStatus_DarkDamage,
};

enum MarioStateMsg {
    MarioStateMsg_Start,
    MarioStateMsg_Close,
    MarioStateMsg_Update,
    MarioStateMsg_Notice,
    MarioStateMsg_Keep,
};

class MarioState : public MarioModule {
public:
    MarioState(MarioActor*, u32);

    virtual void init();
    virtual bool proc(u32);
    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();
    virtual bool keep();
    virtual bool postureCtrl(MtxPtr);
    virtual void hitWall(const Vec3f&, HitSensor*);
    virtual void hitPoly(u8, const Vec3f&, HitSensor*);
    virtual bool passRing(const HitSensor*);
    virtual f32 getBlurOffset() const;
    virtual void draw3D() const;

    u32 getNoticedStatus() const;

    MarioState* mNextState;  // the next state in Mario's active list (MarioState.cpp walks it)
    u32 mStatusId;
    bool mClosing;  // close is running (no re-entry)
};
