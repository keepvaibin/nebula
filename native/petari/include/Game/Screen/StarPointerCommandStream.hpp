#pragma once

#include "Game/Screen/LayoutActor.hpp"

class StarPointerCommandStream : public LayoutActor {
public:
    StarPointerCommandStream(const TVec2f*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();

    void start(const TVec3f*, bool);
    void show();
    void hide();
    void clear();
    void calcPose(const TVec2f&, const TVec2f&);

    void exeWait();
    void exeSignal();
    f32 calcLineWidth(f32) const;

    inline void setPortAndColor(s32 padChannel, const NebulaColor8* color) {
        mPadChannel = padChannel;
        mColor = color;
    }

    bool _20;
    s32 mOffScreenTime;
    f32 _28;
    const TVec2f* mScreenPos;
    const TVec3f* mWorldPos;
    TVec2f mFollowPos;
    s32 mPadChannel;
    const NebulaColor8* mColor;
};
