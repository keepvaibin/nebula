#pragma once

#include "Game/AreaObj/AreaObj.hpp"

class BigBubble;

class BigBubbleGoalArea : public AreaObj {
public:
    BigBubbleGoalArea(int, const char*);

    virtual ~BigBubbleGoalArea() {};
    virtual void init(const JMapInfoIter&);
    virtual void movement();
    virtual void draw() const;

    virtual const char* getManagerName() const {
        return "BigBubbleGoalArea";
    };

    void addBubble(BigBubble*);

    TVec3f mTrans;
    f32 mMeterCapacity;
    f32 mMeterDisplayAmount;
    f32 mMeterAmount;
};

namespace MR {
    bool checkBigBubbleGoal(BigBubble*);
};  // namespace MR
