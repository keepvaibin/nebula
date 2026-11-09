#pragma once

#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include <nebula_geometry.hpp>

class MarioLauncherShell;
class MarioLauncherLayout;

class MarioLauncher : public LiveActor {
public:
    enum LauncherType {
        LauncherType_Front = 0,
        LauncherType_Up = 1,
    };

    MarioLauncher(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    Pos3f mEffectMtxSmoke;
    Pos3f mEffectMtxGravity;
    CameraTargetMtx mCameraTargetMtx;
    Pos3f mPlayerMtx;
    Pos3f mPlayerBaseMtx;
    Pos3f mLauncherBaseMtx;
};

class MarioLauncherLayout : public LayoutActor {
public:
    MarioLauncherLayout() : LayoutActor("移動用砲台用レイアウト", true), mLayout() {
    }

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;

    void appearAt(const Vec2f& pos) {
        mLayout->appear();
        mLayout->setTrans(pos);
    }

    void initLayout() {
        initLayoutManager("Telescope", 1);
        mLayout = new SimpleLayout("移動用砲台照準", "Sight", 1, -1);
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

    virtual void init(const TableIter& rIter) override;
    virtual MtxPtr getBaseMtx() const override;
    virtual void calcAndSetBaseMtx() override;
    virtual void calcBaseMtx();

    Vec3f& getPosition() {
        return mPosition;
    }

    Pos3f mBaseMtx;
    Quat4f mRotQuat;
    Vec3f mReboundDir;
};
