#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FloaterFloatingForceTypeNormal;
class ProjmapEffectMtxSetter;
class LodCtrl;

class LavaFloater : public LiveActor {
public:
    LavaFloater(const char*);

    virtual void init(const TableIter&);
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
    Vec3f mGroundPos;
    f32 mGroundOffset;
};
