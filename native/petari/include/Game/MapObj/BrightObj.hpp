#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>
#include <nebula_game_api.h>

class Sun;

class BrightInfo {
public:
    BrightInfo();

    void write(const Vec2f&, const Vec2f&, f32);
    void endRead();
    void reset();

    u32 mIndex;
    u32 mCurrIndex;
    Vec2f mBrightnessCenter[3];
    Vec2f mRealCenter[3];
    f32 mBright[3];
};

class BrightObjBase {
public:
    // The lens flare's readback channel, and the points of the picture an object asks about each frame. An object that
    // asked nothing leaves cNoProbes in its first point's x.
    static const u32 cReadbackChannel = 3;
    static const u32 cProbeCount = 17;
    static constexpr f32 cNoProbes = -2.0f;

    BrightObjBase();

    virtual ~BrightObjBase();
    virtual void calcBrightInfo(NebulaScreenProbe*) = 0;
    virtual f32 getBright() const;
    virtual const Vec2f* getBrightCenter() const;
    virtual const Vec2f* getCenter() const;
    virtual void endRead();
    virtual void getNowCenter(Vec2f*) const;

    void checkVisibilityOfSphere(NebulaScreenProbe*);
    void checkVisible(NebulaScreenProbe*, const Vec3f&, const Pos3f&, const Proj3f&);
    void setResult(const NebulaScreenProbe*);

    BrightInfo mInfo;
    Vec2f mBrightCenter;
    Vec2f mNowCenter;
    f32 mBrightness;
    Vec3f mDrawPos;
    f32 mDrawRadius;
};

class BrightObj : public LiveActor, public BrightObjBase {
public:
    BrightObj(const char*);

    virtual void init(const TableIter&);
    virtual void control();

    virtual void calcBrightInfo(NebulaScreenProbe*);
    virtual void getNowCenter(Vec2f*) const;

    f32 mRadius;
};

class BrightSun : public LiveActor, public BrightObjBase {
public:
    BrightSun(const char*);

    virtual void init(const TableIter&);
    virtual void control();

    virtual void calcBrightInfo(NebulaScreenProbe*);
    virtual void getNowCenter(Vec2f*) const;

    void controlSunModel();

    Sun* mSun;
};
