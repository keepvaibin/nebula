#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Render/TypedDraw.hpp"
#include <nebula_geometry.hpp>

class Color8;
class GameTexture;
class SwingRopePoint;

class Flag : public LiveActor {
public:
    class FixPoints {
    public:
        FixPoints() {
        }

        Vec3f mPos;
        SwingRopePoint** mPoints;
    };

    Flag(const char*);

    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void draw() const;
    virtual void appear();

    void setInfoPos(const char* pObjName, const Vec3f* pPos, const Vec3f& rSide, f32 stickLength, f32 width, f32 height, s32 numPointsU,
                    s32 numPointsV);
    void updateFlag();
    SiteMaterial loadMaterial() const;
    // Vertex `index`'s colour and texture coordinate (its mColors and mTexST entries) for the draw being sent.
    VertexPCT vertexOf(const Vec3f& rPos, u8 index) const;

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

    const char* mObjName;
    bool mDisableLighting;
    s32 mNumPointsV;
    s32 mNumPointsU;
    f32 mPointIntervalU;
    f32 mPointIntervalV;
    FixPoints* mFixPoints;
    Vec3f mUp;
    Vec3f mClipCenter;
    const Vec3f* mBasePos;
    Pos3f* mBaseMtx;
    f32 mStickLength;
    f32 mWavePhase;
    Vec3f mSide;
    Vec3f mFront;
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
    GameTexture* mTexture;
    bool mUseAlpha;
    u8 mAlpha;
    f32 mAlphaDistanceNear;
    f32 mAlphaDistanceFar;
    s32 mSeStep;
    bool mDisableSound;
};
