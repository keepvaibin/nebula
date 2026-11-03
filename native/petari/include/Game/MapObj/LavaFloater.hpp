#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FloaterFloatingForceTypeNormal;
class ProjmapEffectMtxSetter;
class LodCtrl;

class LavaFloater : public LiveActor {
public:
    LavaFloater(const char*);

    virtual ~LavaFloater();
    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

    f32 getCurrentSinkDepth() const;

    void exeFloat();
    void exeSink();

    FloaterFloatingForceTypeNormal* mFloatingForce;
    ProjmapEffectMtxSetter* mMtxSetter;
    LodCtrl* mLodCtrlPlanet;
    const char* mObjectName;
    TVec3f _9C;
    f32 _A8;
};
