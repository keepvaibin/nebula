#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class GameTexture;

// The G-capture line: a textured ribbon from the capture point to its target.
class GCaptureRibbon : public LiveActor {
public:
    GCaptureRibbon(const char*);

    virtual void init(const TableIter&);
    virtual void draw() const;

    void reset();
    void setLine(const Vec3f&, const Vec3f&);
    f32 calcLineWidth() const;

    GameTexture* mTexture;
    Vec3f mStart;
    Vec3f mEnd;
    Vec3f mSide;
    Vec3f mUp;
    bool mIsLineSet;
};
