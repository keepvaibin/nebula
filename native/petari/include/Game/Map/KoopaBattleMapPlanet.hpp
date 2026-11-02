#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class LiveActorGroup;

class KoopaBattleMapPlanet : public LiveActor {
public:
    KoopaBattleMapPlanet(const char*, const char*, bool, bool, bool);

    virtual ~KoopaBattleMapPlanet();
    virtual void init(const JMapInfoIter&);
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mBaseMtx;
    }
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void startAnimPowerStarAppear();
    void createPlate();

    void exeWait();
    void exeDemoGrandStarAppear();

private:
    const char* mModelName;
    TPos3f mBaseMtx;
    TVec3f _C0;
    LiveActorGroup* mDamagePlateGroup;
    LiveActorGroup* mCoinPlateGroup;
    LiveActorGroup* mPlateGroup;
    LiveActor* mDamagePlate;
    bool _DC;
    bool _DD;
    bool _DE;
};
