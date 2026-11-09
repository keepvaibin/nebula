#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MarblePlanetElectron;
class MarblePlanetElectronShadow;
class ModelObj;

class MarblePlanet : public LiveActor {
public:
    MarblePlanet(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

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

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

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

    virtual void init(const TableIter&) override;
    virtual void calcAndSetBaseMtx() override;

    MarblePlanetElectron* mParentElectron;
    const Vec3f* mGroundPos;
};
