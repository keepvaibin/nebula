#pragma once

#include <JSystem/JGeometry/TBox.hpp>
#include <JSystem/JGeometry/TVec.hpp>

class SpiderThreadMainPoint;
class SpiderThreadPoint;

class SpiderThreadPart {
public:
    SpiderThreadPart(SpiderThreadMainPoint*, SpiderThreadMainPoint*, f32);

    void cut(SpiderThreadMainPoint*, SpiderThreadMainPoint*);
    void updatePointerPos();
    void update();
    void draw() const;
    void startHang(s32);
    void updateHang(const TVec3f&, s32);
    void endHang(s32);
    void stopAllPoints();
    f32 calcNearestPointInfo(const TVec3f**, const TVec3f**, s32*, const TVec3f&);
    void updatePointPosToLine();
    void updatePointPosAndBasePosToLine();
    bool touchActor(s32, const TVec3f&);
    void tryTouch(f32, const TVec3f&, s32);
    s32 findTouchPoint(f32, const TVec3f&, s32);
    f32 findNearestPointNo(s32*, const TVec3f&) const;
    void updatePointPosHanging(s32);
    void updatePointVelocityTouch(s32, const TVec3f&);
    void updateBasePos();
    void updateForceAttach();
    void updateCutPoints();
    void updateDrawVec();

    void drawLine() const;
    void drawPoints() const;

    inline void padBox(f32 padAmount) {
        TVec3f padding(padAmount);
        mBoundBox.i.sub(padding);
        mBoundBox.f.add(padding);
    }

    SpiderThreadPoint* getPoint(int idx) const {
        return mPoints[idx];
    }

    bool mIsStill;
    bool mIsCut;
    bool mIsFree;
    s16 _4;
    s32 mNumHangingPoints;
    s32 mStopTime;
    s32 mTouchTimeout;
    TVec3f mPointerPosPad0;
    TVec3f mPointerPosPad1;
    SpiderThreadMainPoint* mPointA;
    SpiderThreadMainPoint* mPointB;
    s32 mNumPoints;
    SpiderThreadPoint** mPoints;
    TBox3f mBoundBox;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    f32 mDistBetweenMainPoints;
    f32 mDistBetweenPoints;
    f32 mTextureOffset;
};
