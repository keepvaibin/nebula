#pragma once

#include "Game/Screen/LayoutActor.hpp"

class StarPointerCommandStream : public LayoutActor {
public:
    StarPointerCommandStream(const Vec2f*);

    virtual void init(const TableIter& rIter);
    virtual void control();

    void start(const Vec3f*, bool);
    void show();
    void hide();
    void clear();
    void calcPose(const Vec2f&, const Vec2f&);

    void exeWait();
    void exeSignal();
    f32 calcLineWidth(f32) const;

    inline void setPortAndColor(s32 padChannel, const NebulaColor8* color) {
        mPadChannel = padChannel;
        mColor = color;
    }

    s32 mOffScreenTime;
    const Vec2f* mScreenPos;
    const Vec3f* mWorldPos;
    Vec2f mFollowPos;
    s32 mPadChannel;
    const NebulaColor8* mColor;
};
