#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class ActorCameraInfo;
class NameObjArchiveListCollector;

class SunakazeKunTrap : public ModelObj {
public:
    SunakazeKunTrap(LiveActor*, const TVec3f&);

    virtual void init(const JMapInfoIter& rIter);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeTrap();

private:
    LiveActor* mHost;
    TVec3f _94;
};

class Sandstorm : public LiveActor {
public:
    enum Type {
        Type_TwisterSan,
        Type_SunakazeKun,
        Type_TwisterSanSlim,
    };

    /// @brief Creates a new `Sandstorm`.
    /// @param pName A pointer to the null-terminated name of the object.
    Sandstorm(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    void initSandstormParam();
    void initTrapModel();
    void updateBindActorMtx();
    void endBind();
    void updateSpiral();
    bool tryStartBind(HitSensor*);
    bool tryTornadoJumpPlayer();
    bool tryNonActive();
    bool isSunakazeKun() const;

    void exeNonActive();
    void exeWait();
    void exeStorm();
    void exeStormAdjust();
    void exeStormFix();
    void exeStormEnd();

private:
    ActorCameraInfo* _8C;
    LiveActor* mBindActor;
    TVec3f _94;
    TVec3f _A0;
    s32 mType;
    f32 mSpiralRot;
    f32 mSpiralRadius;
    f32 _B8;
    f32 _BC;
    f32 _C0;
    f32 _C4;
    s32 _C8;
    f32 _CC;
    f32 mSensorHeight;
    f32 mSizeUpRate;
    f32 mSensorMin;
    s32 mStepStorm;
    s32 mStepAdjust;
    s32 mStepFix;
    s32 mStepEnd;
    f32 mBindHeight;
    SunakazeKunTrap** mTrapModel;
};
