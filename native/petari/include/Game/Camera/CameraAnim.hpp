#pragma once

#include "Game/Camera/Camera.hpp"

// CANM/CKAN camera animations, read in place in host order as the importer cooks them (crates/nebula-import
// cook/native_data.rs canm: the type's last letter lower case, "CANm"/"CKAn").
struct CanmFileHeader {
    u8 mMagic[4];
    u8 mType[4];
    s32 _8;
    s32 _C;
    s32 _10;
    s32 _14;
    u32 mNrFrames;
    u32 mValueOffset;
};

struct CanmKeyFrameComponentInfo {
    u32 mCount;
    u32 mOffset;
    u32 mType;
};

struct CanmKeyFrameInfo {
    CanmKeyFrameComponentInfo mPosX;
    CanmKeyFrameComponentInfo mPosY;
    CanmKeyFrameComponentInfo mPosZ;
    CanmKeyFrameComponentInfo mWatchPosX;
    CanmKeyFrameComponentInfo mWatchPosY;
    CanmKeyFrameComponentInfo mWatchPosZ;
    CanmKeyFrameComponentInfo mTwist;
    CanmKeyFrameComponentInfo mFovy;
};

struct CamnFrameComponentInfo {
    u32 mCount;
    u32 mOffset;
};

struct CanmFrameInfo {
    CamnFrameComponentInfo mPosX;
    CamnFrameComponentInfo mPosY;
    CamnFrameComponentInfo mPosZ;
    CamnFrameComponentInfo mWatchPosX;
    CamnFrameComponentInfo mWatchPosY;
    CamnFrameComponentInfo mWatchPosZ;
    CamnFrameComponentInfo mTwist;
    CamnFrameComponentInfo mFovy;
};

class BaseCamAnmDataAccessor;
class CamAnmDataAccessor;
class KeyCamAnmDataAccessor;

class CameraAnim : public Camera {
public:
    CameraAnim(const char* pName = "\203\101\203\152\203\201\201\133\203\126\203\207\203\223\203\112\203\201\203\211" /* アニメーションカメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isInterpolationOff() const {
        return true;
    }
    virtual bool isCollisionOff() const {
        return true;
    }
    virtual bool isZeroFrameMoveOff() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void setParam(u8*, f32);
    bool isAnimEnd() const;
    static u32 getAnimFrame(u8*);
    bool loadBin(u8*);

    s32 _4C;
    s32 _50;
    u32 mNrFrames;
    bool mIsKey;
    f32 mSpeed;
    f32 mCurrentFrame;
    BaseCamAnmDataAccessor* mFileDataAccessor;
    CamAnmDataAccessor* mDataAccessor;
    KeyCamAnmDataAccessor* mKeyDataAccessor;
    u32 mNrValues;
    s32 _74;
    u8* mFileData;
    bool mIsPaused;
};

class BaseCamAnmDataAccessor {
public:
    inline BaseCamAnmDataAccessor() {
    }

    virtual inline ~BaseCamAnmDataAccessor() {
    }

    virtual void set(void*, void*) = 0;
    virtual void getPos(TVec3f*, f32) const = 0;
    virtual void getWatchPos(TVec3f*, f32) const = 0;
    virtual f32 getTwist(f32) const = 0;
    virtual f32 getFovy(f32) const = 0;
};

class CamAnmDataAccessor : public BaseCamAnmDataAccessor {
public:
    inline CamAnmDataAccessor() {
        mInfo = nullptr;
        mValues = nullptr;
    }

    virtual void set(void*, void*);
    virtual void getPos(TVec3f*, f32) const;
    virtual void getWatchPos(TVec3f*, f32) const;
    virtual f32 getTwist(f32) const;
    virtual f32 getFovy(f32) const;

    f32 get(f32, u32, u32) const;

    f32 getValue(int index) const {
        return mValues[index];
    }

    CanmFrameInfo* mInfo;
    f32* mValues;
};

class KeyCamAnmDataAccessor : public BaseCamAnmDataAccessor {
public:
    inline KeyCamAnmDataAccessor() {
        mInfo = nullptr;
        mValues = nullptr;
    }

    virtual void set(void*, void*);
    virtual void getPos(TVec3f*, f32) const;
    virtual void getWatchPos(TVec3f*, f32) const;
    virtual f32 getTwist(f32) const;
    virtual f32 getFovy(f32) const;

    f32 get(f32, u32, u32, u32) const;
    u32 searchKeyFrameIndex(f32, u32, u32, u32) const;
    f32 get3f(f32, u32, u32) const;
    f32 get4f(f32, u32, u32) const;
    f32 calcHermite(f32, f32, f32, f32, f32, f32, f32) const;

    CanmKeyFrameInfo* mInfo;
    f32* mValues;
};
