#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ModelObj;
class SimpleJ3DModelDrawer;
class SwingRopePoint;

class KirairaChain : public LiveActor {
public:
    KirairaChain(const LiveActor*);
    virtual ~KirairaChain();

    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;
    virtual void control();

    void initPoints();
    void updatePoints();
    void restrictPointFromTop(s32, s32, const TVec3f&, f32);
    void restrictPointFromBottom(s32, s32, const TVec3f&, f32);
    void restrictPointFromTopAndCalcAxis(s32, s32, const TVec3f&, f32);
    void restrictPointFromBottomAndCalcAxis(s32, s32, const TVec3f&, f32);

    const TVec3f* getCutPos() const {
        return &mCutPos;
    }

    const LiveActor* mHost;
    bool mIsCut;
    TVec3f mCutPos;
    TVec3f mRailStart;
    TVec3f mRailEnd;
    TPos3f mFixPointBottomMtx;
    TPos3f mFixPointTopMtx;
    TVec3f mFixPointTopDir;
    TVec3f mFixPointBottomDir;
    s32 mPointCount;
    SwingRopePoint** mPoints;
    s32 _138;
    u8 _13C[4];
    s32 mAccelPointIdx;
    s32 mAccelTimer;
    TVec3f mAccel;
    TVec3f mClippingCenter;
    SimpleJ3DModelDrawer* mModelDrawer;
    ModelObj* mFixPointTop;
    ModelObj* mFixPointBottom;
};
