#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include <nebula_geometry.hpp>

class DinoPackunTrackFire : public LiveActor {
public:
    DinoPackunTrackFire(const char*);

    virtual void init(const TableIter&) override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mMtx.mMtx);
    };
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void appearAndSetPos(const Vec3f&);
    void exeWait();
    void exeGround();

    Pos3f mMtx;
};

class DinoPackunTrackFireHolder : public DeriveActorGroup< DinoPackunTrackFire > {
public:
    DinoPackunTrackFireHolder(s32);

};
