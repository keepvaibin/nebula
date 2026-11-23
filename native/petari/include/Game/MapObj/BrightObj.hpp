#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>
#include <nebula_game_api.h>

class Sun;

// One point of a bright object's sphere that the lens flare's sight pass reads (LensFlareDirector::drawSight): its place
// in the frame (0 to 1, y down), the game's depth just in front of the sphere there (0 near to 1 far), and whether it is
// on the screen.
struct BrightSample {
    f32 mFrameX;
    f32 mFrameY;
    f32 mDepth;
    bool mOnScreen;
};

class BrightObjBase {
public:
    // The points of the sphere the flare's sight reads: the centre, eight at 0.4 of the radius and eight at 0.7.
    static const u32 cSampleCount = 17;

    BrightObjBase();

    virtual ~BrightObjBase();
    // Whether the object gives light now (a sun not out gives none).
    virtual bool isShining() const {
        return true;
    }
    // Its centre on the screen now.
    virtual void getNowCenter(Vec2f*) const = 0;

    // Its samples seen with the scene's camera as it stands; how many are on the screen.
    u32 calcSamples(BrightSample*) const;

    Vec3f mDrawPos;
    f32 mDrawRadius;
};

class BrightObj : public LiveActor, public BrightObjBase {
public:
    BrightObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    virtual void getNowCenter(Vec2f*) const override;

    f32 mRadius;
};

class BrightSun : public LiveActor, public BrightObjBase {
public:
    BrightSun(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    virtual bool isShining() const override;
    virtual void getNowCenter(Vec2f*) const override;

    void controlSunModel();

    Sun* mSun;
};
