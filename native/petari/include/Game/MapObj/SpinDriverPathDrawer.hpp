#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;
class SpinDriverShootPath;

class SpinDriverPathDrawer : public LiveActor {
public:
    SpinDriverPathDrawer(SpinDriverShootPath*);

    virtual void init(const JMapInfoIter&);
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
    void sendPoint(const TVec3f&, f32, f32) const;

    SpinDriverShootPath* mShootPath;
    TVec3f* _90;
    TVec3f* _94;
    TVec3f* _98;
    f32* _9C;
    s32 mPositionCount;
    u32 _A4;
    u32 _A8;
    f32 _AC;
    f32 _B0;
    s32 mColor;
    f32 _B8;
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
