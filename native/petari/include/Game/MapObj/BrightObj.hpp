#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <nebula_game_api.h>

class Sun;

class BrightInfo {
public:
    /// @brief Creates a new `BrightInfo`.
    BrightInfo();

    void write(const TVec2f&, const TVec2f&, f32);
    void endRead();
    void reset();

    u32 mIndex;
    u32 mCurrIndex;
    TVec2f mBrightnessCenter[3];
    TVec2f mRealCenter[3];
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
    virtual const TVec2f* getBrightCenter() const;
    virtual const TVec2f* getCenter() const;
    virtual void endRead();
    virtual void getNowCenter(TVec2f*) const;

    void checkVisibilityOfSphere(NebulaScreenProbe*);
    void checkVisible(NebulaScreenProbe*, const TVec3f&, const TPos3f&, const TProj3f&);
    void setResult(const NebulaScreenProbe*);
    void drawSphere(const TVec3f&, float) const;

    BrightInfo mInfo;
    TVec2f mBrightCenter;
    TVec2f mNowCenter;
    f32 mBrightness;
    bool mIsNotVisible;
    TVec3f mDrawPos;
    f32 mDrawRadius;
};

class BrightObj : public LiveActor, public BrightObjBase {
public:
    BrightObj(const char*);

    virtual ~BrightObj();
    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void control();

    virtual void calcBrightInfo(NebulaScreenProbe*);
    virtual void getNowCenter(TVec2f*) const;

    f32 mRadius;
};

class BrightSun : public LiveActor, public BrightObjBase {
public:
    BrightSun(const char*);

    virtual ~BrightSun();
    virtual void init(const JMapInfoIter&);
    virtual void draw() const;
    virtual void control();

    virtual void calcBrightInfo(NebulaScreenProbe*);
    virtual void getNowCenter(TVec2f*) const;

    void controlSunModel();

    Sun* mSun;
};
