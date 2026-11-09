#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AudSeKeeper;
class CollisionParts;

class SeaBottomTriplePropeller : public LiveActor {
public:
    SeaBottomTriplePropeller(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void control() override;

    void exeWait();
    void exeBreak();

    CollisionParts* mPropellerCollision[3];
    AudSeKeeper* mAudSeKeeper;
};
