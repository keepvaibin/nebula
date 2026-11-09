#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class CameraShakeTask;

class CameraShaker : public NameObj {
public:
    // One task per intensity.
    static const u32 cVerticalTaskNum = 7;
    static const u32 cHorizontalTaskNum = 3;

    enum ESinglyVerticalPower {
        SINGLY_VERTICAL_POWER_VERY_WEAK = 0,
        SINGLY_VERTICAL_POWER_WEAK = 1,
        SINGLY_VERTICAL_POWER_NORMAL_WEAK = 2,
        SINGLY_VERTICAL_POWER_NORMAL = 3,
        SINGLY_VERTICAL_POWER_NORMAL_STRONG = 4,
        SINGLY_VERTICAL_POWER_STRONG = 5,
        SINGLY_VERTICAL_POWER_VERY_STRONG = 6
    };

    CameraShaker(const char*);

    virtual void movement() override;

    void shakeVertical(ESinglyVerticalPower);
    void shakeInfinity(NameObj*, f32, f32);
    void stopShakingInfinity(NameObj*);
    void createSinglyVerticalTask();
    void createSinglyHorizontalTask();
    void createInfinityTask();
    void updateSinglyVerticalTask();
    void updateSinglyHorizontalTask();
    void updateInfinityTask();
    void setOffset();
    void addSinglyVerticalTaskOffset(Vec2f*);
    void addSinglyHorizontalTaskOffset(Vec2f*);
    void addInfinityTaskOffset(Vec2f*);
    void adjustOffsetToScreen(Vec2f*);
    void startInfinity(u32, f32, f32);

    CameraShakeTask* mVerticalTasks[cVerticalTaskNum];
    CameraShakeTask* mHorizontalTasks[cHorizontalTaskNum];
    struct InfinityShake {
        CameraShakeTask* mTask;
        NameObj* mOwner;
    };

    // One per shaking owner at a time; an ended entry is reused.
    MR::Vector< MR::AssignableArray< InfinityShake > > mInfinity;
};
