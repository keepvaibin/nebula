#pragma once

#include "Game/Enemy/KoopaJrShipCannonShell.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class IronCannonShell : public KoopaJrShipCannonShell {
public:
    IronCannonShell(const char*);

    virtual ~IronCannonShell();

    virtual f32 getBaseScale() const {
        return 1.3f;
    }
    virtual s32 getLifeTime() const {
        return 300;
    }
};

class IronCannonLauncherPoint : public LiveActor {
public:
    IronCannonLauncherPoint(const char*);

    virtual ~IronCannonLauncherPoint();
    virtual void init(const JMapInfoIter& rIter);

    virtual void initModelAndConnectToScene();

    void exeWait();
    void exeShot();

    void initBullet();
    bool tryShotBullet(f32);

    CannonShellHolder* mShellHolder;
    s32 mShotInterval;  // Delay between cannon shots [ObjArg0]
    f32 mBulletSpeed;   // Speed of each bullet [ObjArg1]
};

class IronCannonLauncher : public IronCannonLauncherPoint {
public:
    IronCannonLauncher(const char*);

    virtual ~IronCannonLauncher();
    virtual void init(const JMapInfoIter& rIter);

    virtual void initModelAndConnectToScene();

    void exeRelax();
    void exeWait();
    void exeShot();

    TVec3f mEffectMtx;
};
