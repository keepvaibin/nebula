#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Color8;
class JUTTexture;
class SwingRopePoint;

class Flag : public LiveActor {
public:
    class FixPoints {
    public:
        FixPoints() {
        }

        TVec3f mPos;
        SwingRopePoint** mPoints;
    };

    Flag(const char*);

    virtual ~Flag();

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void appear();

    void setInfoPos(const char* pObjName, const TVec3f* pPos, const TVec3f& rSide, f32 stickLength, f32 width, f32 height, s32 numPointsU,
                    s32 numPointsV, f32);
    void updateFlag();
    void loadMaterial() const;
    // Vertex `index`'s colour and texture coordinate (its mColors and mTexST entries) for the draw being sent.
    void sendEntry(u8 index) const;

    s32 getNumPointsU() const {
        return mNumPointsU;
    }

    s32 getNumPointsV() const {
        return mNumPointsV;
    }

    const FixPoints& getFixPoint(s32 idx) const {
        return mFixPoints[idx];
    }

    SwingRopePoint* getPoint(s32 idxV, s32 idxU) const {
        return mFixPoints[idxV].mPoints[idxU];
    }

    u32 _8C;  // Note: unused?
    const char* mObjName;
    bool mDisableLighting;
    s32 mNumPointsV;
    s32 mNumPointsU;
    f32 mPointIntervalU;
    f32 mPointIntervalV;
    FixPoints* mFixPoints;
    TVec3f mUp;
    TVec3f mClipCenter;
    const TVec3f* mBasePos;
    TPos3f* mBaseMtx;
    f32 mStickLength;
    f32 mWavePhase;
    TVec3f mSide;
    TVec3f mFront;
    bool mIsVerticalFlag;
    s32 mWaitTime;
    f32 mGravityScale;
    f32 mFrictionRateMin;
    f32 mFrictionRateMax;
    f32 mWindAccelRate;
    f32 mWindAccelMin;
    f32 mRandomWindAccelMin;
    f32 mRandomWindAccelMax;
    f32 mLightColorMin;
    Color8* mColors;
    // Texture coordinates.
    f32* mTexST;
    JUTTexture* mTexture;
    bool mUseAlpha;
    u8 mAlpha;
    f32 mAlphaDistanceNear;
    f32 mAlphaDistanceFar;
    s32 mSeStep;
    bool mDisableSound;
};
