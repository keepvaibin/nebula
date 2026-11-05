#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;

class GCaptureRibbon : public LiveActor {
public:
    GCaptureRibbon(const char*);

    virtual ~GCaptureRibbon();
    virtual void init(const JMapInfoIter&);
    virtual void draw() const;

    void reset();
    void lengthen(const Vec3f&, const Vec3f&);
    void shorten(const Vec3f&, const Vec3f&);
    void updateAxis();
    f32 calcLineWidth() const;

    JUTTexture* mTexture;
    Vec3f _90[0x40];
    Vec3f _390[0x40];
    Vec3f _690[0x40];
    s32 _990;
    s32 _994;
};
