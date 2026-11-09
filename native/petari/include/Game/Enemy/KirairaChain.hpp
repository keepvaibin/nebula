#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ModelObj;
class SimpleModelDrawer;
class SwingRopePoint;

class KirairaChain : public LiveActor {
public:
    KirairaChain(const LiveActor*);

    virtual void init(const TableIter& rIter) override;
    virtual void draw() const override;
    virtual void control() override;

    void initPoints();
    void updatePoints();
    void restrictPointFromTop(s32, s32, const Vec3f&, f32);
    void restrictPointFromBottom(s32, s32, const Vec3f&, f32);
    void restrictPointFromTopAndCalcAxis(s32, s32, const Vec3f&, f32);
    void restrictPointFromBottomAndCalcAxis(s32, s32, const Vec3f&, f32);

    const Vec3f* getCutPos() const {
        return &mCutPos;
    }

    const LiveActor* mHost;
    bool mIsCut;
    Vec3f mCutPos;
    Vec3f mRailStart;
    Vec3f mRailEnd;
    Pos3f mFixPointBottomMtx;
    Pos3f mFixPointTopMtx;
    Vec3f mFixPointTopDir;
    Vec3f mFixPointBottomDir;
    s32 mPointCount;
    SwingRopePoint** mPoints;
    s32 mAccelPointIdx;
    s32 mAccelTimer;
    Vec3f mAccel;
    Vec3f mClippingCenter;
    SimpleModelDrawer* mModelDrawer;
    ModelObj* mFixPointTop;
    ModelObj* mFixPointBottom;
};
