#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class ModelObj;
class PowerStarAppearPoint;

class PowerStar : public LiveActor {
public:
    // The btp frame and the point light row. Collected is the clear star.
    enum Color {
        Color_Gold = 0,
        Color_Blue = 1,
        Color_Green = 2,
        Color_Red = 3,
        Color_Collected = 4,
    };

    PowerStar(const char* pName);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void requestAppear();
    void setDemoAppearPos(const Vec3f& rPos);
    bool isEndAppearDemo() const;
    void offAppearDemo();
    static void setupColor(LiveActor* pActor, const NameObj* pDeclarer, s32 starId);
    static void setupColorAtResultSequence(LiveActor* pPowerStar, bool isGrandStar);
    static void requestPointLight(const LiveActor* pActor, const NameObj* pDeclarer, s32 starId);
    static void requestPointLightAtResultSequence(const LiveActor* pActor);
    static f32 getPowerStarWaitRotateSpeed();
    static s32 getBtpFrameCurrentStage(s32 starId);
    static bool isCurrentStageKoopaVs3();
    static void initShadowPowerStar(LiveActor* pActor, bool isCylinder);
    void initMapToolInfo(const TableIter&);
    void initModel();
    void initSensorGrandStar();
    void initShadow(const TableIter&);
    void initPosture();
    void endAppearDemo();
    PowerStarAppearPoint* getNearestAppearPoint(const Vec3f& rPos) const;
    LiveActor* getAppearCameraActor();
    ActorCameraInfo* getAppearCameraInfo() const;
    void requestAppearOrWait();
    void calcAppearDemoRiseTrans(Vec3f* pOutTrans, f32 height) const;
    void processWait(f32 rotateSpeed);
    void startExistSound();
    f32 getWaitRotateSpeed() const;
    const char* getModelName() const;
    const char* getGetDemoName() const;
    void exeWaitStartAppear();
    void exeAppearDemoRise();
    void exeAppearDemoMove();
    void exeAppearDemoKoopa();
    void exeWait();
    void exeWeak();
    void exeWeakNoRotate();
    void exeWeakToWait();
    void exeStageClearDemo();

    inline bool isGrandStar() const {
        return mIsGrandStar;
    }

    s32 mPowerStarId;
    bool mIsInDemo;
    Vec3f mAppearPosition;
    Vec3f mInitRotation;
    Vec3f mInitPosition;
    Pos3f mPostureMtx;
    Pos3f mBaseMtx;
    ModelObj* mPowerStarModelObj;
    bool mIsForceAppear;
    s32 mColor;
    bool mIsGrandStar;
    bool mIsRiseOnGravity;
    bool mIsNoPostureFront;
    bool mIsCameraActorPose;
    ActorCameraInfo* mCameraInfo;
    PowerStarAppearPoint* mCameraActor;
    ModelObj* mLuigiNPC;
    Mtx34f mPlayerMtx;
    bool mIsWeak;
};
