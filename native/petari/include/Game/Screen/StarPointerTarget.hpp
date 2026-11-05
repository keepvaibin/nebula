#pragma once

#include <nebula_geometry.hpp>

class LayoutActor;

class StarPointerTarget {
public:
    StarPointerTarget(f32, const Vec3f*, MtxPtr, Vec3f);

    bool calcScreenPosition(Vec2f*) const;
    bool isPointing(const Vec2f&, f32, f32) const;
    void calcPosition(Vec3f*) const;
    f32 calcRadius2d(f32, f32, f32) const;
    bool calcScreenPositionFromView(Vec2f*, const Vec3f&) const;
    // Where it shows and how big, as isPointing sees it.
    bool calcScreenCircle(Vec2f*, f32*) const;

    f32 mRadius3d;
    Vec3f mOffset;
    s32 mLastPointedChannel;
    const Vec3f* mPosition;
    MtxPtr mMtx;
};

class StarPointerLayoutTarget {
public:
    enum TargetType { TargetType_Circle = 0, TargetType_Rectangle = 1 };
    StarPointerLayoutTarget(LayoutActor*, const char*, const char*);

    bool isPointing(const Vec2f&) const;
    bool isPointingCircle(const Vec2f&) const;
    bool isPointingRectangle(const Vec2f&) const;
    void calcBasePos(Vec2f*) const;

    LayoutActor* mLayout;
    const char* mLayoutName;
    const char* mPaneName;
    s32 mType;
    Vec2f mPosition;
    f32 mRadius;
    f32 mWidth;
    f32 mHeight;
};

class StarPointerLayoutTargetKeeper {
public:
    StarPointerLayoutTargetKeeper(int);

    void addTargetCircle(LayoutActor*, const char*, f32, const Vec2f&, const char*);
    StarPointerLayoutTarget* getTarget(const char*) const;
    void registerTarget(StarPointerLayoutTarget*);

    s32 mNumTargets;
    s32 mMaxNumTargets;
    StarPointerLayoutTarget** mTargets;
};
