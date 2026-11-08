#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class DinoPackunTrackFire : public LiveActor {
public:
    DinoPackunTrackFire(const char*);

    virtual void init(const TableIter&);
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)&mMtx;
    };
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void appearAndSetPos(const Vec3f&);
    void exeWait();
    void exeGround();

    Pos3f mMtx;
};

class DinoPackunTrackFireHolder : public DeriveActorGroup< DinoPackunTrackFire > {
public:
    DinoPackunTrackFireHolder(s32);

};
