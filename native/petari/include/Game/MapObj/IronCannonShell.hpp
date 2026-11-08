#pragma once

#include "Game/Enemy/KoopaJrShipCannonShell.hpp"
#include "Game/LiveActor/LiveActor.hpp"

// A Koopa Jr. ship shell, drawn and hit-tested larger.
class IronCannonShell : public KoopaJrShipCannonShell {
public:
    IronCannonShell(const char*);

    virtual f32 getBaseScale() const;
};

class IronCannonLauncherPoint : public LiveActor {
public:
    IronCannonLauncherPoint(const char*);

    virtual void init(const TableIter& rIter);

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

    virtual void init(const TableIter& rIter);

    virtual void initModelAndConnectToScene();

    void exeRelax();
    void exeWait();
    void exeShot();

    // Muzzle point the shot effect is hosted at.
    Vec3f mMuzzlePos;
};
