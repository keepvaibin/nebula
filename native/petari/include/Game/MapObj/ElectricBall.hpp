#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class MapPartsRailMover;
class PartsModel;

class ElectricBall : public LiveActor {
public:
    class Ball {
    public:
        Ball();

        void init(LiveActor*);
        void updatePosition(const TPos3f&);
        void activate();

        PartsModel* mHost;
        TVec3f mPosition;
        bool mIsActive;
    };

    ElectricBall(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual MtxPtr getBaseMtx() const {
        return (MtxPtr)mBaseMtx.mMtx;
    };
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void initBalls(const JMapInfoIter&);
    Ball* getNearestBall();
    void startMove();

    void exeWait() {};
    void exeMove() {};
    void exeAttack();

    TPos3f mBaseMtx;
    MR::AssignableArray< Ball > mBalls;
    TVec3f mClosestBallPos;
    f32 mRadius;        // Obj_arg1
    f32 mAngularSpeed;  // Obj_arg2
    MapPartsRailMover* mRailMover;
};
