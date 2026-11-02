#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AnimScaleController;
class BenefitItemOneUp;
class WalkerStateBindStarPointer;

enum SnakeheadType {
    SnakeheadType_BigWait,
    SnakeheadType_SmallWait,
    SnakeheadType_BigMove,
    SnakeheadType_SmallMove
};

class Snakehead : public LiveActor {
public:
    Snakehead(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeWaylay();
    void exeWait();
    void exeMoveForward();
    void exeRest();
    void exeMoveBack();
    void exeTurtleDown();
    void exeDPDSwoon();
    void endDPDSwoon();
    void choiceAndStartBck(const char*);
    bool tryDPDSwoon();
    bool isNearPlayerFromRail() const;

    bool isSmall() {
        return _E8 == SnakeheadType_SmallWait || _E8 == SnakeheadType_SmallMove;
    }

    bool isMove() {
        return _E8 == SnakeheadType_SmallMove || _E8 == SnakeheadType_BigMove;
    }

    AnimScaleController* mController;
    WalkerStateBindStarPointer* mStarPointer;
    TPos3f mBodyMtx;
    TVec3f _C4;
    f32 mForwardSpeed;
    f32 mBackwardSpeed;
    f32 mActivationRadius;
    TVec3f mRailCenter;
    SnakeheadType _E8;
    BenefitItemOneUp* mItem;
};
