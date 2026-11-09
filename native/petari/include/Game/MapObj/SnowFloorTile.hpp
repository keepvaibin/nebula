#pragma once

class TypedDraw;

#include "Game/LiveActor/LiveActor.hpp"

class GameTexture;

class SnowFloorTile : public LiveActor {
public:
    SnowFloorTile(const char*);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void initLocalPoint(const TableIter&);
    bool calcLocalPoint(const Vec3f&, Vec3f*, f32);
    void createReduceMap(u16);
    void digPlayerWalk(const Vec3f&);
    void digPointer(const Vec3f&);
    bool doErase(const Vec3f&, f32, f32);
    f32 doMove(Vec2f, Vec2f, f32, f32);
    void doErase(Vec2f, f32, f32);
    void createTexture();
    void drawLayer(TypedDraw&, s32) const;

    Vec3f mRight;
    Vec3f mUp;
    Vec3f mForward;
    f32 mCellSize;
    s32 mTextureShift;
    s32 mTextureSize;
    Vec2f mLastPlayerPoint;
    u16 mDirtyFrames;
    u8* mImage;
    GameTexture* mTexture;
    f32* mSnowDepth;
    f32* mReduceMap;
    u16 mReduceMapSize;
    Vec2f mLastPointerPoint;
    GameTexture* mTextures[2];
    u8 mTextureIndex;
};
