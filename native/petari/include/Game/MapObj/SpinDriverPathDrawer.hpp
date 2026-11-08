#pragma once

#include "Game/Render/TypedDraw.hpp"

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;
class SpinDriverShootPath;

class SpinDriverPathDrawer : public LiveActor {
public:
    SpinDriverPathDrawer(SpinDriverShootPath*);

    virtual void init(const TableIter&);
    virtual void draw() const;

    s32 calcPositionCount(f32, f32) const;
    void initPositionList(f32, f32);
    void initClipping();
    void initPathEnd();
    f32 calcDrawCode() const;
    f32 calcDrawBaseCode() const;
    void setCoord(f32);
    void setColorNormal();
    void setColorGreen();
    void setColorPink();
    void setMaskLength(f32);
    void setFadeScale(f32);
    void sendPoint(VertexPT*&, const Vec3f& rPos, f32 u, f32 v) const;

    SpinDriverShootPath* mShootPath;
    Vec3f* mPositions;
    Vec3f* mSides;
    Vec3f* mUps;
    f32* mCoords;
    s32 mPositionCount;
    f32 mCoord;
    f32 mMaxCoord;
    s32 mColor;
    f32 mEndCut;
    f32 mFadeScale;
    f32 mMaskLength;
};

class SpinDriverPathDrawInit : public NameObj {
public:
    SpinDriverPathDrawInit();

    void initDraw();

    JUTTexture* mOrangeTexture;
    JUTTexture* mGreenTexture;
    JUTTexture* mPinkTexture;
    JUTTexture* mMaskTexture;
    bool mIsPathAtOpa;
};

namespace MR {
    void setSpinDriverPathColorNormal();
    void setSpinDriverPathColorGreen();
    void setSpinDriverPathColorPink();
    bool isDrawSpinDriverPathAtOpa();
    void onDrawSpinDriverPathAtOpa();
    void offDrawSpinDriverPathAtOpa();
};  // namespace MR
