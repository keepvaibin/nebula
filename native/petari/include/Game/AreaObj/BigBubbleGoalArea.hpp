#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class BigBubble;

class BigBubbleGoalArea : public AreaObj {
public:
    BigBubbleGoalArea(int, const char*);

    virtual void init(const TableIter&);
    virtual void movement();
    virtual void draw() const;

    virtual const char* getManagerName() const {
        return "BigBubbleGoalArea";
    };

    void addBubble(BigBubble*);

    Vec3f mTrans;
    f32 mMeterCapacity;
    f32 mMeterDisplayAmount;
    f32 mMeterAmount;
};

namespace MR {
    bool checkBigBubbleGoal(BigBubble*);
};  // namespace MR
