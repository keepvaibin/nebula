#pragma once

#include <JSystem/JGeometry/TVec.hpp>

class LayoutActor;

class StarPointerTarget {
public:
    StarPointerTarget(f32, const TVec3f*, MtxPtr, TVec3f);

    bool calcScreenPosition(TVec2f*) const;
    bool isPointing(const TVec2f&, f32, f32) const;
    void calcPosition(TVec3f*) const;
    f32 calcRadius2d(f32, f32, f32) const;
    bool calcScreenPositionFromView(TVec2f*, const TVec3f&) const;
    // Where it shows and how big, as isPointing sees it.
    bool calcScreenCircle(TVec2f*, f32*) const;

    f32 mRadius3d;
    TVec3f mOffset;
    s32 mLastPointedChannel;
    const TVec3f* mPosition;
    MtxPtr mMtx;
};

class StarPointerLayoutTarget {
public:
    enum TargetType { TargetType_Circle = 0, TargetType_Rectangle = 1 };
    StarPointerLayoutTarget(LayoutActor*, const char*, const char*);

    bool isPointing(const TVec2f&) const;
    bool isPointingCircle(const TVec2f&) const;
    bool isPointingRectangle(const TVec2f&) const;
    void calcBasePos(TVec2f*) const;

    LayoutActor* mLayout;
    const char* mLayoutName;
    const char* mPaneName;
    s32 mType;
    TVec2f mPosition;
    f32 mRadius;
    f32 mWidth;
    f32 mHeight;
};

class StarPointerLayoutTargetKeeper {
public:
    StarPointerLayoutTargetKeeper(int);

    void addTargetCircle(LayoutActor*, const char*, f32, const TVec2f&, const char*);
    StarPointerLayoutTarget* getTarget(const char*) const;
    void registerTarget(StarPointerLayoutTarget*);

    s32 mNumTargets;
    s32 mMaxNumTargets;
    StarPointerLayoutTarget** mTargets;
};
