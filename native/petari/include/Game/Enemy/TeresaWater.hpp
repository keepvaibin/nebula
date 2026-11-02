#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class TeresaWater : public LiveActor {
public:
    /// @brief Creates a new `TeresaWater`.
    /// @param pName A pointer to the null-terminated name of the object.
    TeresaWater(const char* pName);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeMove();
    void exeHit();
    void exeStun();
    void exeDPDSwoon();
    void endDPDSwoon();
    bool tryDPDSwoon();

    AnimScaleController* _8C;
    WalkerStateBindStarPointer* _90;
    TVec3f _94;
    TVec3f _A0;
    TVec3f _AC;
    TVec3f* _B8;
};
