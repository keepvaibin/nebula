#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

// A value stepped linearly between 0 and 1 over mMaxFrame frames, toward one end or the other.
class ValueControl {
public:
    enum Direction {
        Direction_Backward = -1,  // toward 0
        Direction_Forward = 1,    // toward 1
    };

    ValueControl(int maxFrame);

    void setDirToOne();
    // Toward 1, from frame 0.
    void setDirToOneResetFrame();
    void setDirToZero();
    // Toward 0, from the last frame.
    void setDirToZeroResetFrame();
    // Keeps the value.
    void setMaxFrame(int maxFrame);
    void update();
    void setZero();
    void setOne();
    f32 getValue() const;

    void resetFrame();

    inline bool isFrameMaxFrame() const {
        return mFrame == mMaxFrame;
    }

    s32 mFrame;
    s32 mMaxFrame;
    s32 mDirection;
};
