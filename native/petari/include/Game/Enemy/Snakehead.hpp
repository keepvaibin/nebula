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

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeWaylay();
    void exeWait();
    void exeMoveForward();
    void exeRest();
    void exeMoveBack();
    void exeTurtleDown();
    void exePointerSwoon();
    void endPointerSwoon();
    enum BckKind {
        Bck_Waylay,
        Bck_Wait,
        Bck_Forward,
        Bck_Back
    };

    void choiceAndStartBck(BckKind);
    bool tryPointerSwoon();
    bool isNearPlayerFromRail() const;

    bool isSmall() const {
        return mType == SnakeheadType_SmallWait || mType == SnakeheadType_SmallMove;
    }

    // Swims back and forth on its own instead of waylaying the player.
    bool isMove() const {
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
