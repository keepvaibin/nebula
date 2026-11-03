#pragma once
#include "Game/Util/Array.hpp"
#include "Game/LiveActor/LiveActor.hpp"
class JUTTexture;
class SnowFloor : public LiveActor {
public:
    SnowFloor(const char*);

    virtual ~SnowFloor();
    virtual void init(const JMapInfoIter&) override;
    virtual void draw() const override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    void drawSetup();
    void initLocalPoint(const JMapInfoIter&);
    bool calcLocalPoint(const TVec3f&, TVec3f*);
    void createReduceMap(u16);
    void digPlayerWalk(const TVec3f&);
    void digDpd(const TVec3f&);
    bool doErase(const TVec3f&, f32, f32);
    f32 doMove(TVec2f, TVec2f, f32, f32);
    void doErase(TVec2f, f32, f32);
    void createTexture();
    void addCheckList(HitSensor*);
    void updateCheckList();
    f32 calcCoverSnow(const TVec2f&, f32);
    void drawLayer(s32) const;
    f32 mHalfLength;
    f32 mHalfAngle;
    f32 mRadius;
    TVec3f mUp;
    TVec3f mAxis;
    TVec2f mLastPlayerPoint;
    f32* mTexCoords;
    TVec3f* mDirections;
    u16 mDirtyFrames;
    u8* mImage;
    JUTTexture* mTexture;
    f32* mSnowDepth;
    u8* mBaseDepth;
    f32* mReduceMap;
    u16 mReduceMapSize;
    TVec2f mLastDpdPoint;
    u16 _E4;
    f32 _E8;
    f32 mWalkingResist;
    u16 mResistFrames;
    u16 mCheckCount;
    MR::GrowTable< HitSensor*, 8 > mCheckList;
    JUTTexture* mTextures[2];
    u8 mTextureIndex;
};
