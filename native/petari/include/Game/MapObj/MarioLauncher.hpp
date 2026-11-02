#pragma once

#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class MarioLauncherShell;
class MarioLauncherLayout;

class MarioLauncher : public LiveActor {
public:
    enum LauncherType {
        LauncherType_Front = 0,
        LauncherType_Up = 1,
    };

    MarioLauncher(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeReady();
    void exePlayerIn();
    void exePrep();
    void exeAim();
    void exeFlight();
    void exeKill();
    void exeLand();
    void exeReject();

    bool doReady(HitSensor*);
    bool doPrep();
    bool doAim();
    bool doReject();
    bool doEject();
    bool doFlight();
    bool doLanding(s32);

    bool testHitCollision();
    bool isBreak(const Triangle*);

    void onSwitchA();
    void offSwitchA();

    MarioLauncherLayout* mLayout;
    MarioLauncherShell* mShell;
    LiveActor* mHost;
    bool mIsInAttractor;
    s32 mLauncherType;  // arg0
    TPos3f mEffectMtxSmoke;
    TPos3f mEffectMtxGravity;
    CameraTargetMtx mCameraTargetMtx;
    TPos3f mPlayerMtx;
    TPos3f mPlayerBaseMtx;
    TPos3f mLauncherBaseMtx;
};

class MarioLauncherLayout : public LayoutActor {
public:
    MarioLauncherLayout() : LayoutActor("\210\332\223\256\227\160\226\103\221\344\227\160\203\214\203\103\203\101\203\105\203\147" /* 移動用砲台用レイアウト */, true), mLayout() {
    }

    virtual void init(const JMapInfoIter& rIter);
    virtual void kill();

    void appearAt(const TVec2f& pos) {
        mLayout->appear();
        mLayout->setTrans(pos);
    }

    void initLayout() {
        initLayoutManager("Telescope", 1);
        mLayout = new SimpleLayout("\210\332\223\256\227\160\226\103\221\344\217\306\217\200" /* 移動用砲台照準 */, "Sight", 1, -1);
        mLayout->kill();
    }

    SimpleLayout* mLayout;
};

class MarioLauncherShell : public LiveActor {
public:
    MarioLauncherShell(const char* pName) : LiveActor(pName) {
        mBaseMtx.identity();
        mReboundDir.zero();
    }

    virtual void init(const JMapInfoIter& rIter);
    virtual MtxPtr getBaseMtx() const;
    virtual void calcAndSetBaseMtx();
    virtual void calcBaseMtx();

    TVec3f& getPosition() {
        return mPosition;
    }

    TPos3f mBaseMtx;
    TQuat4f mRotQuat;
    TVec3f _CC;
    TVec3f mReboundDir;
};
