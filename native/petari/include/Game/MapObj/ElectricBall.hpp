#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailMover;
class PartsModel;

class ElectricBall : public LiveActor {
public:
    class Ball {
    public:
        Ball();

        void init(LiveActor*);
        void updatePosition(const Pos3f&);
        void activate();

        PartsModel* mHost;
        Vec3f mPosition;
        bool mIsActive;
    };

    ElectricBall(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mBaseMtx.mMtx);
    };
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void initBalls(const TableIter&);
    Ball* getNearestBall();
    void startMove();

    void exeWait() {};
    void exeMove() {};
    void exeAttack();

    Pos3f mBaseMtx;
    MR::AssignableArray< Ball > mBalls;
    Vec3f mClosestBallPos;
    f32 mRadius;        // Obj_arg1
    f32 mAngularSpeed;  // Obj_arg2
    MapPartsRailMover* mRailMover;
};
