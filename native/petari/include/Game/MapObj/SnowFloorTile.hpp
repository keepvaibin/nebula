#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;

class SnowFloorTile : public LiveActor {
public:
    SnowFloorTile(const char*);

    virtual ~SnowFloorTile();
    virtual void init(const JMapInfoIter&) override;
    virtual void draw() const override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void initLocalPoint(const JMapInfoIter&);
    bool calcLocalPoint(const TVec3f&, TVec3f*, f32);
    void createReduceMap(u16);
    void digPlayerWalk(const TVec3f&);
    void digDpd(const TVec3f&);
    bool doErase(const TVec3f&, f32, f32);
    f32 doMove(TVec2f, TVec2f, f32, f32);
    void doErase(TVec2f, f32, f32);
    void createTexture();
    void drawLayer(s32) const;

    u8 _8C[12];
    TVec3f mRight;
    TVec3f mUp;
    TVec3f mForward;
    f32 mCellSize;
    s32 mTextureShift;
    s32 mTextureSize;
    TVec2f mLastPlayerPoint;
    u8 _D0[8];
    u16 mDirtyFrames;
    u8* mImage;
    JUTTexture* mTexture;
    f32* mSnowDepth;
    f32* mReduceMap;
    u16 mReduceMapSize;
    TVec2f mLastDpdPoint;
    u16 _F8;
    f32 _FC;
    JUTTexture* mTextures[2];
    u8 mTextureIndex;
};
