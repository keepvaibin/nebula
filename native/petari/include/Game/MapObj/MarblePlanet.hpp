#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MarblePlanetElectron;
class MarblePlanetElectronShadow;
class ModelObj;

class MarblePlanet : public LiveActor {
public:
    MarblePlanet(const char*);

    virtual ~MarblePlanet();
    virtual void init(const JMapInfoIter&);
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
    MarblePlanetElectron(LiveActor*, const TVec3f&, const TVec3f&, const char*);

    virtual ~MarblePlanetElectron();
    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeMove();
    void exeAttack();
    void crashElectron(HitSensor*);

    MarblePlanet* mParentPlanet;
    MarblePlanetElectronShadow* mElectronShadow;
    TVec3f _94;
};

class MarblePlanetElectronShadow : public LiveActor {
public:
    MarblePlanetElectronShadow(LiveActor*, const TVec3f&, const char*);

    virtual ~MarblePlanetElectronShadow();
    virtual void init(const JMapInfoIter&);
    virtual void calcAndSetBaseMtx();

    MarblePlanetElectron* mParentElectron;
    const TVec3f* _90;
};
