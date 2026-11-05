#pragma once

#include "Game/System/NebulaAtomic.hpp"

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class ModelObj;
class NameObjArchiveListCollector;
class PowerStarAppearPoint;

class PowerStar : public LiveActor {
public:
    PowerStar(const char*);

    virtual ~PowerStar();
    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void requestAppear();
    void setDemoAppearPos(const Vec3f&);
    bool isEndAppearDemo() const;
    void offAppearDemo();
    static void setupColor(LiveActor*, const NameObj*, s32);
    static void setupColorAtResultSequence(LiveActor*, bool);
    static void requestPointLight(const LiveActor*, const NameObj*, s32);
    static void requestPointLightAtResultSequence(const LiveActor*);
    static f32 getPowerStarWaitRotateSpeed();
    static s32 getBtpFrameCurrentStage(s32);
    static bool isCurrentStageKoopaVs3();
    static void initShadowPowerStar(LiveActor*, bool);
    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);
    void initMapToolInfo(const JMapInfoIter&);
    void initModel();
    void initSensorGrandStar();
    void initShadow(const JMapInfoIter&);
    void initPosture();
    void endAppearDemo();
    PowerStarAppearPoint* getNearestAppearPoint(const Vec3f&) const;
    LiveActor* getAppearCameraActor();
    ActorCameraInfo* getAppearCameraInfo() const;
    void requestAppearOrWait();
    void calcAppearDemoRiseTrans(Vec3f*, f32) const;
    void processWait(f32);
    void exeWaitStartAppear();
    void exeAppearDemoRise();
    void exeAppearDemoMove();
    void exeAppearDemoKoopa();
    void exeWait();
    void exeWeak();
    void exeWeakNoRotate();
    void exeWeakToWait();
    void exeStageClearDemo();

    inline bool isGrandStar() {
        return mIsGrandStar;
    }

    s32 mPowerStarId;
    bool mIsInDemo;
    Vec3f mAppearPosition;
    Vec3f mInitRotation;
    Vec3f mInitPosition;
    Pos3f _B8;
    Pos3f mBaseMtx;
    ModelObj* mPowerStarModelObj;
    u8 _11C;
    u8 _11D;
    u8 _11E;
    u8 _11F;

    // this isn't what they wrote but oh well
    union {
        vs32 mColorFrame_v;
        s32 mColorFrame;
    };

    NebulaAtomic< bool > mIsGrandStar;
    bool _125;
    bool _126;
    bool _127;
    ActorCameraInfo* mCameraInfo;
    PowerStarAppearPoint* mCameraActor;
    ModelObj* mLuigiNPC;
    Mtx34f _134;
    bool _164;
};
