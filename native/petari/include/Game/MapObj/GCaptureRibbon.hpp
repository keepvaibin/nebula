#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;

class GCaptureRibbon : public LiveActor {
public:
    GCaptureRibbon(const char*);

    virtual ~GCaptureRibbon();
    virtual void init(const TableIter&);
    virtual void draw() const;

    void reset();
    void lengthen(const Vec3f&, const Vec3f&);
    void shorten(const Vec3f&, const Vec3f&);
    void updateAxis();
    f32 calcLineWidth() const;

    JUTTexture* mTexture;
    Vec3f mPoints[0x40];
    Vec3f mSides[0x40];
    Vec3f mUps[0x40];
    s32 mPointNum;
};
