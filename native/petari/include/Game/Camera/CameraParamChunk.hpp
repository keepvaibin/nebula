#pragma once

#include "Game/Camera/CameraParamString.hpp"
#include <nebula_geometry.hpp>

class CameraHolder;
class CameraParamChunkID;
class DotCamReader;

class CameraGeneralParam {
public:
    CameraGeneralParam()
        : mDist(1200.0f), mAxis(0.0f, 1.0f, 0.0f), mWPoint(0.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mAngleA(), mAngleB(0.3f), mNum1(), mNum2(),
          mString() {
    }

    CameraGeneralParam& operator=(const CameraGeneralParam&);

    f32 mDist;
    Vec3f mAxis;
    Vec3f mWPoint;
    Vec3f mUp;
    f32 mAngleA;
    f32 mAngleB;
    s32 mNum1;
    // CAM_TYPE_ANIM's animation data, kept in mNum1.
    u8* mAnimData;
    s32 mNum2;
    CameraParamString mString;
};

class CameraParamChunk {
public:
    class ExParam {
    public:
        ExParam() {
        }

        void init() {
            mWOffset.set< f32 >(0.0f, 100.0f, 0.0f);
            mLOffset = 0.0f;
            mLOffsetV = 0.0f;
            mRoll = 0.0f;
            mFovy = 45.0f;
            mCamInt = 120;
            mFlag = 0;
            mUpper = 0.3f;
            mLower = 0.1f;
            mGndInt = 160;
            mUPlay = 300.0f;
            mLPlay = 800.0f;
            mPushDelay = 120;
            mPushDelayLow = 120;
            mUDown = 120;
            mVPanUse = true;
            mVPanAxis.set(0.0f, 1.0f, 0.0f);
        }

        void setWOffset(const Vec3f& rWOffset) {
            mWOffset.set(rWOffset);
        }

        Vec3f mWOffset;
        f32 mLOffset;
        f32 mLOffsetV;
        f32 mRoll;
        f32 mFovy;
        s32 mCamInt;
        u16 mFlag;
        f32 mUpper;
        f32 mLower;
        s32 mGndInt;
        f32 mUPlay;
        f32 mLPlay;
        s32 mPushDelay;
        s32 mPushDelayLow;
        s32 mUDown;
        s32 mVPanUse;
        Vec3f mVPanAxis;
    };

    CameraParamChunk(CameraHolder*, const CameraParamChunkID&);

    virtual void copy(const CameraParamChunk*);
    virtual void load(DotCamReader*, CameraHolder*);
    virtual void initiate();
    virtual const char* getClassName() const {
        return "Base";
    }

    s32 getZoneID() const;
    bool isOnNoReset() const;
    bool isOnUseFovy() const;
    bool isLOfsErpOff() const;
    bool isAntiBlurOff() const;
    bool isCollisionOff() const;
    bool isSubjectiveCameraOff() const;
    void getVPanAxis(Vec3f*) const;

    void setCameraType(const char*, const CameraHolder*);
    void setUseFovy(bool);
    void setLOfsErpOff(bool);
    void setCollisionOff(bool);

    void arrangeCamTypeName(u32, const char**);

    CameraGeneralParam* getGeneralParam() {
        return mGeneralParam;
    }

    u8 getCameraTypeIndex() const {
        return mCameraTypeIndex;
    }

    CameraParamChunkID* mParamChunkID;
    u8 mCameraTypeIndex;
    ExParam mExParam;
    CameraGeneralParam* mGeneralParam;
    bool mIsCodeChunk;
};

class CameraParamChunkGame : public CameraParamChunk {
public:
    CameraParamChunkGame(CameraHolder*, const CameraParamChunkID&);

    virtual void copy(const CameraParamChunk*) override;
    virtual void load(DotCamReader*, CameraHolder*) override;
    virtual void initiate() override;
    virtual const char* getClassName() const override {
        return "Game";
    }

    u8 mThru;
    u8 mEnableEndErpFrame;
    u32 mCamEndInt;
};

class CameraParamChunkEvent : public CameraParamChunk {
public:
    CameraParamChunkEvent(CameraHolder*, const CameraParamChunkID&);

    virtual void copy(const CameraParamChunk*) override;
    virtual void load(DotCamReader*, CameraHolder*) override;
    virtual void initiate() override;
    virtual const char* getClassName() const override {
        return "Event";
    }

    u8 mEnableErpFrame;
    u32 mEvFrame;
    u8 mEnableEndErpFrame;
    u32 mCamEndInt;
    u32 mEvPriority;
};
