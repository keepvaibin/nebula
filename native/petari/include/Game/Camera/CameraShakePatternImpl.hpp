#pragma once

#include <nebula_geometry.hpp>

class CameraShakePattern {
public:
    CameraShakePattern() : mFrame() {
    }

    virtual ~CameraShakePattern() {
    }

    virtual bool isEnd() const = 0;
    virtual void getOffset(Vec2f*) const = 0;
    virtual void start() = 0;
    virtual void update() = 0;

    u32 mFrame;
};

class CameraShakePatternSingly : public CameraShakePattern {
public:
    CameraShakePatternSingly(f32);

    virtual bool isEnd() const;
    virtual void getOffset(Vec2f*) const;
    virtual void start();
    virtual void update();

    void setDirection(const Vec2f&);

    f32 mIntensity;
    Vec2f mOffset;
    Vec2f mDirection;
};

class CameraShakePatternVerticalSin : public CameraShakePattern {
public:
    CameraShakePatternVerticalSin(f32, f32);

    virtual bool isEnd() const;
    virtual void getOffset(Vec2f*) const;
    virtual void start();
    virtual void update();

    f32 mIntensity;
    f32 mSpeed;
    Vec2f mOffset;
};
