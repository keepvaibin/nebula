#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MarblePlanetElectron;
class MarblePlanetElectronShadow;
class ModelObj;

class MarblePlanet : public LiveActor {
public:
    MarblePlanet(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void startClipped();
    virtual void endClipped();
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeScaleUpCore();
    void exeBreakCore();
    void initCoreAndElectron();

    ModelObj* mCorePlanetModel;
    MarblePlanetElectron** mPlanetElectrons;
    CollisionParts* mWatermelonCollision;
    s32 mNumElectrons;
    s32 mRemainingElectrons;
};

class MarblePlanetElectron : public LiveActor {
public:
    MarblePlanetElectron(LiveActor*, const Vec3f&, const Vec3f&, const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeMove();
    void exeAttack();
    void crashElectron(HitSensor*);

    MarblePlanet* mParentPlanet;
    MarblePlanetElectronShadow* mElectronShadow;
    Vec3f mFront;
};

class MarblePlanetElectronShadow : public LiveActor {
public:
    MarblePlanetElectronShadow(LiveActor*, const Vec3f&, const char*);

    virtual void init(const TableIter&);
    virtual void calcAndSetBaseMtx();

    MarblePlanetElectron* mParentElectron;
    const Vec3f* mGroundPos;
};
