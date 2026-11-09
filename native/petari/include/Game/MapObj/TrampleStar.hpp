#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class GameTexture;

class TrampleStar : public LiveActor {
public:
    class Surface {
    public:
        Surface() {};

        void set(u16 vtx1, u16 vtx2, u16 vtx3) {
            mVtxs[0] = vtx1;
            mVtxs[1] = vtx2;
            mVtxs[2] = vtx3;
        }

        u16 mVtxs[3];
    };

    TrampleStar(const char*);

    virtual void init(const TableIter& rIter);
    virtual void draw() const;
    virtual void calcAnim();
    virtual void control();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void toSphere(f32);
    void initCheckFlag(u32);
    u16 readCheckFlag(u16, u16);
    void writeCheckFlag(u16, u16, u16);
    void divide();
    void moveVtx(f32, u32, u32, f32, u32);
    void calcSurface(bool);
    void calcLodSurface();
    void onLod();
    void offLod();

    void exeWait();
    void exeBindingCapture();
    void exeBindingShoot();
    u32 calcNearestVtxIndex(const Vec3f&);
    void drawSelf() const;

    u16* mCheckFlags;
    u16 mNumFlags;
    u16 mNumSurfaces;
    u16 mNumVtxs;
    Vec3f* mVtxs;
    f32* mDeformCoeff;
    f32* mDeformSpeed;
    Surface* mSurfaces;
    Surface* mLodSurfaces;
    GameTexture* mTexture;
    Vec2f* mTexST;
    s16* mVtxOverlap;
    u16 mNumDrawVtxs;
    Vec3f* mDrawPos;
    Vec3f* mDrawNorm;
    Vec2f* mDrawTex;
    f32* mDrawDeformCoeffs;
    bool mIsLod;
    u16 mCaptureVtx;
    f32 mDeformRate;
    f32 mJumpVel;
    f32 mSpringRate;
    u16 mCaptureFrames;
};

class TrampleStarShare : public LiveActor {
public:
    TrampleStarShare(TrampleStar*);
    void copy(TrampleStar*) const;

    TrampleStar* mTrampleStar;
};
