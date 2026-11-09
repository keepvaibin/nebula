#pragma once

class TypedDraw;
#include "Game/Util/Array.hpp"
#include "Game/LiveActor/LiveActor.hpp"
class GameTexture;
class SnowFloor : public LiveActor {
public:
    SnowFloor(const char*);

    virtual void init(const TableIter&) override;
    virtual void draw() const override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    void drawSetup();
    void initLocalPoint(const TableIter&);
    bool calcLocalPoint(const Vec3f&, Vec3f*);
    void createReduceMap(u16);
    void digPlayerWalk(const Vec3f&);
    void digPointer(const Vec3f&);
    bool doErase(const Vec3f&, f32, f32);
    f32 doMove(Vec2f, Vec2f, f32, f32);
    void doErase(Vec2f, f32, f32);
    void createTexture();
    void addCheckList(HitSensor*);
    void updateCheckList();
    f32 calcCoverSnow(const Vec2f&, f32);
    void drawLayer(TypedDraw&, s32) const;
    f32 mHalfLength;
    f32 mHalfAngle;
    f32 mRadius;
    Vec3f mUp;
    Vec3f mAxis;
    Vec2f mLastPlayerPoint;
    f32* mTexCoords;
    Vec3f* mDirections;
    s32 mDirtyFrames;
    u8* mImage;
    GameTexture* mTexture;
    f32* mSnowDepth;
    u8* mBaseDepth;
    f32* mReduceMap;
    u16 mReduceMapSize;
    Vec2f mLastPointerPoint;
    f32 mWalkingResist;
    s32 mResistFrames;
    u16 mCheckCount;
    MR::GrowTable< HitSensor*, 8 > mCheckList;
    GameTexture* mTextures[2];
    u8 mTextureIndex;
};
