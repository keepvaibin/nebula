#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class FlashingCtrl;

class ShadowClipActor : public LiveActor {
public:
    virtual void endClipped();

    LiveActor* _8C;
};

class BenefitItemObj : public LiveActor {
public:
    /// @brief Creates a new `BenefitItemObj`.
    /// @param pName A pointer to the null-terminated name of the object.
    /// @param pModelName A pointer to the null-terminated name of the model.
    BenefitItemObj(const char* pName, const char* pModelName);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void appearGround();
    virtual void appearThrowUp();
    virtual void appearThrowUpQuestionBox();
    virtual void initModelAndEfx();
    virtual void exeCatch();
    virtual void runBck(const char*);
    virtual void runEfx(const char*);
    virtual void stopEfx(const char*);
    virtual void doRotateY();
    virtual void initEscape();
    virtual void doEscape();

    void shoot(const TVec3f&, const TVec3f&, bool);
    void exeShoot();
    void exeWait();
    void exeAppearGround();
    void exePreEscape();
    void exeEscape();
    void setFollowMtx(MtxPtr);
    void calcAndSetBaseMtxInMovement();

    u32 _8C;
    u32 _90;
    u32 _94;
    TVec3f _98;
    TVec3f _A4;
    s16 _B0;
    s16 _B2;
    s16 _B4;
    s16 _B6;
    s16 _B8;
    s16 _BA;
    TVec3f _BC;
    TVec3f _C8;
    MtxPtr mFollowMtx;
    u8 _D8;
    u8 _D9;
    u8 _DA;
    u8 _DB;
    u8 _DC;
    u8 _DD;
    u8 _DE;
    u8 _DF;
    u8 _E0;
    u8 _E1;
    u8 _E2;
    u8 _E3;
    u8 _E4;
    u8 _E5;
    u8 _E6;
    u8 _E7;
    f32 _E8;
    f32 _EC;
    TMtx34f _F0;
    const char* _120;
    FlashingCtrl* mFlashCtrl;
    ActorCameraInfo* mCameraInfo;
    u32 _12C;
    u32 _130;
    LiveActor* mHitSensorActor;
    u8 _138;
    u8 _139;
    u16 _13A;
};

namespace NrvBenefitItemObj {
    NEW_NERVE(HostTypeNrvWait, BenefitItemObj, Wait);
    NEW_NERVE(HostTypeNrvShoot, BenefitItemObj, Shoot);
    NEW_NERVE(HostTypeNrvCatch, BenefitItemObj, Catch);
    NEW_NERVE(HostTypeNrvAppearGround, BenefitItemObj, AppearGround);
    NEW_NERVE(HostTypeNrvPreEscape, BenefitItemObj, PreEscape);
    NEW_NERVE(HostTypeNrvEscape, BenefitItemObj, Escape);
};  // namespace NrvBenefitItemObj
