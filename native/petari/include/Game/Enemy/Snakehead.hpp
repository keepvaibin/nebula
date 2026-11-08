#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

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

    virtual void init(const TableIter&);
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
    void exePointerSwoon();
    void endPointerSwoon();
    void choiceAndStartBck(const char*);
    bool tryPointerSwoon();
    bool isNearPlayerFromRail() const;

    bool isSmall() {
        return mType == SnakeheadType_SmallWait || mType == SnakeheadType_SmallMove;
    }

    bool isMove() {
        return mType == SnakeheadType_SmallMove || mType == SnakeheadType_BigMove;
    }

    AnimScaleController* mController;
    WalkerStateBindStarPointer* mStarPointer;
    Pos3f mBodyMtx;
    Vec3f mSmokePos;
    f32 mForwardSpeed;
    f32 mBackwardSpeed;
    f32 mActivationRadius;
    Vec3f mRailCenter;
    SnakeheadType mType;
    BenefitItemOneUp* mItem;
};
