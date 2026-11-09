#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FloaterFloatingForceTypeNormal;
class ProjmapEffectMtxSetter;
class LodCtrl;

class LavaFloater : public LiveActor {
public:
    LavaFloater(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;

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
