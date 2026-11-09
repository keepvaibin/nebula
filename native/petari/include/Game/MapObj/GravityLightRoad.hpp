#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class GravityLightRoad : public LiveActor {
public:
    GravityLightRoad(const char*);

    virtual void init(const TableIter&) override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void updateBindActorMtx();
    void endBind();
    void startDisappear();
    bool tryStartBind(LiveActor*);
    bool isPlayerInCylinder() const;

    void exeWait();
    void exeDraw();
    void exeDisappear();

    LiveActor* mHost;
    f32 mSpeed;
    f32 mRadius;
    Vec3f mEndPos;
};
