#pragma once

class TypedDraw;

#include "Game/LiveActor/LiveActor.hpp"

class JUTTexture;

class SnowFloorTile : public LiveActor {
public:
    SnowFloorTile(const char*);

    virtual ~SnowFloorTile();
    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void initLocalPoint(const TableIter&);
    bool calcLocalPoint(const Vec3f&, Vec3f*, f32);
    void createReduceMap(u16);
    void digPlayerWalk(const Vec3f&);
    void digDpd(const Vec3f&);
    bool doErase(const Vec3f&, f32, f32);
    f32 doMove(Vec2f, Vec2f, f32, f32);
    void doErase(Vec2f, f32, f32);
    void createTexture();
    void drawLayer(TypedDraw&, s32) const;

    u8 _8C[12];
    Vec3f mRight;
    Vec3f mUp;
    Vec3f mForward;
    f32 mCellSize;
    s32 mTextureShift;
    s32 mTextureSize;
    Vec2f mLastPlayerPoint;
    u8 _D0[8];
    u16 mDirtyFrames;
    u8* mImage;
    JUTTexture* mTexture;
    f32* mSnowDepth;
    f32* mReduceMap;
    u16 mReduceMapSize;
    Vec2f mLastDpdPoint;
    u16 _F8;
    f32 _FC;
    JUTTexture* mTextures[2];
    u8 mTextureIndex;
};
